#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/ParameterRegistry.h"

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
    using Polarity = ParameterRegistry::Polarity;
    using Scope = ParameterRegistry::Scope;

    // --- registerParameter() / find() ------------------------------------
    {
        ParameterRegistry registry;
        float backing = 3.0f;

        registry.registerParameter ({
            "test.param", "Test Param", "Test Category", 0.0f, 10.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; },
            [&backing] (float v) { backing = v; }
        });

        const auto* found = registry.find ("test.param");
        check (found != nullptr, "find(): a registered id is found");
        check (found != nullptr && found->displayName == "Test Param", "find(): displayName round-trips correctly");
        check (found != nullptr && found->category == "Test Category", "find(): category round-trips correctly");
        check (found != nullptr && found->scope == Scope::Global, "find(): scope round-trips correctly");
        check (registry.find ("missing.id") == nullptr, "find(): an unregistered id returns nullptr");
    }

    // --- get/set actually reach the backing storage -----------------------
    {
        ParameterRegistry registry;
        float backing = 1.0f;
        registry.registerParameter ({
            "test.roundtrip", "Roundtrip", "Test", 0.0f, 1.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; },
            [&backing] (float v) { backing = v; }
        });

        const auto* p = registry.find ("test.roundtrip");
        check (p != nullptr && approxEqual (p->getValue(), 1.0f), "getValue() reads the current backing value");
        p->setValue (0.42f);
        check (approxEqual (backing, 0.42f), "setValue() writes through to the backing storage");
        check (approxEqual (p->getValue(), 0.42f), "getValue() reflects a value just set");
    }

    // --- normalize()/denormalize(): unipolar -------------------------------
    {
        ParameterRegistry::Descriptor d;
        d.minValue = 0.0f; d.maxValue = 10.0f; d.polarity = Polarity::Unipolar;

        check (approxEqual (d.normalize (5.0f), 0.5f), "Unipolar: normalize(midpoint) == 0.5");
        check (approxEqual (d.normalize (0.0f), 0.0f), "Unipolar: normalize(min) == 0.0");
        check (approxEqual (d.normalize (10.0f), 1.0f), "Unipolar: normalize(max) == 1.0");
        check (approxEqual (d.normalize (-5.0f), 0.0f), "Unipolar: normalize() clamps below range to 0.0");
        check (approxEqual (d.normalize (15.0f), 1.0f), "Unipolar: normalize() clamps above range to 1.0");

        check (approxEqual (d.denormalize (0.5f), 5.0f), "Unipolar: denormalize(0.5) == midpoint");
        check (approxEqual (d.denormalize (0.0f), 0.0f), "Unipolar: denormalize(0.0) == min");
        check (approxEqual (d.denormalize (1.0f), 10.0f), "Unipolar: denormalize(1.0) == max");
        check (approxEqual (d.denormalize (2.0f), 10.0f), "Unipolar: denormalize() clamps above 1.0 to max");
        check (approxEqual (d.denormalize (-1.0f), 0.0f), "Unipolar: denormalize() clamps below 0.0 to min");
    }

    // --- normalize()/denormalize(): bipolar --------------------------------
    {
        ParameterRegistry::Descriptor d;
        d.minValue = -10.0f; d.maxValue = 10.0f; d.polarity = Polarity::Bipolar;

        check (approxEqual (d.normalize (0.0f), 0.0f), "Bipolar: normalize(midpoint) == 0.0");
        check (approxEqual (d.normalize (-10.0f), -1.0f), "Bipolar: normalize(min) == -1.0");
        check (approxEqual (d.normalize (10.0f), 1.0f), "Bipolar: normalize(max) == 1.0");
        check (approxEqual (d.normalize (-50.0f), -1.0f), "Bipolar: normalize() clamps below range to -1.0");
        check (approxEqual (d.normalize (50.0f), 1.0f), "Bipolar: normalize() clamps above range to 1.0");

        check (approxEqual (d.denormalize (0.0f), 0.0f), "Bipolar: denormalize(0.0) == midpoint");
        check (approxEqual (d.denormalize (-1.0f), -10.0f), "Bipolar: denormalize(-1.0) == min");
        check (approxEqual (d.denormalize (1.0f), 10.0f), "Bipolar: denormalize(1.0) == max");
        check (approxEqual (d.denormalize (2.0f), 10.0f), "Bipolar: denormalize() clamps above 1.0 to max");
        check (approxEqual (d.denormalize (-2.0f), -10.0f), "Bipolar: denormalize() clamps below -1.0 to min");

        // Asymmetric range sanity: normalize/denormalize should still be
        // mutually consistent even when min/max aren't symmetric around 0.
        ParameterRegistry::Descriptor asym;
        asym.minValue = -2.0f; asym.maxValue = 8.0f; asym.polarity = Polarity::Bipolar;
        const float mid = asym.denormalize (0.0f);
        check (approxEqual (mid, 3.0f), "Bipolar (asymmetric range): denormalize(0.0) == arithmetic midpoint");
        check (approxEqual (asym.normalize (mid), 0.0f), "Bipolar (asymmetric range): normalize(denormalize(0.0)) round-trips to 0.0");
    }

    // --- Scope::SpecificObject and multiple independent entries -----------
    {
        ParameterRegistry registry;
        float objectMass[3] = { 1.0f, 2.0f, 3.0f };

        for (int i = 0; i < 3; ++i)
        {
            registry.registerParameter ({
                "object." + juce::String (i) + ".mass", "Mass", "Object Physics", 0.01f, 20.0f, Polarity::Unipolar,
                Scope::SpecificObject, i,
                [&objectMass, i] { return objectMass[i]; },
                [&objectMass, i] (float v) { objectMass[i] = v; }
            });
        }

        check (registry.all().size() == 3, "SpecificObject: three distinct object slots register as three entries");
        const auto* obj1 = registry.find ("object.1.mass");
        check (obj1 != nullptr && obj1->objectId == 1, "SpecificObject: objectId matches the registered slot");
        obj1->setValue (99.0f);
        check (approxEqual (objectMass[1], 99.0f), "SpecificObject: setValue() only touches its own slot's backing value");
        check (approxEqual (objectMass[0], 1.0f) && approxEqual (objectMass[2], 3.0f),
               "SpecificObject: other slots' backing values are untouched by an unrelated setValue()");
    }

    // --- Scope::SelectedObject: dynamic retargeting ------------------------
    {
        ParameterRegistry registry;
        float objectGain[2] = { 0.5f, 0.75f };
        int selectedIndex = -1;

        registry.registerParameter ({
            "selectedObject.gain", "Gain", "Object Physics", 0.0f, 2.0f, Polarity::Unipolar, Scope::SelectedObject, -1,
            [&objectGain, &selectedIndex]
            {
                return (selectedIndex >= 0 && selectedIndex < 2) ? objectGain[selectedIndex] : 0.0f;
            },
            [&objectGain, &selectedIndex] (float v)
            {
                if (selectedIndex >= 0 && selectedIndex < 2) objectGain[selectedIndex] = v;
            }
        });

        const auto* p = registry.find ("selectedObject.gain");
        check (p != nullptr && approxEqual (p->getValue(), 0.0f), "SelectedObject: getValue() is inert (0.0) while nothing is selected");
        p->setValue (5.0f);
        check (approxEqual (objectGain[0], 0.5f) && approxEqual (objectGain[1], 0.75f),
               "SelectedObject: setValue() is a silent no-op while nothing is selected -- no backing value changes");

        selectedIndex = 0;
        check (approxEqual (p->getValue(), 0.5f), "SelectedObject: getValue() resolves to slot 0 once it's selected");
        p->setValue (1.0f);
        check (approxEqual (objectGain[0], 1.0f), "SelectedObject: setValue() writes to slot 0 while it's selected");

        selectedIndex = 1;
        check (approxEqual (p->getValue(), 0.75f), "SelectedObject: getValue() now resolves to slot 1 after the selection changed");
        p->setValue (2.0f);
        check (approxEqual (objectGain[1], 2.0f) && approxEqual (objectGain[0], 1.0f),
               "SelectedObject: setValue() now writes to slot 1 only, slot 0 stays at its last value");
    }

    // --- all() preserves registration order --------------------------------
    {
        ParameterRegistry registry;
        for (int i = 0; i < 5; ++i)
        {
            registry.registerParameter ({
                "seq." + juce::String (i), "Seq", "Test", 0.0f, 1.0f, Polarity::Unipolar, Scope::Global, -1,
                [] { return 0.0f; }, [] (float) {}
            });
        }
        bool inOrder = true;
        for (int i = 0; i < 5; ++i)
            if (registry.all()[(size_t) i].id != ("seq." + juce::String (i)))
                inOrder = false;
        check (inOrder, "all(): entries stay in registration order");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
