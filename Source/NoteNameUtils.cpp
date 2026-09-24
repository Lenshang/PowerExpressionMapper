#include "NoteNameUtils.h"

namespace NoteNameUtils
{
    static const char* const kNoteNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    // Index 0 -> 'C', 1 -> 'D', ... used to accept letter input.
    static int letterToPitchClass (juce::juce_wchar c) noexcept
    {
        switch (juce::CharacterFunctions::toUpperCase (c))
        {
            case 'C': return 0;
            case 'D': return 2;
            case 'E': return 4;
            case 'F': return 5;
            case 'G': return 7;
            case 'A': return 9;
            case 'B': return 11;
            default:  return -1;
        }
    }

    juce::String midiToName (int noteNumber)
    {
        if (! juce::isPositiveAndBelow (noteNumber, 128))
            return {};

        const auto pitchClass = noteNumber % 12;
        const auto octave = (noteNumber / 12) - 2;  // Bitwig/DAW convention: C3 = 60
        return juce::String (kNoteNames[pitchClass]) + juce::String (octave);
    }

    juce::String midiToNameWithNumber (int noteNumber)
    {
        const auto name = midiToName (noteNumber);
        return name.isEmpty() ? juce::String ("---")
                              : name + " (" + juce::String (noteNumber) + ")";
    }

    int nameToMidi (const juce::String& input)
    {
        if (input.isEmpty())
            return -1;

        const auto trimmed = input.trim();
        auto t = trimmed.getCharPointer();

        // 1. Leading note letter (A-G / a-g).
        const auto letter = t.getAndAdvance();
        const auto pitchClass = letterToPitchClass (letter);
        if (pitchClass < 0)
            return -1;

        int pc = pitchClass;

        // 2. Accidentals: a run of '#' (sharps) and/or 'b' (flats).
        bool hasAccidental = false;
        while (*t == '#' || *t == 'b' || *t == 'B')
        {
            if (*t == '#')
            {
                ++pc;
                hasAccidental = true;
            }
            else // 'b' or 'B'
            {
                --pc;
                hasAccidental = true;
            }
            ++t;
        }

        // Wrap accidental into 0-11.
        pc = ((pc % 12) + 12) % 12;

        // 3. Octave number (optionally signed).
        juce::String octavePart (t);
        octavePart = octavePart.trim();
        if (octavePart.isEmpty())
            return -1;

        int octave = 0;
        if (! octavePart.trim().containsOnly ("-0123456789"))
            return -1;

        octave = octavePart.getIntValue();
        const int noteNumber = (octave + 2) * 12 + pc;  // Bitwig/DAW convention: C3 = 60

        if (! juce::isPositiveAndBelow (noteNumber, 128))
            return -1;

        (void) hasAccidental;
        return noteNumber;
    }

    int parseNoteText (const juce::String& input)
    {
        const auto t = input.trim();
        if (t.isEmpty())
            return -1;

        // "C1 (36)" style: trust only a complete parenthesised number, so a
        // half-typed label like "C1 (3" is rejected rather than misparsed.
        if (t.containsChar ('(') || t.containsChar (')'))
        {
            if (! (t.containsChar ('(') && t.endsWithChar (')')))
                return -1;

            const auto inner = t.fromFirstOccurrenceOf ("(", false, false)
                                  .upToLastOccurrenceOf (")", false, false)
                                  .trim();

            if (! inner.containsOnly ("0123456789"))
                return -1;

            const int value = inner.getIntValue();
            return (value >= 0 && value < 128) ? value : -1;
        }

        const int byName = nameToMidi (t);
        if (byName >= 0)
            return byName;

        if (t.containsOnly ("0123456789"))
        {
            const int value = t.getIntValue();
            return (value >= 0 && value < 128) ? value : -1;
        }

        return -1;
    }

    juce::String channelToString (int channel)
    {
        return isAllChannels (channel) ? "ALL" : juce::String (channel);
    }

    int stringToChannel (const juce::String& text)
    {
        const auto trimmed = text.trim();
        if (trimmed.equalsIgnoreCase ("ALL") || trimmed.equalsIgnoreCase ("A") || trimmed.isEmpty())
            return 0;

        const auto value = trimmed.getIntValue();
        if (value >= 1 && value <= 16)
            return value;

        return 0;
    }
}
