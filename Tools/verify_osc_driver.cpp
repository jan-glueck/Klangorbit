#include <cstdio>
#include "../Source/OscDriver.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }
}

int main()
{
    using Kind = CanonicalInputEvent::Kind;
    using Polarity = ParameterRegistry::Polarity;

    // --- Float argument -> Continuous, Unipolar, address becomes sourceId ---
    {
        juce::OSCMessage message ("/orbit/x", 0.75f);
        CanonicalInputEvent event;
        bool ok = OscInterpretation::interpretMessage (message, event);

        check (ok, "float arg: interpretMessage() returns true");
        check (ok && event.sourceId == "OSC./orbit/x", "float arg: sourceId is \"OSC.\" + address pattern");
        check (ok && event.kind == Kind::Continuous, "float arg: dispatched as Continuous");
        check (ok && event.polarity == Polarity::Unipolar, "float arg: dispatched as Unipolar");
        check (ok && approxEqual (event.value, 0.75f), "float arg: value passed through directly (already normalized convention)");
    }

    // --- Int32 argument -> Continuous, treated as a float value -----------
    {
        juce::OSCMessage message ("/orbit/y", (juce::int32) 1);
        CanonicalInputEvent event;
        bool ok = OscInterpretation::interpretMessage (message, event);

        check (ok, "int32 arg: interpretMessage() returns true");
        check (ok && event.kind == Kind::Continuous, "int32 arg: dispatched as Continuous");
        check (ok && approxEqual (event.value, 1.0f), "int32 arg: value converted to float");
    }

    // --- No arguments -> Button, value 1.0 (bare trigger convention) ------
    {
        juce::OSCMessage message ("/trigger/fire");
        CanonicalInputEvent event;
        bool ok = OscInterpretation::interpretMessage (message, event);

        check (ok, "no args: interpretMessage() returns true");
        check (ok && event.sourceId == "OSC./trigger/fire", "no args: sourceId still uses \"OSC.\" + address pattern");
        check (ok && event.kind == Kind::Button, "no args: dispatched as Button");
        check (ok && approxEqual (event.value, 1.0f), "no args: value is exactly 1.0");
        check (ok && event.polarity == Polarity::Unipolar, "no args: dispatched as Unipolar");
    }

    // --- Non-numeric first argument (string) -> returns false -------------
    {
        juce::OSCMessage message ("/text/label", juce::String ("hello"));
        CanonicalInputEvent event;
        bool ok = OscInterpretation::interpretMessage (message, event);

        check (! ok, "string arg: interpretMessage() returns false for a non-numeric first argument");
    }

    // --- Out-of-range float values are clamped to [0,1] --------------------
    {
        juce::OSCMessage highMessage ("/orbit/z", 5.0f);
        CanonicalInputEvent highEvent;
        bool okHigh = OscInterpretation::interpretMessage (highMessage, highEvent);
        check (okHigh && approxEqual (highEvent.value, 1.0f), "float arg: value > 1.0 is clamped to 1.0");

        juce::OSCMessage lowMessage ("/orbit/z", -3.0f);
        CanonicalInputEvent lowEvent;
        bool okLow = OscInterpretation::interpretMessage (lowMessage, lowEvent);
        check (okLow && approxEqual (lowEvent.value, 0.0f), "float arg: value < 0.0 is clamped to 0.0");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
