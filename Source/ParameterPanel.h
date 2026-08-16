#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"

/**
    Ein Zeilen-Widget: Label + ein Slider, mit generischem onValueChanged-
    Callback. Kennt weder SoundObject noch SceneSettings -- das Binding an
    ein konkretes Feld passiert ausschliesslich in ParameterPanel.
*/
class FloatRowComponent : public juce::Component
{
public:
    FloatRowComponent (const juce::String& name, double min, double max, double step);

    void resized() override;
    void setValueQuiet (float v);

    static constexpr int preferredHeight = 36;

    juce::Label label;
    juce::Slider slider;
    std::function<void (float)> onValueChanged;
};

/** Wie FloatRowComponent, aber fuer Vec3 (drei uebereinander gestapelte Achsen-Slider). */
class Vec3RowComponent : public juce::Component
{
public:
    Vec3RowComponent (const juce::String& name, double min, double max, double step);

    void resized() override;
    void setValueQuiet (Vec3 v);

    static constexpr int preferredHeight = 16 + 3 * 20;

    juce::Label label, xLabel, yLabel, zLabel;
    juce::Slider xSlider, ySlider, zSlider;
    std::function<void (Vec3)> onValueChanged;

private:
    void setupAxis (juce::Label& l, juce::Slider& s, const juce::String& axisName,
                    double min, double max, double step);
};

/** Label + ComboBox, mit generischem onSelected(itemIndex)-Callback. */
class ComboRowComponent : public juce::Component
{
public:
    explicit ComboRowComponent (const juce::String& name);

    void resized() override;

    static constexpr int preferredHeight = 38;

    juce::Label label;
    juce::ComboBox combo;
    std::function<void (int)> onSelected;
};

/**
    Scrollbares Seitenpanel: Szene-Parameter (immer sichtbar/editierbar) und
    die Parameter des aktuell in der 2D-Ansicht ausgewaehlten Objekts.

    Schreibt direkt auf die uebergebenen SoundObject- und SceneSettings-
    Pointer -- dieselben Zugriffsregeln wie ueberall sonst in der GUI
    (Message-Thread).

    refreshFromModel() synchronisiert die Controls mit dem aktuellen
    Modellzustand. Wird bewusst NICHT per Timer aufgerufen, sondern nur bei
    Selektionswechsel und nach Preset-Laden -- sonst wuerde ein Slider
    waehrend des Ziehens durch die laufende Physik/Snapshot-Updates
    "zurueckspringen".
*/
class ParameterPanel : public juce::Component
{
public:
    ParameterPanel();

    // Einmalig vom Editor gesetzt (nie wieder null).
    void setSceneSettings (SceneSettings* settings);

    // nullptr = keine Auswahl. numObjects fuer die Referenzobjekt-Combo (Orbit).
    void setEditedObject (SoundObject* obj, int objectIndexForHeader, int numObjects);

    // Alle Controls mit dem aktuellen Modellzustand synchronisieren.
    void refreshFromModel();

    void resized() override;

private:
    FloatRowComponent& addSceneFloatRow (const juce::String& name, float SceneSettings::* member,
                                          double min, double max, double step);
    FloatRowComponent& addObjectFloatRow (const juce::String& name, float SoundObject::* member,
                                           double min, double max, double step);
    Vec3RowComponent& addObjectVec3Row (const juce::String& name, Vec3 SoundObject::* member,
                                         double min, double max, double step);
    juce::Label& addHeader (const juce::String& text);
    void addToLayout (juce::Component& c, int height);

    void rebuildOrbitReferenceItems (int numObjects, int selfId);

    juce::Viewport viewport;
    juce::Component content;

    SceneSettings* sceneSettings = nullptr;
    SoundObject* editedObject = nullptr;

    std::vector<juce::Component*> layoutOrder;
    std::vector<int> layoutHeights;

    struct SceneFloatBinding { std::unique_ptr<FloatRowComponent> row; float SceneSettings::* member; };
    std::vector<SceneFloatBinding> sceneFloatRows;

    struct ObjectFloatBinding { std::unique_ptr<FloatRowComponent> row; float SoundObject::* member; };
    std::vector<ObjectFloatBinding> objectFloatRows;

    struct ObjectVec3Binding { std::unique_ptr<Vec3RowComponent> row; Vec3 SoundObject::* member; };
    std::vector<ObjectVec3Binding> objectVec3Rows;

    std::vector<std::unique_ptr<juce::Label>> headers;

    juce::Label objectHeaderLabel;
    std::unique_ptr<ComboRowComponent> modeRow;
    std::unique_ptr<ComboRowComponent> boundaryRow;
    std::unique_ptr<Vec3RowComponent> globalFieldRow; // SceneSettings::globalField, kein SoundObject-Feld -> eigenes Binding
    std::unique_ptr<ComboRowComponent> orbitRefRow;

    // Alles, was nur Sinn ergibt, wenn ein Objekt ausgewaehlt ist -- wird
    // in setEditedObject() enabled/disabled.
    std::vector<juce::Component*> objectOnlyComponents;
};
