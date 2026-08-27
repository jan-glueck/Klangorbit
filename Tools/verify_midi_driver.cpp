#include <cstdio>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/MidiDriver.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-3f)
    {
        return std::abs (a - b) <= tolerance;
    }

    struct RecordingListener : public CanonicalInputHub::Listener
    {
        std::vector<CanonicalInputEvent> received;
        void canonicalInputReceived (const CanonicalInputEvent& event) override { received.push_back (event); }
    };
}

int main()
{
    using Kind = CanonicalInputEvent::Kind;
    using Polarity = ParameterRegistry::Polarity;

    // --- Control Change -> Continuous, Unipolar, 0..127 normalized to 0..1 ---
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        MidiDriver driver;
        juce::MidiBuffer buffer;
        buffer.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 0); // channel 1, CC1, value 64 (~midpoint of 0..127)
        driver.processMidiBuffer (buffer);
        driver.drainAndDispatch (hub);

        check (listener.received.size() == 1, "CC: exactly one event dispatched");
        check (listener.received.size() == 1 && listener.received[0].sourceId == "Midi0.CC1.Ch1", "CC: sourceId is \"Midi0.CC<N>.Ch<C>\"");
        check (listener.received.size() == 1 && listener.received[0].kind == Kind::Continuous, "CC: dispatched as Continuous");
        check (listener.received.size() == 1 && listener.received[0].polarity == Polarity::Unipolar, "CC: dispatched as Unipolar");
        check (listener.received.size() == 1 && approxEqual (listener.received[0].value, 64.0f / 127.0f, 0.01f), "CC: value normalized from 0..127 to 0..1");

        hub.removeListener (&listener);
    }

    // --- Note On/Off -> Button, Unipolar, exactly 1.0/0.0 -----------------
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        MidiDriver driver;
        juce::MidiBuffer buffer;
        buffer.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0); // channel 2, note 60, velocity 100
        driver.processMidiBuffer (buffer);
        driver.drainAndDispatch (hub);

        check (listener.received.size() == 1, "NoteOn: exactly one event dispatched");
        check (listener.received.size() == 1 && listener.received[0].sourceId == "Midi0.Note60.Ch2", "NoteOn: sourceId is \"Midi0.Note<N>.Ch<C>\"");
        check (listener.received.size() == 1 && listener.received[0].kind == Kind::Button, "NoteOn: dispatched as Button (velocity NOT captured as a continuous value, see MidiDriver's own comment)");
        check (listener.received.size() == 1 && approxEqual (listener.received[0].value, 1.0f), "NoteOn: value is exactly 1.0 regardless of velocity");

        listener.received.clear();
        juce::MidiBuffer offBuffer;
        offBuffer.addEvent (juce::MidiMessage::noteOff (2, 60), 0);
        driver.processMidiBuffer (offBuffer);
        driver.drainAndDispatch (hub);

        check (listener.received.size() == 1 && approxEqual (listener.received[0].value, 0.0f), "NoteOff: value is exactly 0.0");
        check (listener.received.size() == 1 && listener.received[0].sourceId == "Midi0.Note60.Ch2", "NoteOff: same sourceId as the matching NoteOn (same note/channel)");

        hub.removeListener (&listener);
    }

    // --- Pitch Bend -> Continuous, Bipolar, 0..16383 (center 8192) -> -1..1 ---
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        MidiDriver driver;

        // Center position -> ~0.0
        juce::MidiBuffer centerBuffer;
        centerBuffer.addEvent (juce::MidiMessage::pitchWheel (3, 8192), 0);
        driver.processMidiBuffer (centerBuffer);
        driver.drainAndDispatch (hub);
        check (listener.received.size() == 1 && listener.received[0].kind == Kind::Continuous, "PitchBend: dispatched as Continuous");
        check (listener.received.size() == 1 && listener.received[0].polarity == Polarity::Bipolar, "PitchBend: dispatched as Bipolar");
        check (listener.received.size() == 1 && approxEqual (listener.received[0].value, 0.0f, 0.01f), "PitchBend: center (8192) maps to ~0.0");
        check (listener.received.size() == 1 && listener.received[0].sourceId == "Midi0.PitchBend.Ch3", "PitchBend: sourceId is \"Midi0.PitchBend.Ch<C>\"");

        // Full down -> -1.0
        listener.received.clear();
        juce::MidiBuffer downBuffer;
        downBuffer.addEvent (juce::MidiMessage::pitchWheel (3, 0), 0);
        driver.processMidiBuffer (downBuffer);
        driver.drainAndDispatch (hub);
        check (listener.received.size() == 1 && approxEqual (listener.received[0].value, -1.0f, 0.01f), "PitchBend: minimum (0) maps to -1.0");

        // Full up -> +1.0
        listener.received.clear();
        juce::MidiBuffer upBuffer;
        upBuffer.addEvent (juce::MidiMessage::pitchWheel (3, 16383), 0);
        driver.processMidiBuffer (upBuffer);
        driver.drainAndDispatch (hub);
        check (listener.received.size() == 1 && approxEqual (listener.received[0].value, 1.0f, 0.01f), "PitchBend: maximum (16383) maps to ~1.0");

        hub.removeListener (&listener);
    }

    // --- drainAndDispatch() with nothing queued is a safe no-op -----------
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        MidiDriver driver;
        driver.drainAndDispatch (hub);
        check (listener.received.empty(), "drainAndDispatch() with an empty queue dispatches nothing");

        hub.removeListener (&listener);
    }

    // --- Multiple messages in one block all get queued and dispatched -----
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        MidiDriver driver;
        juce::MidiBuffer buffer;
        buffer.addEvent (juce::MidiMessage::controllerEvent (1, 1, 10), 0);
        buffer.addEvent (juce::MidiMessage::controllerEvent (1, 2, 20), 5);
        buffer.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 10);
        driver.processMidiBuffer (buffer);
        driver.drainAndDispatch (hub);

        check (listener.received.size() == 3, "multiple messages in one MidiBuffer all get queued and dispatched together");

        hub.removeListener (&listener);
    }

    // --- Irrelevant message types (e.g. Program Change) are ignored -------
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        MidiDriver driver;
        juce::MidiBuffer buffer;
        buffer.addEvent (juce::MidiMessage::programChange (1, 5), 0);
        driver.processMidiBuffer (buffer);
        driver.drainAndDispatch (hub);

        check (listener.received.empty(), "Program Change (and other untranslated message types) produce no dispatched event");

        hub.removeListener (&listener);
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
