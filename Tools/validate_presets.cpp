#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Laedt jede *.json-Datei in Presets/factory/ ueber den echten
    PresetManager-Codepfad (denselben, den auch die Plugin-GUI benutzt) und
    prueft schemaVersion + Feldvalidierung. Exit-Code != 0 bei jedem Fehler
    -- so wie in Docs/WORKFLOW.md als Vorbereitung fuer CI vorgesehen.

    Aufruf: validate_presets <Presets/factory-Verzeichnis>
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
        juce::Logger::writeToLog ("Kein Verzeichnis: " + dir.getFullPathName());
        return 1;
    }

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    if (files.isEmpty())
    {
        juce::Logger::writeToLog ("Keine Presets gefunden in " + dir.getFullPathName());
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
