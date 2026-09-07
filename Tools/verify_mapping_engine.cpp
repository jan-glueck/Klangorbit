#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/MappingEngine.h"
#include "../Source/MappingProfileManager.h"

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

    ParameterRegistry makeTestRegistry (float* massBacking, float* gainBacking)
    {
        ParameterRegistry registry;
        registry.registerParameter ({
            "selectedObject.mass", "Mass", "Object Physics", 0.01f, 20.0f, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SelectedObject, -1,
            [massBacking] { return *massBacking; },
            [massBacking] (float v) { *massBacking = v; }
        });
        registry.registerParameter ({
            "selectedObject.gain", "Gain", "Object Physics", 0.0f, 2.0f, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SelectedObject, -1,
            [gainBacking] { return *gainBacking; },
            [gainBacking] (float v) { *gainBacking = v; }
        });
        return registry;
    }
}

int main()
{
    using Kind = CanonicalInputEvent::Kind;
    using Polarity = ParameterRegistry::Polarity;

    // --- Learn mode: the next event becomes a binding, then learning stops ---
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);

        check (! engine.isLearning(), "not learning by default");
        engine.startLearning ("selectedObject.mass");
        check (engine.isLearning(), "startLearning() arms learn mode");
        check (engine.getLearningParameterId() == "selectedObject.mass", "getLearningParameterId() reports the armed target");

        engine.canonicalInputReceived ({ "Gamepad0.LeftStick.X", Kind::Continuous, 0.5f, Polarity::Bipolar });

        check (! engine.isLearning(), "learn mode turns itself off after capturing one event");
        check (engine.hasBindingFor ("Gamepad0.LeftStick.X", 0), "the captured event became a binding in bank 0 (default, no modifier held)");
    }

    // --- Applying a binding writes through to the target parameter --------
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        // RightTrigger, not LeftTrigger -- LeftTrigger is MappingEngine's own
        // default modifierSourceId (see its class comment), which is never
        // itself bindable (canonicalInputReceived() intercepts it before any
        // binding lookup); this test needs an ordinary, non-modifier unipolar
        // source instead.
        engine.addBinding ({ "Gamepad0.RightTrigger", "selectedObject.mass", 0 });

        // Unipolar source (trigger, 0..1) -> Unipolar target (mass, 0.01..20):
        // 0.5 normalized should land at the target's own midpoint.
        engine.canonicalInputReceived ({ "Gamepad0.RightTrigger", Kind::Continuous, 0.5f, Polarity::Unipolar });
        check (approxEqual (mass, 0.01f + 0.5f * (20.0f - 0.01f), 0.1f), "a bound unipolar source writes its denormalized value to the target parameter");
    }

    // --- Polarity conversion: a bipolar source's FULL range reaches a -----
    //     unipolar target's FULL range (not just the upper half). ---
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        engine.addBinding ({ "Gamepad0.LeftStick.X", "selectedObject.gain", 0 });

        engine.canonicalInputReceived ({ "Gamepad0.LeftStick.X", Kind::Continuous, -1.0f, Polarity::Bipolar });
        check (approxEqual (gain, 0.0f, 0.01f), "bipolar source at its own minimum (-1) reaches the unipolar target's minimum, not its midpoint");

        engine.canonicalInputReceived ({ "Gamepad0.LeftStick.X", Kind::Continuous, 1.0f, Polarity::Bipolar });
        check (approxEqual (gain, 2.0f, 0.01f), "bipolar source at its own maximum (+1) reaches the unipolar target's maximum");

        engine.canonicalInputReceived ({ "Gamepad0.LeftStick.X", Kind::Continuous, 0.0f, Polarity::Bipolar });
        check (approxEqual (gain, 1.0f, 0.01f), "bipolar source at its own center (0) reaches the unipolar target's midpoint");
    }

    // --- Paging/banking: the modifier source switches which bank is -------
    //     active; a binding only applies while its own bank is active. ---
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        engine.setModifierSourceId ("Gamepad0.RightShoulder");
        engine.addBinding ({ "Gamepad0.LeftStick.X", "selectedObject.mass", 0 }); // bank 0 (default)
        engine.addBinding ({ "Gamepad0.LeftStick.X", "selectedObject.gain", 1 }); // bank 1 (modifier held)

        check (engine.getCurrentBank() == 0, "bank starts at 0 (modifier not held)");
        engine.canonicalInputReceived ({ "Gamepad0.LeftStick.X", Kind::Continuous, 1.0f, Polarity::Bipolar });
        check (approxEqual (mass, 20.0f, 0.1f) && approxEqual (gain, 1.0f), "bank 0: the same source drives the bank-0 binding (mass), not the bank-1 one (gain unchanged)");

        engine.canonicalInputReceived ({ "Gamepad0.RightShoulder", Kind::Button, 1.0f, Polarity::Unipolar });
        check (engine.getCurrentBank() == 1, "holding the modifier source switches the active bank to 1");

        engine.canonicalInputReceived ({ "Gamepad0.LeftStick.X", Kind::Continuous, 1.0f, Polarity::Bipolar });
        check (approxEqual (gain, 2.0f, 0.01f), "bank 1: the same source now drives the bank-1 binding (gain) instead");

        engine.canonicalInputReceived ({ "Gamepad0.RightShoulder", Kind::Button, 0.0f, Polarity::Unipolar });
        check (engine.getCurrentBank() == 0, "releasing the modifier switches back to bank 0");

        check (! engine.hasBindingFor ("Gamepad0.RightShoulder", 0) && ! engine.hasBindingFor ("Gamepad0.RightShoulder", 1),
               "the modifier source itself is never captured as a bindable target");
    }

    // --- Learn mode ignores the modifier source as a capture target ------
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        engine.setModifierSourceId ("Gamepad0.RightShoulder");
        engine.startLearning ("selectedObject.mass");

        engine.canonicalInputReceived ({ "Gamepad0.RightShoulder", Kind::Button, 1.0f, Polarity::Unipolar });
        check (engine.isLearning(), "the modifier source pressed while learning does NOT get captured -- learn mode stays armed");

        engine.canonicalInputReceived ({ "Gamepad0.ButtonA", Kind::Button, 1.0f, Polarity::Unipolar });
        check (! engine.isLearning() && engine.hasBindingFor ("Gamepad0.ButtonA", 1),
               "the next NON-modifier event is captured instead, in whatever bank is active at that moment (1, since the modifier is still held)");
    }

    // --- addBinding() replaces any existing binding for the same --------
    //     (sourceId, bank) pair, rather than accumulating duplicates. ---
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        engine.addBinding ({ "Gamepad0.LeftStick.X", "selectedObject.mass", 0 });
        engine.addBinding ({ "Gamepad0.LeftStick.X", "selectedObject.gain", 0 }); // same source+bank, different target

        check (engine.getBindings().size() == 1, "re-binding the same (sourceId, bank) replaces rather than accumulates");
        check (engine.getBindings()[0].parameterId == "selectedObject.gain", "the replacement binding's target wins");
    }

    // --- removeBinding() ----------------------------------------------------
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        engine.addBinding ({ "Gamepad0.LeftStick.X", "selectedObject.mass", 0 });
        engine.removeBinding ("Gamepad0.LeftStick.X", 0);
        check (! engine.hasBindingFor ("Gamepad0.LeftStick.X", 0), "removeBinding() removes the matching binding");
        check (engine.getBindings().empty(), "getBindings() reflects the removal");
    }

    // --- Mapping profile round-trip (save -> parse -> load) ----------------
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine source (registry);
        source.setModifierSourceId ("Gamepad0.LeftShoulder");
        source.addBinding ({ "Gamepad0.RightStick.Y", "selectedObject.mass", 0 });
        source.addBinding ({ "Gamepad0.ButtonB", "selectedObject.gain", 1 });

        const auto var = MappingProfileManager::profileToVar (source, "test-profile");
        const auto json = juce::JSON::toString (var);
        const auto reparsed = juce::JSON::parse (json);

        MappingEngine dest (registry);
        const auto result = MappingProfileManager::loadFromVar (reparsed, dest);
        check (result.wasOk(), "a round-tripped profile (toVar -> JSON string -> parse -> loadFromVar) loads without error");
        check (dest.getModifierSourceId() == "Gamepad0.LeftShoulder", "round-trip preserves the modifier source id");
        check (dest.getBindings().size() == 2, "round-trip preserves the number of bindings");
        check (dest.hasBindingFor ("Gamepad0.RightStick.Y", 0) && dest.hasBindingFor ("Gamepad0.ButtonB", 1),
               "round-trip preserves each binding's sourceId/bank");
    }

    // --- loadFromVar() rejects an unsupported schemaVersion -----------------
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);

        auto* badRoot = new juce::DynamicObject();
        badRoot->setProperty ("schemaVersion", 999);
        badRoot->setProperty ("bindings", juce::Array<juce::var>());
        const auto result = MappingProfileManager::loadFromVar (juce::var (badRoot), engine);
        check (result.failed(), "loadFromVar() rejects an unsupported schemaVersion instead of silently misinterpreting it");
    }

    // --- loadFromVar() replaces the whole binding set, doesn't merge -------
    {
        float mass = 1.0f, gain = 1.0f;
        auto registry = makeTestRegistry (&mass, &gain);
        MappingEngine engine (registry);
        engine.addBinding ({ "Gamepad0.DpadUp", "selectedObject.mass", 0 }); // pre-existing binding, should be gone after load

        auto* root = new juce::DynamicObject();
        root->setProperty ("schemaVersion", 1);
        root->setProperty ("name", "replacement");
        root->setProperty ("modifierSourceId", "Gamepad0.RightShoulder");
        juce::Array<juce::var> bindingsArray;
        auto* b = new juce::DynamicObject();
        b->setProperty ("sourceId", "Gamepad0.ButtonY");
        b->setProperty ("parameterId", "selectedObject.gain");
        b->setProperty ("bank", 0);
        bindingsArray.add (juce::var (b));
        root->setProperty ("bindings", bindingsArray);

        const auto result = MappingProfileManager::loadFromVar (juce::var (root), engine);
        check (result.wasOk(), "loading a valid replacement profile succeeds");
        check (! engine.hasBindingFor ("Gamepad0.DpadUp", 0), "loading REPLACES the binding set -- the old binding is gone");
        check (engine.hasBindingFor ("Gamepad0.ButtonY", 0), "loading REPLACES the binding set -- the new binding is present");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
