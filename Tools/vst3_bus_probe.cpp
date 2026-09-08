/*
    Loads a built Klangorbit.vst3 the way a real VST3 host would -- via
    JUCE's own VST3PluginFormat, going through the ACTUAL VST3 ABI
    (IAudioProcessor::setBusArrangements()/getBusArrangement() calls into the
    real compiled plugin binary) -- rather than calling
    KlangorbitProcessor::isBusesLayoutSupported() directly in-process, which
    only tests our OWN C++ logic and never exercises JUCE's VST3<->
    SpeakerArrangement wire-format conversion a real host also depends on.

    Written to get ground truth on a "VST3 output stuck at Stereo,
    independent of DAW" report that survived two source-reading-only fixes.
    Each check loads a FRESH plugin instance (matching what a real host does
    -- instantiate once, then negotiate) so results from one check can't
    leak into the next via mutated shared instance state.

    Usage: vst3_bus_probe <path-to-Klangorbit.vst3>
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

using namespace juce;

static std::unique_ptr<AudioPluginInstance> loadFreshInstance (const String& path)
{
    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, path);
    if (found.isEmpty())
        return nullptr;

    String errorMessage;
    return std::unique_ptr<AudioPluginInstance> (
        format.createInstanceFromDescription (*found.getFirst(), 44100.0, 512, errorMessage));
}

// Mimics a host renegotiating ONLY the output bus: fetches the instance's
// OWN current layout, changes just the output entry, and resends the
// INPUT side completely UNTOUCHED -- the standard, spec-compliant way a
// host signals "leave this bus alone" (see setBusArrangements()'s own
// contract). This is the realistic case an Output Format dropdown change
// actually produces in a live host, NOT an artificial override.
static bool tryOutputOnlyChange (const String& path, const String& label, AudioChannelSet outSet)
{
    auto instance = loadFreshInstance (path);
    if (instance == nullptr) { std::cout << label << ": FAILED TO LOAD INSTANCE\n"; return false; }

    auto layout = instance->getBusesLayout();
    const auto originalIn = layout.getMainInputChannelSet();
    layout.outputBuses.getReference (0) = outSet;

    const bool supported = instance->checkBusesLayoutSupported (layout);
    const bool applied    = supported && instance->setBusesLayoutWithoutEnabling (layout);

    std::cout << label << " [" << outSet.size() << "ch, " << outSet.getDescription()
               << "] (input left at " << originalIn.getDescription() << ", " << originalIn.size() << "ch): "
              << "checkBusesLayoutSupported=" << (supported ? "TRUE" : "false")
              << "  setBusesLayoutWithoutEnabling=" << (applied ? "TRUE" : "false") << "\n";
    return applied;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: vst3_bus_probe <path-to-Klangorbit.vst3>\n";
        return 1;
    }

    const String path (argv[1]);

    {
        auto instance = loadFreshInstance (path);
        if (instance == nullptr)
        {
            std::cerr << "Failed to create instance at " << path << "\n";
            return 1;
        }

        std::cout << "Loaded: " << instance->getName() << "\n";
        const auto layout = instance->getBusesLayout();
        std::cout << "Construction-time default layout:\n";
        std::cout << "  input:  " << (layout.inputBuses.isEmpty() ? "(none)" : layout.getMainInputChannelSet().getDescription())
                   << " (" << layout.getMainInputChannelSet().size() << "ch)\n";
        std::cout << "  output: " << layout.getMainOutputChannelSet().getDescription()
                   << " (" << layout.getMainOutputChannelSet().size() << "ch)\n";

        std::cout << "\n-- No-op: exact current layout re-sent verbatim (sanity check) --\n";
        const bool noopOk = instance->checkBusesLayoutSupported (layout) && instance->setBusesLayoutWithoutEnabling (layout);
        std::cout << "no-op re-send: " << (noopOk ? "TRUE" : "FAILED (something more basic than widening is broken)") << "\n";
    }

    std::cout << "\n-- Realistic output-only renegotiation (fresh instance each time, input left untouched) --\n";
    tryOutputOnlyChange (path, "Quad",           AudioChannelSet::quadraphonic());
    tryOutputOnlyChange (path, "5.1",            AudioChannelSet::create5point1());
    tryOutputOnlyChange (path, "7.1",            AudioChannelSet::create7point1());
    tryOutputOnlyChange (path, "5.1.4 (Atmos)",  AudioChannelSet::create5point1point4());
    tryOutputOnlyChange (path, "Ambisonics O1",  AudioChannelSet::ambisonic (1));
    tryOutputOnlyChange (path, "Ambisonics O3",  AudioChannelSet::ambisonic (3));
    tryOutputOnlyChange (path, "Ambisonics O5",  AudioChannelSet::ambisonic (5));
    tryOutputOnlyChange (path, "Octophonic (discrete 8, no named layout exists)", AudioChannelSet::discreteChannels (8));

    return 0;
}
