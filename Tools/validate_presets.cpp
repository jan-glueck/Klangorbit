#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Loads every *.json file in Presets/factory/ via the real PresetManager
    code path (the same one the plugin GUI uses) and checks schemaVersion +
    field validation. Exit code != 0 on any failure -- as intended in
    Docs/WORKFLOW.md as preparation for CI.

    Usage: validate_presets <Presets/factory-directory>
*/
int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        juce::Logger::writeToLog ("Usage: validate_presets <presets-directory>");
        return 1;
    }

    juce::File dir (argv[1]);
    if (! dir.isDirectory())
    {
        juce::Logger::writeToLog ("Not a directory: " + dir.getFullPathName());
        return 1;
    }

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    if (files.isEmpty())
    {
        juce::Logger::writeToLog ("No presets found in " + dir.getFullPathName());
        return 0;
    }

    bool allOk = true;

    for (auto& file : files)
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        juce::String name;
        auto result = PresetManager::loadFile (file, engine, &name);

        if (result.wasOk())
        {
            juce::Logger::writeToLog ("OK   " + file.getFileName() + "  (\"" + name + "\")");
        }
        else
        {
            juce::Logger::writeToLog ("FAIL " + file.getFileName() + ": " + result.getErrorMessage());
            allOk = false;
        }
    }

    return allOk ? 0 : 1;
}
