#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ParameterPanel.h"

/**
    2D-Draufsicht (x/y-Ebene von oben, z nicht dargestellt -- Erweiterung
    auf echte 3D-Ansicht spaeter, gleiche Datenbasis ueber TrajectoryEngine).

    - Linksklick auf ein Objekt: auswaehlen (Parameter erscheinen im Panel rechts)
    - Linksklick+Ziehen auf ein Objekt: manuelle Bewegung (Mode::Manual)
    - Loslassen mit Schwung: Impuls (Mode::Impulse) -- einfache Wurf-Geste
    - Doppelklick auf ein Objekt: Orbit um Ursprung starten/stoppen (Demo)
    - Klick auf leere Flaeche: Auswahl aufheben
    - Toolbar oben: Preset laden/speichern (PresetManager), Objekt
      hinzufuegen/entfernen
    - Panel rechts: alle Parameter des ausgewaehlten Objekts + Szene-weite
      Parameter (Raumgrenze, globales Feld, Zeitraffer), siehe ParameterPanel

    Die eigentliche Physik-Aktualisierung laeuft ueber einen juce::Timer,
    der TrajectoryEngine::update() mit der gemessenen Zeit seit dem letzten
    Tick aufruft -- das ist die Control-Rate-Schleife, getrennt vom
    Audio-Thread.
*/
class SpatialAudioPOCEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit SpatialAudioPOCEditor (SpatialAudioPOCProcessor&);
    ~SpatialAudioPOCEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void loadPresetClicked();
    void savePresetClicked();
    void showPresetError (const juce::String& title, const juce::String& message);

    void addObjectClicked();
    void removeObjectClicked();
    void selectObject (int index); // -1 = Auswahl aufheben
    void updateObjectUiState();    // Objektzahl-Label + Button-Enablement

    // Bildschirm- <-> Raumkoordinaten (metrische x/y-Ebene, 1m = pixelsPerMeter),
    // relativ zu viewArea (Fenster ohne Toolbar-Streifen oben, ohne Panel rechts).
    juce::Point<float> objectToScreen (Vec3 pos) const;
    Vec3 screenToObject (juce::Point<float> screenPos) const;
    int findObjectNear (juce::Point<float> screenPos) const;

    // Nicht "processor" -- der Name ist bereits (als Basisklassenreferenz auf
    // die Basisklasse juce::AudioProcessor) in AudioProcessorEditor vergeben.
    SpatialAudioPOCProcessor& audioProcessor;

    int draggedObjectIndex = -1;
    int selectedObjectIndex = -1;
    // Wird erst true, sobald eine Bewegung ueber mouseDown hinaus stattfindet
    // (siehe mouseDrag) -- unterscheidet einen reinen Selektions-Klick von
    // echtem Ziehen, damit Klicken ein laufendes Orbit/Impulse nicht stoppt.
    bool physicsDragActive = false;
    juce::Point<float> lastDragScreenPos;
    juce::int64 lastDragTimeMs = 0;
    Vec3 estimatedDragVelocity;

    juce::int64 lastTimerMs = 0;
    static constexpr float pixelsPerMeter = 80.0f;
    static constexpr float hitRadiusPixels = 16.0f;

    static constexpr int toolbarHeight = 64; // zwei Reihen a 32px
    static constexpr int parameterPanelWidth = 340;
    juce::Rectangle<int> viewArea;

    juce::TextButton loadPresetButton { "Preset laden..." };
    juce::TextButton savePresetButton { "Preset speichern..." };
    juce::Label presetStatusLabel;
    juce::String currentPresetName { "(kein Preset geladen)" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton addObjectButton { "+ Objekt" };
    juce::TextButton removeObjectButton { "- Objekt entfernen" };
    juce::Label objectCountLabel;

    ParameterPanel parameterPanel;
};
