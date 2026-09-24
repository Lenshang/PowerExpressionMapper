#include "PluginProcessor.h"
#include "PluginEditor.h"

ExpressionMapperAudioProcessor::ExpressionMapperAudioProcessor()
    : juce::AudioProcessor (BusesProperties())
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    mapping = ExpressionMapping::createDefault();
    delaySamplesAtomic.store (mapping.getDelaySamples(), std::memory_order_relaxed);
    pendingNotes.reserve (256);
}

//==============================================================================
void ExpressionMapperAudioProcessor::prepareToPlay (double, int)
{
    // Drop notes carried over from a previous stream state (transport jump etc.).
    pendingNotes.clear();
}

bool ExpressionMapperAudioProcessor::isBusesLayoutSupported (const BusesLayout&) const
{
    // A MIDI effect has no audio buses; accept whatever layout the host probes.
    return true;
}

void ExpressionMapperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    processMidi (midiMessages, buffer.getNumSamples());
}

void ExpressionMapperAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    processMidi (midiMessages, buffer.getNumSamples());
}

void ExpressionMapperAudioProcessor::processMidi (juce::MidiBuffer& midi, const int numSamples)
{
    if (numSamples <= 0)
        return;

    const int delay = delaySamplesAtomic.load (std::memory_order_relaxed);

    // Snapshot the incoming events first: the buffer is rebuilt below, so we
    // must not iterate it while adding to it.
    std::vector<std::pair<int, juce::MidiMessage>> incoming;
    incoming.reserve ((size_t) midi.getNumEvents());

    for (const auto metadata : midi)
        incoming.emplace_back ((int) metadata.samplePosition, metadata.getMessage());

    midi.clear();

    // 1) Emit pending notes whose delay has expired within this block. The
    //    stored offset is relative to the start of this block; anything that
    //    still reaches past its end is carried over.
    for (size_t i = 0; i < pendingNotes.size();)
    {
        auto& p = pendingNotes[i];

        if (p.samplesRemaining < numSamples)
        {
            midi.addEvent (p.message, p.samplesRemaining);
            pendingNotes.erase (pendingNotes.begin() + (std::ptrdiff_t) i);
        }
        else
        {
            p.samplesRemaining -= numSamples;
            ++i;
        }
    }

    // 2) Route the snapshot. Listed notes keep their original sample position;
    //    unlisted notes are pushed `delay` samples into the future.
    for (auto& e : incoming)
    {
        auto& message = e.second;
        const int samplePos = e.first;

        if (message.isNoteOnOrOff() && ! isListedNote (message.getNoteNumber()))
        {
            const int emitPos = samplePos + delay;

            if (emitPos < numSamples)
                midi.addEvent (message, emitPos);
            else
                pendingNotes.push_back ({ emitPos - numSamples, std::move (message) });

            continue;
        }

        // Listed notes and all non-note events pass through untouched.
        // (MidiBuffer keeps itself sorted, so the deferred emissions above
        // and the events re-added here end up in time order.)
        midi.addEvent (message, samplePos);
    }
}

//==============================================================================
int ExpressionMapperAudioProcessor::getNumEntries()
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return mapping.getNumEntries();
}

ExpressionEntry ExpressionMapperAudioProcessor::getEntry (int index)
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return mapping.getEntry (index);
}

void ExpressionMapperAudioProcessor::setEntry (int index, const ExpressionEntry& entry)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.setEntry (index, entry);
    }
    // Value edits don't change the table structure, so just mark host state dirty.
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true));
    // Note names may have changed (name or note).
    noteNamesChanged();
}

void ExpressionMapperAudioProcessor::addEntry()
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.addEntry ({ "New", 60 });
    }
    markStateChanged();
}

void ExpressionMapperAudioProcessor::insertEntry (int index)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.insertEntry (index, { "New", 60 });
    }
    markStateChanged();
}

void ExpressionMapperAudioProcessor::removeEntry (int index)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.removeEntry (index);
    }
    markStateChanged();
}

void ExpressionMapperAudioProcessor::clearEntries()
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.clear();
    }
    markStateChanged();
}

int ExpressionMapperAudioProcessor::getDelaySamples()
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return mapping.getDelaySamples();
}

void ExpressionMapperAudioProcessor::setDelaySamples (int samples)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.setDelaySamples (samples);
    }
    delaySamplesAtomic.store (mapping.getDelaySamples(), std::memory_order_relaxed);
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true));
}

//==============================================================================
bool ExpressionMapperAudioProcessor::exportToFile (const juce::File& file)
{
    juce::String csv;
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        csv = mapping.toCsvString();
    }

    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (csv))
        return false;

    return temp.overwriteTargetFileWithTemporary();
}

bool ExpressionMapperAudioProcessor::importFromFile (const juce::File& file)
{
    auto text = file.loadFileAsString();
    bool changed = false;

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        changed = mapping.fromCsvString (text);
    }

    if (changed)
        markStateChanged();

    return changed;
}

bool ExpressionMapperAudioProcessor::isListedNote (int noteNumber) const noexcept
{
    // Never block the audio thread: if the message thread is editing, pass
    // the note through instantly (the less surprising fallback).
    const juce::SpinLock::ScopedTryLockType tryLock (mappingLock);
    if (! tryLock.isLocked())
        return true;

    return mapping.containsNote (noteNumber);
}

void ExpressionMapperAudioProcessor::markStateChanged()
{
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true));
    sendChangeMessage();
    // Structural change means note names changed too.
    noteNamesChanged();
}

//==============================================================================
void ExpressionMapperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree tree;
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        tree = mapping.toValueTree();
    }

    if (auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destData);
}

void ExpressionMapperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        bool changed = false;
        {
            const juce::SpinLock::ScopedLockType sl (mappingLock);
            changed = mapping.fromValueTree (tree);
        }

        if (changed)
        {
            delaySamplesAtomic.store (mapping.getDelaySamples(), std::memory_order_relaxed);
            markStateChanged();
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ExpressionMapperAudioProcessor::createEditor()
{
    return new ExpressionMapperAudioProcessorEditor (*this);
}

//==============================================================================
// Host note-name queries (VST3 IUnitInfo pitch names / VST2 effGetMidiKeyName)
//==============================================================================
std::optional<juce::String> ExpressionMapperAudioProcessor::getNameForMidiNoteNumber (int note, int /*midiChannel*/)
{
    // Note: some hosts query notes outside 0-127, so don't clamp — the lookup
    // simply fails for anything without an entry.
    const juce::SpinLock::ScopedLockType sl (mappingLock);

    if (const auto* entry = mapping.findEntryForNote (note))
        return entry->name;

    return std::nullopt;
}

//==============================================================================
// CLAP note-name extension
//==============================================================================
uint32_t ExpressionMapperAudioProcessor::noteNameCount() noexcept
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return static_cast<uint32_t> (mapping.getNumEntries());
}

bool ExpressionMapperAudioProcessor::noteNameGet (uint32_t index, clap_note_name* noteName) noexcept
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);

    if (index >= static_cast<uint32_t> (mapping.getNumEntries()))
        return false;

    const auto& entry = mapping.getEntry (static_cast<int> (index));

    // Copy the entry name into the CLAP struct.
    entry.name.copyToUTF8 (noteName->name, CLAP_NAME_SIZE);

    noteName->key = static_cast<int16_t> (entry.note);

    // Report the name for every channel and port.
    noteName->channel = -1;
    noteName->port = -1;

    return true;
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExpressionMapperAudioProcessor();
}
