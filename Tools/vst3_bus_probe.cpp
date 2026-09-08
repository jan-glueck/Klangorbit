/*
    Loads a built Klangorbit.vst3 the way a real VST3 host would -- via
    JUCE's own VST3PluginFormat, going through the ACTUAL VST3 ABI
    (IAudioProcessor::setBusArrangements()/getBusArrangement() calls into the
    real compiled plugin binary) -- rather than calling
    KlangorbitProcessor::isBusesLayoutSupported() directly in-process, which
    only tests our OWN C++ logic and never exercises JUCE's VST3<->
    SpeakerArrangement wire-format conversion a real host also depends on.

    Written during this project's "VST3 output stuck at Stereo" saga (see
    CHANGELOG.md) to get ground truth rather than more source-reading
    speculation. What it verifies now, after landing on the final
    architecture (one fixed 36-channel output bus for VST3/Standalone,
    never renegotiated -- see PluginProcessor.h's own class comment and
    isBusesLayoutSupported()'s comment): the plugin's construction-time
    default IS that fixed layout, a verbatim re-send of it succeeds (a
    basic sanity check), and every OTHER candidate a host might try is
    correctly rejected -- confirming a host has exactly one option to
    settle on, matching how real fixed-wide-bus Ambisonics VST3 plugins
    (e.g. IEM Suite) behave regardless of the host's own track/bus size.

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

// Mimics a host trying to negotiate a DIFFERENT output layout than the
// plugin's own fixed default, with the input bus left completely
// untouched -- the standard, spec-compliant way a host signals "leave
// this bus alone" (see setBusArrangements()'s own contract). Every one of
// these should now fail (the plugin only ever accepts its own one fixed
// 36-channel layout) -- this is a regression test for "did the fixed-bus
// model actually stick," not a search for a working wider layout the way
// it was during the earlier multi-candidate architecture.
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
    int failures = 0;

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

        const auto expectedOutput = AudioChannelSet::ambisonic (5);
        if (layout.getMainOutputChannelSet() != expectedOutput)
        {
            std::cout << "FAIL: default output is not the expected fixed 36ch ambisonic(5) layout\n";
            ++failures;
        }

        std::cout << "\n-- No-op: exact current layout re-sent verbatim (sanity check) --\n";
        const bool noopOk = instance->checkBusesLayoutSupported (layout) && instance->setBusesLayoutWithoutEnabling (layout);
        std::cout << "no-op re-send: " << (noopOk ? "TRUE" : "FAILED (something more basic than the fixed-bus model is broken)") << "\n";
        if (! noopOk) ++failures;
    }

    std::cout << "\n-- Regression check: every OTHER candidate must now be REJECTED (fixed-bus model) --\n";
    struct Candidate { const char* label; AudioChannelSet set; };
    const Candidate candidates[] = {
        { "Quad",          AudioChannelSet::quadraphonic() },
        { "5.1",           AudioChannelSet::create5point1() },
        { "7.1",           AudioChannelSet::create7point1() },
        { "5.1.4 (Atmos)", AudioChannelSet::create5point1point4() },
        { "Ambisonics O1", AudioChannelSet::ambisonic (1) },
        { "Ambisonics O3", AudioChannelSet::ambisonic (3) },
        { "Octophonic (discrete 8)", AudioChannelSet::discreteChannels (8) },
    };
    for (const auto& c : candidates)
        if (tryOutputOnlyChange (path, c.label, c.set))
        {
            std::cout << "FAIL: " << c.label << " was unexpectedly accepted -- fixed-bus model not enforced\n";
            ++failures;
        }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
