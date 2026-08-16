#pragma once
#include <juce_core/juce_core.h>
#include "TrajectoryEngine.h"

/**
    Liest/schreibt Szenen im Preset-JSON-Format, siehe Presets/schema/README.md
    fuer die Feldreferenz.

    schemaVersion wird geprueft: ein nicht unterstuetztes schemaVersion wird
    mit einer klaren Fehlermeldung abgelehnt statt stillschweigend falsch
    interpretiert zu werden (bewusste Vorgabe aus Presets/schema/README.md).
    Sobald es eine schemaVersion 2 gibt, kommt hier eine migrateSchemaV1toV2()-
    artige Funktion dazu.

    Laden ERSETZT die komplette Szene: Objekte, die im Preset nicht
    vorkommen, werden auf einen inaktiven Ausgangszustand zurueckgesetzt --
    ein Preset beschreibt eine vollstaendige Konfiguration, kein Diff zu dem,
    was vorher geladen war.

    Nur vom Message-Thread aus aufrufen (wie TrajectoryEngine::getObject()).
*/
namespace PresetManager
{
    constexpr int currentSchemaVersion = 1;

    // Baut ein var-Objekt aus dem aktuellen Szenenzustand (schemaVersion 1).
    // Inaktive Objekte (inputChannel < 0) werden nicht mit gespeichert.
    juce::var sceneToVar (TrajectoryEngine& engine, const juce::String& name);

    // Wendet ein geparstes Preset-var auf die Engine an (ersetzt die Szene).
    juce::Result loadFromVar (const juce::var& root, TrajectoryEngine& engine);

    // outName wird bei Erfolg auf das "name"-Feld des Presets gesetzt (Fallback: Dateiname).
    juce::Result loadFile (const juce::File& file, TrajectoryEngine& engine, juce::String* outName = nullptr);
    juce::Result saveFile (const juce::File& file, TrajectoryEngine& engine, const juce::String& name);
}
