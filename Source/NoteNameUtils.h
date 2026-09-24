#pragma once

#include <juce_core/juce_core.h>

/**
    Utilities for converting between MIDI note numbers (0-127) and human
    readable note names such as "C0", "D#0", "C1", etc.

    Naming convention used project-wide (Bitwig / DAW convention):
        MIDI note 0  -> C-2
        MIDI note 12 -> C-1
        MIDI note 24 -> C0
        MIDI note 60 -> C3  (middle C)

    Channel representation:
        channel 0      -> "ALL"  (matches / preserves any channel)
        channel 1..16  -> "1".."16"
*/
namespace NoteNameUtils
{
    /** Converts a MIDI note number to a name like "C0" or "D#1". Returns "" if out of range. */
    juce::String midiToName (int noteNumber);

    /** Converts a MIDI note number to a name with its number, e.g. "C0 (12)". */
    juce::String midiToNameWithNumber (int noteNumber);

    /** Parses a note name (e.g. "C0", "Db0", "D#1", "C-1") to a MIDI note number. Returns -1 if invalid. */
    int nameToMidi (const juce::String& name);

    /** Parses free text into a MIDI note number. Accepts an exact item label with
        a parenthesised number ("C1 (36)"), a note name ("C1", "D#2", "Bb0") or a
        raw number ("36"). Returns -1 when the text is not a usable note. */
    int parseNoteText (const juce::String& text);

    /** Converts a channel (0 = ALL, 1-16) to its display string. */
    juce::String channelToString (int channel);

    /** Parses a channel display string ("ALL" or "1".."16") to a channel number. Returns 0 (ALL) if invalid. */
    int stringToChannel (const juce::String& text);

    /** True if a channel value means "ALL" / any channel. */
    constexpr bool isAllChannels (int channel) noexcept { return channel <= 0; }
}
