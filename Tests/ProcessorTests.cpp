/*
    Headless test runner for PowerExpressionMapper (formerly Expression Mapper).

    Exercises the plugin's core behaviour without a host or audio device:
      - listed notes pass through at their original sample position;
      - unlisted notes are delayed, including across block boundaries;
      - non-note events are never delayed;
      - state (entries + delay) survives save / load;
      - CSV import / export;
      - CLAP note-name reporting.

    Returns 0 when every check passes, 1 otherwise.
*/

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/NoteNameUtils.h"

namespace
{
    int failures = 0;
    int checks   = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.toRawUTF8());
        }
    }

    using Event = std::pair<int, juce::MidiMessage>;

    std::vector<Event> collect (const juce::MidiBuffer& midi)
    {
        std::vector<Event> v;
        for (const auto md : midi)
            v.emplace_back ((int) md.samplePosition, md.getMessage());
        return v;
    }

    /** Runs one processBlock round and returns the resulting MIDI events. */
    std::vector<Event> runBlock (ExpressionMapperAudioProcessor& p,
                                 juce::MidiBuffer input, const int numSamples)
    {
        juce::AudioBuffer<float> audio (2, numSamples);
        p.processBlock (audio, input);
        return collect (input);
    }

    juce::MidiBuffer makeBuffer (const std::vector<Event>& events)
    {
        juce::MidiBuffer buffer;
        for (const auto& e : events)
            buffer.addEvent (e.second, e.first);
        return buffer;
    }

    juce::MidiMessage noteOn  (int channel, int note, juce::uint8 vel) { return juce::MidiMessage::noteOn  (channel, note, vel); }
    juce::MidiMessage noteOff (int channel, int note, juce::uint8 vel) { return juce::MidiMessage::noteOff (channel, note, vel); }
    juce::MidiMessage cc      (int channel, int controller, int value) { return juce::MidiMessage::controllerEvent (channel, controller, value); }

    // Count how many note-ons of a given note number are in the event list.
    int countNoteOns (const std::vector<Event>& events, int note)
    {
        int n = 0;
        for (const auto& e : events)
            if (e.second.isNoteOn() && e.second.getNoteNumber() == note)
                ++n;
        return n;
    }

    // First sample position at which a note-on of `note` appears, or -1.
    int firstNoteOnPos (const std::vector<Event>& events, int note)
    {
        for (const auto& e : events)
            if (e.second.isNoteOn() && e.second.getNoteNumber() == note)
                return e.first;
        return -1;
    }

    //==========================================================================
    void testListedNotePassesThroughInstantly()
    {
        std::printf ("  testListedNotePassesThroughInstantly\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        // Default mapping lists Kick = 36. Default delay is 16.

        auto out = runBlock (p, makeBuffer ({ { 10, noteOn (1, 36, 100) },
                                              { 200, noteOff (1, 36, 0) } }), 256);

        check (out.size() == 2, "listed note: exactly 2 events come out");
        check (countNoteOns (out, 36) == 1, "listed note: one note-on for note 36");
        check (firstNoteOnPos (out, 36) == 10, "listed note: note-on keeps its sample position (10)");
        check (out.size() == 2 && out[1].first == 200 && out[1].second.isNoteOff(), "listed note: note-off keeps its position (200)");
        check (out.size() == 2 && out[0].second.getVelocity() == 100, "listed note: velocity preserved");
        check (out.size() == 2 && out[0].second.getChannel() == 1, "listed note: channel preserved");
    }

    void testUnlistedNoteIsDelayedWithinBlock()
    {
        std::printf ("  testUnlistedNoteIsDelayedWithinBlock\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.setDelaySamples (16);

        auto out = runBlock (p, makeBuffer ({ { 5, noteOn (1, 40, 90) },
                                              { 100, noteOff (1, 40, 0) } }), 256);

        check (out.size() == 2, "unlisted: both events come out");
        check (firstNoteOnPos (out, 40) == 5 + 16, "unlisted: note-on delayed by 16 samples (21)");
        check (out.size() == 2 && out[1].first == 116 && out[1].second.isNoteOff(), "unlisted: note-off delayed by the same amount (116)");
        check (out.size() == 2 && out[0].second.getVelocity() == 90, "unlisted: velocity preserved");
    }

    void testUnlistedNoteCarriedAcrossBlockBoundary()
    {
        std::printf ("  testUnlistedNoteCarriedAcrossBlockBoundary\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.setDelaySamples (16);

        // Note-on at 250 in a 256-sample block: 250 + 16 = 266 -> next block at 10.
        auto out1 = runBlock (p, makeBuffer ({ { 250, noteOn (1, 41, 80) } }), 256);
        check (out1.empty(), "carry-over: nothing emitted in the first block");

        auto out2 = runBlock (p, {}, 256);
        check (out2.size() == 1, "carry-over: event arrives in the second block");
        check (! out2.empty() && out2[0].first == 10, "carry-over: emitted at sample 10 (sample-accurate)");
        check (! out2.empty() && out2[0].second.getNoteNumber() == 41, "carry-over: correct note number");

        // And a really long delay spanning three blocks (delay 600, block 256).
        p.setDelaySamples (600);
        auto outA = runBlock (p, makeBuffer ({ { 0, noteOn (1, 42, 70) } }), 256);
        check (outA.empty(), "long delay: first block silent");
        auto outB = runBlock (p, {}, 256);
        check (outB.empty(), "long delay: second block silent");
        auto outC = runBlock (p, {}, 256);
        check (outC.size() == 1 && outC[0].first == (0 + 600) - 2 * 256, "long delay: emitted in third block at sample 88");
    }

    void testNonNoteEventsAreNeverDelayed()
    {
        std::printf ("  testNonNoteEventsAreNeverDelayed\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.setDelaySamples (16);

        auto out = runBlock (p, makeBuffer ({ { 7, cc (1, 1, 64) },
                                              { 9, juce::MidiMessage::pitchWheel (1, 4000) } }), 256);

        check (out.size() == 2, "non-note: both events come out");
        check (out.size() == 2 && out[0].first == 7 && out[0].second.isController(), "non-note: CC keeps position 7");
        check (out.size() == 2 && out[1].first == 9 && out[1].second.isPitchWheel(), "non-note: pitch bend keeps position 9");
    }

    void testZeroDelayMeansEverythingIsInstant()
    {
        std::printf ("  testZeroDelayMeansEverythingIsInstant\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.setDelaySamples (0);

        auto out = runBlock (p, makeBuffer ({ { 3, noteOn (1, 99, 60) } }), 256);

        check (firstNoteOnPos (out, 99) == 3, "zero delay: unlisted note keeps its position");
    }

    void testEmptyListDelaysEverything()
    {
        std::printf ("  testEmptyListDelaysEverything\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.setDelaySamples (16);
        p.clearEntries();

        auto out = runBlock (p, makeBuffer ({ { 3, noteOn (1, 36, 100) } }), 256);

        check (firstNoteOnPos (out, 36) == 19, "empty list: even a default-listed note is delayed (3 + 16)");
    }

    void testOrderingListedBeforeDelayed()
    {
        std::printf ("  testOrderingListedBeforeDelayed\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.setDelaySamples (32);

        // Unlisted note arrives first (pos 5), listed note later (pos 10).
        // After routing: listed stays at 10, unlisted lands at 37 -> listed fires first.
        auto out = runBlock (p, makeBuffer ({ { 5, noteOn (1, 60, 100) },
                                              { 10, noteOn (1, 36, 100) } }), 256);

        check (out.size() == 2, "ordering: two events out");
        check (out.size() == 2 && out[0].first == 10 && out[0].second.getNoteNumber() == 36, "ordering: listed note (36) fires first at 10");
        check (out.size() == 2 && out[1].first == 37 && out[1].second.getNoteNumber() == 60, "ordering: unlisted note (60) fires later at 37");
    }

    void testStateRoundTrip()
    {
        std::printf ("  testStateRoundTrip\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);

        p.clearEntries();
        p.addEntry();
        p.setEntry (0, { "Vibrato", 71 });
        p.addEntry();
        p.setEntry (1, { "Slide", 72 });
        p.setDelaySamples (100);

        juce::MemoryBlock state;
        p.getStateInformation (state);

        ExpressionMapperAudioProcessor q;   // fresh instance, defaults
        q.prepareToPlay (48000, 256);
        q.setStateInformation (state.getData(), (int) state.getSize());

        check (q.getNumEntries() == 2, "state: 2 entries restored");
        check (q.getNumEntries() > 0 && q.getEntry (0).name == "Vibrato" && q.getEntry (0).note == 71, "state: entry 0 restored");
        check (q.getNumEntries() > 1 && q.getEntry (1).name == "Slide" && q.getEntry (1).note == 72, "state: entry 1 restored");
        check (q.getDelaySamples() == 100, "state: delay restored");

        // And the restored delay really affects routing.
        auto out = runBlock (q, makeBuffer ({ { 1, noteOn (1, 60, 64) } }), 256);
        check (firstNoteOnPos (out, 60) == 101, "state: restored delay is active (1 + 100)");
    }

    void testInvalidStateIsRejected()
    {
        std::printf ("  testInvalidStateIsRejected\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        const int entriesBefore = p.getNumEntries();

        const char junk[] = "this is not valid state data";
        p.setStateInformation (junk, (int) sizeof (junk));

        check (p.getNumEntries() == entriesBefore, "invalid state: mapping unchanged");
    }

    void testCsvImportExport()
    {
        std::printf ("  testCsvImportExport\n");
        ExpressionMapping m;
        m.addEntry ({ "Kick", 36 });
        m.addEntry ({ "Snare", 38 });

        const auto csv = m.toCsvString();
        check (csv == "Kick,36\nSnare,38", "csv export: exact \"name,note\" lines");

        // Round-trip via import.
        check (m.fromCsvString (csv), "csv import: round-trip succeeds");
        check (m.getNumEntries() == 2 && m.getEntry (1).name == "Snare" && m.getEntry (1).note == 38, "csv import: entries restored");

        // Note names instead of numbers are accepted ("D1" = 38).
        check (m.fromCsvString ("HiHat,D1"), "csv import: note-name field accepted");
        check (m.getNumEntries() == 1 && m.getEntry (0).name == "HiHat" && m.getEntry (0).note == 38, "csv import: D1 parsed as note 38");

        // Drum Mapper .bwdrm lines (5 fields) are tolerated: name + source note.
        check (m.fromCsvString ("Old,24,0,36,10"), "csv import: 5-field drum-mapper line accepted");
        check (m.getNumEntries() == 1 && m.getEntry (0).name == "Old" && m.getEntry (0).note == 24, "csv import: name + note taken from drum-mapper line");

        // Garbage is rejected (and leaves the mapping alone).
        check (! m.fromCsvString ("only-a-name"), "csv import: single-column line rejected");
        check (m.getNumEntries() == 1, "csv import: failed import leaves mapping untouched");
    }

    void testFileRoundTrip()
    {
        std::printf ("  testFileRoundTrip\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.clearEntries();
        p.addEntry();
        p.setEntry (0, { "Clap", 45 });

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("expression_mapper_test.exprmap");
        file.deleteFile();

        check (p.exportToFile (file), "file export: succeeds");
        check (file.existsAsFile(), "file export: file exists");

        p.clearEntries();
        check (p.importFromFile (file), "file import: succeeds");
        check (p.getNumEntries() == 1 && p.getEntry (0).name == "Clap" && p.getEntry (0).note == 45, "file import: entry restored");

        file.deleteFile();
    }

    void testHostNoteNameQueries()
    {
        std::printf ("  testHostNoteNameQueries\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);

        // The VST3 (IUnitInfo pitch names) and VST2 (effGetMidiKeyName) paths
        // both go through AudioProcessor::getNameForMidiNoteNumber.
        const auto kick = p.getNameForMidiNoteNumber (36, 1);
        check (kick.has_value() && *kick == "Kick", "host query: note 36 channel 1 is \"Kick\"");
        check (p.getNameForMidiNoteNumber (38, 10).has_value() && *p.getNameForMidiNoteNumber (38, 10) == "Snare",
               "host query: names apply to every channel");

        check (! p.getNameForMidiNoteNumber (40, 1).has_value(), "host query: unlisted note has no name");
        check (! p.getNameForMidiNoteNumber (-1, 1).has_value(), "host query: negative note safe");
        check (! p.getNameForMidiNoteNumber (200, 1).has_value(), "host query: note > 127 safe (some hosts ask)");

        // After clearing, no names at all.
        p.clearEntries();
        check (! p.getNameForMidiNoteNumber (36, 1).has_value(), "host query: no names after clear");

        // findEntryForNote model helper.
        ExpressionMapping m;
        m.addEntry ({ "A", 10 });
        m.addEntry ({ "B", 11 });
        check (m.findEntryForNote (11) != nullptr && m.findEntryForNote (11)->name == "B", "model: findEntryForNote finds B");
        check (m.findEntryForNote (10) != nullptr && m.findEntryForNote (10)->name == "A", "model: findEntryForNote finds A");
        check (m.findEntryForNote (12) == nullptr, "model: findEntryForNote rejects unknown note");
    }

    void testClapNoteNameExtension()
    {
        std::printf ("  testClapNoteNameExtension\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);

        check (p.supportsNoteName(), "clap: note-name extension supported");

        clap_note_name name {};
        check (p.noteNameCount() == 3, "clap: 3 default entries reported");

        check (p.noteNameGet (0, &name), "clap: index 0 available");
        check (juce::String (name.name) == "Kick", "clap: name 0 is \"Kick\"");
        check (name.key == 36, "clap: key 0 is 36 (Kick)");
        check (name.channel == -1, "clap: channel -1 (every channel)");
        check (name.port == -1, "clap: port -1 (every port)");

        check (p.noteNameGet (2, &name) && juce::String (name.name) == "ESnare" && name.key == 39, "clap: index 2 is ESnare @ 39");
        check (! p.noteNameGet (3, &name), "clap: out-of-range index rejected");

        // After clearing, the host sees no names.
        p.clearEntries();
        check (p.noteNameCount() == 0, "clap: count 0 after clear");
        check (! p.noteNameGet (0, &name), "clap: get after clear rejected");
    }

    void testModelBasics()
    {
        std::printf ("  testModelBasics\n");
        ExpressionMapping m;

        m.addEntry ({ "A", 10 });
        m.addEntry ({ "B", 11 });
        m.insertEntry (1, { "C", 12 });
        check (m.getNumEntries() == 3 && m.getEntry (1).name == "C", "model: insertEntry at index 1");

        check (m.containsNote (10) && m.containsNote (11) && m.containsNote (12), "model: containsNote finds all");
        check (! m.containsNote (13), "model: containsNote rejects unknown");

        m.setEntry (0, { "A2", 14 });
        check (m.getEntry (0).name == "A2" && m.getEntry (0).note == 14, "model: setEntry replaces");
        check (m.containsNote (14) && ! m.containsNote (10), "model: containsNote follows the edit");

        m.removeEntry (0);
        check (m.getNumEntries() == 2 && m.getEntry (0).name == "C", "model: removeEntry");
        m.clear();
        check (m.isEmpty() && ! m.containsNote (11), "model: clear");

        m.setDelaySamples (123456);   // clamped
        check (m.getDelaySamples() == 8192, "model: delay clamped to 8192");
        m.setDelaySamples (-5);       // clamped
        check (m.getDelaySamples() == 0, "model: delay clamped to 0");
    }

    void testParseNoteText()
    {
        std::printf ("  testParseNoteText\n");

        // Item label with parenthesised number.
        check (NoteNameUtils::parseNoteText ("C1 (36)") == 36, "parse: full item label \"C1 (36)\"");
        check (NoteNameUtils::parseNoteText ("D#1 (39)") == 39, "parse: full item label \"D#1 (39)\"");

        // Bare note names (DAW convention, C3 = 60).
        check (NoteNameUtils::parseNoteText ("C1") == 36, "parse: name \"C1\"");
        check (NoteNameUtils::parseNoteText ("D#2") == 51, "parse: sharp name \"D#2\"");
        check (NoteNameUtils::parseNoteText ("Bb0") == 34, "parse: flat name \"Bb0\"");
        check (NoteNameUtils::parseNoteText ("  C1  ") == 36, "parse: surrounding whitespace trimmed");

        // Raw numbers.
        check (NoteNameUtils::parseNoteText ("36") == 36, "parse: raw number");
        check (NoteNameUtils::parseNoteText ("127") == 127, "parse: max note");
        check (NoteNameUtils::parseNoteText ("0") == 0, "parse: zero note");

        // Rejections.
        check (NoteNameUtils::parseNoteText ("128") == -1, "parse: 128 rejected");
        check (NoteNameUtils::parseNoteText ("-3") == -1, "parse: negative rejected");
        check (NoteNameUtils::parseNoteText ("C1 (3") == -1, "parse: half-typed label rejected");
        check (NoteNameUtils::parseNoteText ("C1 (abc)") == -1, "parse: non-numeric parens rejected");
        check (NoteNameUtils::parseNoteText ("hello") == -1, "parse: garbage rejected");
        check (NoteNameUtils::parseNoteText ("") == -1, "parse: empty rejected");
    }

    void testMultiBlockStress()
    {
        std::printf ("  testMultiBlockStress\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 64);
        p.setDelaySamples (32);

        const int totalNotes = 100;
        const int blockSize  = 64;
        const int delay      = 32;

        std::vector<Event> emitted;
        for (int i = 0; i < totalNotes; ++i)
        {
            const int note = (i % 2 == 0) ? 36 : 60 + (i % 50);   // alternate listed / unlisted
            const int pos  = i % blockSize;

            // Velocity must stay 1-127: a note-on with velocity 0 is a note-off.
            auto out = runBlock (p, makeBuffer ({ { pos, noteOn (1, note, (juce::uint8) (1 + i % 127)) } }), blockSize);
            emitted.insert (emitted.end(), out.begin(), out.end());
        }
        // Drain pending tail.
        for (int b = 0; b < 20; ++b)
        {
            auto out = runBlock (p, {}, blockSize);
            emitted.insert (emitted.end(), out.begin(), out.end());
        }

        check ((int) emitted.size() == totalNotes, "stress: no notes lost or duplicated");
        check (countNoteOns (emitted, 36) == 50, "stress: 50 listed kicks came through");

        bool allPositionsValid = true;
        for (const auto& e : emitted)
            allPositionsValid = allPositionsValid && e.first >= 0 && e.first < blockSize;
        check (allPositionsValid, "stress: all positions inside the block");
    }

    void testNoteNameClampAndDuplicateNotes()
    {
        std::printf ("  testNoteNameClampAndDuplicateNotes\n");
        ExpressionMapperAudioProcessor p;
        p.prepareToPlay (48000, 256);
        p.clearEntries();
        p.addEntry();
        p.setEntry (0, { "First", 40 });
        p.addEntry();
        p.setEntry (1, { "Second", 40 });   // same note twice is allowed

        check (p.getNumEntries() == 2, "dup: two entries accepted");

        // The note is listed, so it must trigger directly despite the duplicate.
        auto out = runBlock (p, makeBuffer ({ { 4, noteOn (1, 40, 90) } }), 256);
        check (firstNoteOnPos (out, 40) == 4, "dup: duplicated note still passes through instantly");
    }
}

//==============================================================================
int main()
{
    // Keep test output flowing even when something dies mid-run.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    // AudioProcessor / ChangeBroadcaster rely on the JUCE message machinery.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf ("=== PowerExpressionMapper tests ===\n");

    testListedNotePassesThroughInstantly();
    testUnlistedNoteIsDelayedWithinBlock();
    testUnlistedNoteCarriedAcrossBlockBoundary();
    testNonNoteEventsAreNeverDelayed();
    testZeroDelayMeansEverythingIsInstant();
    testEmptyListDelaysEverything();
    testOrderingListedBeforeDelayed();
    testStateRoundTrip();
    testInvalidStateIsRejected();
    testCsvImportExport();
    testFileRoundTrip();
    testClapNoteNameExtension();
    testHostNoteNameQueries();
    testModelBasics();
    testParseNoteText();
    testMultiBlockStress();
    testNoteNameClampAndDuplicateNotes();

    std::printf ("=== %d checks, %d failures ===\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
