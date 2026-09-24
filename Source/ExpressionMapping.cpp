#include "ExpressionMapping.h"
#include "NoteNameUtils.h"

ExpressionEntry ExpressionMapping::getEntry (int index) const
{
    if (index >= 0 && index < (int) entries.size())
        return entries[(size_t) index];

    return {};
}

void ExpressionMapping::setEntry (int index, const ExpressionEntry& entry)
{
    if (index >= 0 && index < (int) entries.size())
        entries[(size_t) index] = entry;
}

void ExpressionMapping::addEntry (const ExpressionEntry& entry)
{
    entries.push_back (entry);
}

void ExpressionMapping::insertEntry (int index, const ExpressionEntry& entry)
{
    if (index < 0)
        index = 0;
    if (index > (int) entries.size())
        index = (int) entries.size();

    entries.insert (entries.begin() + index, entry);
}

void ExpressionMapping::removeEntry (int index)
{
    if (index >= 0 && index < (int) entries.size())
        entries.erase (entries.begin() + index);
}

void ExpressionMapping::clear()
{
    entries.clear();
}

bool ExpressionMapping::containsNote (int noteNumber) const noexcept
{
    return findEntryForNote (noteNumber) != nullptr;
}

const ExpressionEntry* ExpressionMapping::findEntryForNote (int noteNumber) const noexcept
{
    for (const auto& e : entries)
        if (e.note == noteNumber)
            return &e;

    return nullptr;
}

void ExpressionMapping::setDelaySamples (int samples) noexcept
{
    delaySamples = juce::jlimit (0, 8192, samples);
}

//==============================================================================
juce::ValueTree ExpressionMapping::toValueTree() const
{
    juce::ValueTree root ("ExpressionMapping");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("delaySamples", delaySamples, nullptr);

    for (const auto& e : entries)
    {
        juce::ValueTree child ("Entry");
        child.setProperty ("name", e.name, nullptr);
        child.setProperty ("note", e.note, nullptr);
        root.appendChild (child, nullptr);
    }

    return root;
}

bool ExpressionMapping::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.isValid() || tree.getType() != juce::Identifier ("ExpressionMapping"))
        return false;

    std::vector<ExpressionEntry> loaded;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (child.getType() != juce::Identifier ("Entry"))
            continue;

        ExpressionEntry e;
        e.name = child.getProperty ("name", juce::String()).toString();
        e.note = juce::jlimit (0, 127, (int) child.getProperty ("note", 60));

        loaded.push_back (e);
    }

    entries = std::move (loaded);
    setDelaySamples ((int) tree.getProperty ("delaySamples", 16));
    return true;
}

ExpressionMapping ExpressionMapping::createDefault()
{
    // General MIDI kit names on their usual notes (Bitwig convention: C3 = 60).
    ExpressionMapping m;
    m.addEntry ({ "Kick",   36 });  // C1
    m.addEntry ({ "Snare",  38 });  // D1
    m.addEntry ({ "ESnare", 39 });  // D#1
    return m;
}

//==============================================================================
juce::String ExpressionMapping::toCsvString() const
{
    juce::StringArray lines;

    for (const auto& e : entries)
        lines.add (e.name + "," + juce::String (e.note));

    return lines.joinIntoString ("\n");
}

bool ExpressionMapping::fromCsvString (const juce::String& text)
{
    std::vector<ExpressionEntry> loaded;
    auto lines = juce::StringArray::fromLines (text);

    for (const auto& line : lines)
    {
        auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;

        auto tokens = juce::StringArray::fromTokens (trimmed, ",", "");
        tokens.removeEmptyStrings (false);

        if (tokens.size() < 2)
            continue;  // skip malformed lines

        ExpressionEntry e;
        e.name = tokens[0];

        // The note field may be a number ("38") or a note name ("D1").
        const auto noteField = tokens[1].trim();
        const int parsed = NoteNameUtils::nameToMidi (noteField);
        e.note = juce::jlimit (0, 127, parsed >= 0 ? parsed : noteField.getIntValue());

        loaded.push_back (e);
    }

    if (loaded.empty())
        return false;

    entries = std::move (loaded);
    return true;
}
