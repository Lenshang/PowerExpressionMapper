#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wunused-parameter")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include "ExpressionMapping.h"

/**
    Main audio processor for PowerExpressionMapper.

    This is a pure MIDI effect: it has no audio buses and does NOT remap notes.

    Routing rules:
      - a note whose number appears in the user-edited list passes through
        untouched, at its original sample position (direct trigger);
      - every other note is delayed by a configurable number of samples.
        Delays that reach past the end of the current block are carried over
        to the following block, sample-accurately;
      - non-note events (CC, pitch bend, ...) are never delayed.

    The processor is also a ChangeBroadcaster: it notifies the editor whenever
    the mapping changes structurally (rows added/removed/cleared, delay edited,
    or a preset is loaded by the host) so the table view can refresh.

    It implements the CLAP note-name extension so the host (Bitwig) can display
    the entry names on its piano roll / note lanes.
*/
class ExpressionMapperAudioProcessor
    : public juce::AudioProcessor
    , public juce::ChangeBroadcaster
    , public clap_juce_extensions::clap_juce_audio_processor_capabilities
{
public:
    ExpressionMapperAudioProcessor();
    ~ExpressionMapperAudioProcessor() override = default;

    // ---- Mapping access (call from the message thread / editor) --------------
    int  getNumEntries();
    ExpressionEntry getEntry (int index);

    /** Edit operations. Each one locks the mapping, applies the change, tells
        the host the non-parameter state changed, and broadcasts to listeners. */
    void setEntry (int index, const ExpressionEntry& entry);
    void addEntry();
    void insertEntry (int index);
    void removeEntry (int index);
    void clearEntries();

    /** Current delay in samples. Mirrored into an atomic for the audio thread. */
    int  getDelaySamples();
    void setDelaySamples (int samples);

    /** Export the current mapping to a CSV file. Returns true on success. */
    bool exportToFile (const juce::File& file);

    /** Import a CSV file, replacing the current mapping. Returns true on success. */
    bool importFromFile (const juce::File& file);

    // ---- AudioProcessor overrides -------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- CLAP note-name extension -------------------------------------------
    /** Tells the host we provide custom note names. */
    bool supportsNoteName() const noexcept override { return true; }

    /** Returns the number of note names (one per entry). */
    uint32_t noteNameCount() noexcept override;

    /** Fills in the note name for the given index. */
    bool noteNameGet (uint32_t index, clap_note_name* noteName) noexcept override;

    // ---- Host note-name queries (VST3 IUnitInfo pitch names, VST2 effGetMidiKeyName)
    /** Hosts that support per-note names (Cubase, FL Studio, ...) call this to
        label notes on their piano roll. Returned for every channel: names are
        not channel-specific here. */
    std::optional<juce::String> getNameForMidiNoteNumber (int note, int midiChannel) override;

private:
    /** Shared MIDI routing for the float / double processBlock overloads. */
    void processMidi (juce::MidiBuffer& midi, int numSamples);

    /** Audio-thread check: is this note number in the list? Uses a try-lock;
        if the mapping is being edited the note is passed through instantly. */
    bool isListedNote (int noteNumber) const noexcept;

    /** Lock guarding `mapping`. Held by the editor on write and try-locked by
        the audio thread on read. */
    mutable juce::SpinLock mappingLock;
    ExpressionMapping mapping;

    /** Audio-thread snapshot of mapping.getDelaySamples(). */
    std::atomic<int> delaySamplesAtomic { 16 };

    /** Notes waiting to be emitted. `samplesRemaining` counts down from the
        start of the next block; only touched on the audio thread. */
    struct PendingNote
    {
        int samplesRemaining;
        juce::MidiMessage message;
    };
    std::vector<PendingNote> pendingNotes;

    void markStateChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionMapperAudioProcessor)
};
