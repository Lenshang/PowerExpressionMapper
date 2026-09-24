#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/**
    A single name -> note assignment.

    Unlike the DrumMapper there is no remapping here: the note number is the
    note the user plays and the name is what the host should display for it.
*/
struct ExpressionEntry
{
    juce::String name;
    int note = 60;             // 0-127
};

/**
    Owns the ordered list of expression entries plus the delay setting.

    This is a plain data model (no UI / threading concerns): mutation happens
    on the message thread from the editor, while the audio thread only reads
    it under the processor's spin lock.

    Routing contract (implemented by the processor):
      - a note whose number appears in the list passes through instantly;
      - every other note is delayed by `delaySamples`.
*/
class ExpressionMapping
{
public:
    ExpressionMapping() = default;

    int  getNumEntries() const noexcept { return (int) entries.size(); }
    bool isEmpty()       const noexcept { return entries.empty(); }

    ExpressionEntry getEntry (int index) const;
    void setEntry (int index, const ExpressionEntry& entry);
    void addEntry (const ExpressionEntry& entry);
    void insertEntry (int index, const ExpressionEntry& entry);
    void removeEntry (int index);
    void clear();

    /** Returns the raw entries (read only). Used by the table model. */
    const std::vector<ExpressionEntry>& getEntries() const noexcept { return entries; }

    /** True if any entry uses the given note number. Called from the audio thread. */
    bool containsNote (int noteNumber) const noexcept;

    /** Returns the first entry using the given note number, or nullptr.
        Used for host note-name queries (CLAP / VST3 / VST2). */
    const ExpressionEntry* findEntryForNote (int noteNumber) const noexcept;

    /** Number of samples unlisted notes are delayed by. */
    int  getDelaySamples() const noexcept { return delaySamples; }
    void setDelaySamples (int samples) noexcept;

    /** Serialises the mapping (entries + delay) for host state / preset storage. */
    juce::ValueTree toValueTree() const;

    /** Replaces all entries and the delay from a ValueTree produced by toValueTree(). */
    bool fromValueTree (const juce::ValueTree& tree);

    /** A small starter mapping demonstrating the naming concept. */
    static ExpressionMapping createDefault();

    /** Exports the mapping to a CSV string, one "name,note" pair per line. */
    juce::String toCsvString() const;

    /** Imports a mapping from a CSV string, replacing all entries.
        Returns true on success. Invalid lines are silently skipped.
        Accepts both "name,note" lines and the 5-field lines of the Drum
        Mapper's .bwdrm format (name + source note are taken from those). */
    bool fromCsvString (const juce::String& text);

private:
    std::vector<ExpressionEntry> entries;
    int delaySamples = 16;
};
