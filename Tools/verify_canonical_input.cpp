#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/CanonicalInput.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
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

    // --- Basic dispatch: a listener receives what was sent -----------------
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);

        CanonicalInputEvent event;
        event.sourceId = "Gamepad0.LeftStick.X";
        event.kind = Kind::Continuous;
        event.value = 0.5f;
        event.polarity = Polarity::Bipolar;
        hub.dispatch (event);

        check (listener.received.size() == 1, "dispatch(): a registered listener receives exactly one event");
        check (listener.received.size() == 1 && listener.received[0].sourceId == "Gamepad0.LeftStick.X",
               "dispatch(): sourceId round-trips correctly");
        check (listener.received.size() == 1 && listener.received[0].kind == Kind::Continuous,
               "dispatch(): kind round-trips correctly");
        check (listener.received.size() == 1 && listener.received[0].value == 0.5f,
               "dispatch(): value round-trips correctly");
        check (listener.received.size() == 1 && listener.received[0].polarity == Polarity::Bipolar,
               "dispatch(): polarity round-trips correctly");

        hub.removeListener (&listener);
    }

    // --- Multiple listeners all receive the same event ---------------------
    {
        CanonicalInputHub hub;
        RecordingListener a, b, c;
        hub.addListener (&a);
        hub.addListener (&b);
        hub.addListener (&c);

        CanonicalInputEvent event;
        event.sourceId = "Gamepad0.ButtonA";
        event.kind = Kind::Button;
        event.value = 1.0f;
        event.polarity = Polarity::Unipolar;
        hub.dispatch (event);

        check (a.received.size() == 1 && b.received.size() == 1 && c.received.size() == 1,
               "dispatch(): every registered listener receives the same event");

        hub.removeListener (&a);
        hub.removeListener (&b);
        hub.removeListener (&c);
    }

    // --- removeListener() actually stops delivery ---------------------------
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);
        hub.dispatch ({});
        hub.removeListener (&listener);
        hub.dispatch ({});

        check (listener.received.size() == 1, "removeListener(): no further events are delivered after removal");
    }

    // --- addListener() is idempotent (no duplicate delivery) ---------------
    {
        CanonicalInputHub hub;
        RecordingListener listener;
        hub.addListener (&listener);
        hub.addListener (&listener); // second add, same pointer
        hub.dispatch ({});

        check (listener.received.size() == 1, "addListener(): registering the same listener twice doesn't duplicate delivery");
        hub.removeListener (&listener);
    }

    // --- A listener that adds/removes another listener from inside its own
    //     callback doesn't deadlock or corrupt iteration (snapshot-before-call). ---
    {
        CanonicalInputHub hub;
        RecordingListener innocent;

        struct ReentrantListener : public CanonicalInputHub::Listener
        {
            CanonicalInputHub& hubRef;
            RecordingListener& other;
            bool didReenter = false;
            ReentrantListener (CanonicalInputHub& h, RecordingListener& o) : hubRef (h), other (o) {}
            void canonicalInputReceived (const CanonicalInputEvent&) override
            {
                didReenter = true;
                hubRef.removeListener (&other); // reentrant modification during dispatch
            }
        };

        ReentrantListener reentrant (hub, innocent);
        hub.addListener (&innocent);
        hub.addListener (&reentrant);

        hub.dispatch ({}); // must not deadlock/crash; `innocent` still gets this in-flight event (snapshotted before the reentrant removal)
        check (reentrant.didReenter, "reentrant test: the reentrant listener's callback actually ran");
        check (innocent.received.size() == 1, "reentrant test: a listener removed mid-dispatch still receives that in-flight event (snapshot semantics)");

        hub.dispatch ({}); // innocent was removed by the first dispatch -- should NOT receive this one
        check (innocent.received.size() == 1, "reentrant test: the removed listener receives no further events afterward");

        hub.removeListener (&reentrant);
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
