#include "PresetManager.h"

namespace
{
    juce::var vecToVar (Vec3 v)
    {
        juce::Array<juce::var> a;
        a.add (v.x);
        a.add (v.y);
        a.add (v.z);
        return juce::var (a);
    }

    bool varToVec (const juce::var& v, Vec3& out)
    {
        auto* arr = v.getArray();
        if (arr == nullptr || arr->size() != 3)
            return false;

        out.x = (float) (*arr)[0];
        out.y = (float) (*arr)[1];
        out.z = (float) (*arr)[2];
        return true;
    }

    juce::String modeToString (SoundObject::Mode m)
    {
        switch (m)
        {
            case SoundObject::Mode::Static:    return "static";
            case SoundObject::Mode::Manual:    return "manual";
            case SoundObject::Mode::Orbit:     return "orbit";
            case SoundObject::Mode::Impulse:   return "impulse";
            case SoundObject::Mode::Attracted: return "attracted";
        }
        return "static";
    }

    bool modeFromString (const juce::String& s, SoundObject::Mode& out)
    {
        if (s == "static")    { out = SoundObject::Mode::Static;    return true; }
        if (s == "manual")    { out = SoundObject::Mode::Manual;    return true; }
        if (s == "orbit")     { out = SoundObject::Mode::Orbit;     return true; }
        if (s == "impulse")   { out = SoundObject::Mode::Impulse;   return true; }
        if (s == "attracted") { out = SoundObject::Mode::Attracted; return true; }
        return false;
    }

    juce::String boundaryBehaviorToString (SceneSettings::BoundaryBehavior b)
    {
        switch (b)
        {
            case SceneSettings::BoundaryBehavior::Reflect: return "reflect";
            case SceneSettings::BoundaryBehavior::Wrap:    return "wrap";
            case SceneSettings::BoundaryBehavior::Absorb:  return "absorb";
        }
        return "reflect";
    }

    bool boundaryBehaviorFromString (const juce::String& s, SceneSettings::BoundaryBehavior& out)
    {
        if (s == "reflect") { out = SceneSettings::BoundaryBehavior::Reflect; return true; }
        if (s == "wrap")    { out = SceneSettings::BoundaryBehavior::Wrap;    return true; }
        if (s == "absorb")  { out = SceneSettings::BoundaryBehavior::Absorb;  return true; }
        return false;
    }

    juce::var sceneSettingsToVar (const SceneSettings& s)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("roomSize", (double) s.roomSize);
        obj->setProperty ("boundaryBehavior", boundaryBehaviorToString (s.boundaryBehavior));
        obj->setProperty ("globalField", vecToVar (s.globalField));
        obj->setProperty ("timeScale", (double) s.timeScale);
        return juce::var (obj);
    }

    // "scene" ist optional -- fehlt es komplett, bleiben die SceneSettings-Defaults
    // unveraendert (wichtig fuer Abwaertskompatibilitaet mit Presets ohne diesen Block).
    juce::Result sceneSettingsFromVar (const juce::var& sceneVar, SceneSettings& out)
    {
        if (sceneVar.isVoid())
            return juce::Result::ok();

        if (! sceneVar.isObject())
            return juce::Result::fail ("'scene' ist kein JSON-Objekt.");

        out.roomSize = (float) sceneVar.getProperty ("roomSize", (double) out.roomSize);

        if (sceneVar.hasProperty ("boundaryBehavior"))
        {
            const auto s = sceneVar.getProperty ("boundaryBehavior", juce::var()).toString();
            if (! boundaryBehaviorFromString (s, out.boundaryBehavior))
                return juce::Result::fail ("'scene.boundaryBehavior': unbekannter Wert '" + s + "'.");
        }

        if (sceneVar.hasProperty ("globalField"))
        {
            if (! varToVec (sceneVar.getProperty ("globalField", juce::var()), out.globalField))
                return juce::Result::fail ("'scene.globalField' ist kein 3er-Array.");
        }

        out.timeScale = (float) sceneVar.getProperty ("timeScale", (double) out.timeScale);

        return juce::Result::ok();
    }
}

juce::var PresetManager::sceneToVar (TrajectoryEngine& engine, const juce::String& name)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", currentSchemaVersion);
    root->setProperty ("name", name);
    root->setProperty ("scene", sceneSettingsToVar (engine.getSceneSettings()));

    juce::Array<juce::var> objectsArray;
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0)
            continue; // inaktive Objekte sind nicht Teil der gespeicherten Szene

        auto* objVar = new juce::DynamicObject();
        objVar->setProperty ("id", obj.id);
        objVar->setProperty ("inputChannel", obj.inputChannel);
        objVar->setProperty ("position", vecToVar (obj.position));
        objVar->setProperty ("mode", modeToString (obj.mode));
        objVar->setProperty ("orbitCenter", vecToVar (obj.orbitCenter));
        objVar->setProperty ("orbitRadius", (double) obj.orbitRadius);
        objVar->setProperty ("orbitAngularSpeed", (double) obj.orbitAngularSpeed);
        objVar->setProperty ("attractionStrength", (double) obj.attractionStrength);
        objVar->setProperty ("mass", (double) obj.mass);
        objVar->setProperty ("damping", (double) obj.damping);
        objVar->setProperty ("gain", (double) obj.gain);

        // Traegheit/Bewegungsgrenzen (optionale Felder, siehe Presets/schema/README.md)
        objVar->setProperty ("maxVelocity", (double) obj.maxVelocity);
        objVar->setProperty ("dragCoefficient", (double) obj.dragCoefficient);
        objVar->setProperty ("restitution", (double) obj.restitution);
        objVar->setProperty ("velocitySnapThreshold", (double) obj.velocitySnapThreshold);

        // n-Body-Verfeinerung (optional)
        objVar->setProperty ("forceExponent", (double) obj.forceExponent);
        objVar->setProperty ("minDistance", (double) obj.minDistance);
        objVar->setProperty ("maxRange", (double) obj.maxRange);
        objVar->setProperty ("attractionPulseRate", (double) obj.attractionPulseRate);
        objVar->setProperty ("attractionPulseDepth", (double) obj.attractionPulseDepth);

        // Orbit-Erweiterungen (optional)
        objVar->setProperty ("orbitPlaneNormal", vecToVar (obj.orbitPlaneNormal));
        objVar->setProperty ("orbitEccentricity", (double) obj.orbitEccentricity);
        objVar->setProperty ("orbitDecay", (double) obj.orbitDecay);
        objVar->setProperty ("orbitReferenceObjectId", obj.orbitReferenceObjectId);

        objectsArray.add (juce::var (objVar));
    }

    root->setProperty ("objects", objectsArray);
    return juce::var (root);
}

juce::Result PresetManager::loadFromVar (const juce::var& root, TrajectoryEngine& engine)
{
    if (! root.isObject())
        return juce::Result::fail ("Preset ist kein gueltiges JSON-Objekt.");

    auto schemaVersionVar = root.getProperty ("schemaVersion", juce::var());
    if (! (schemaVersionVar.isInt() || schemaVersionVar.isDouble() || schemaVersionVar.isInt64()))
        return juce::Result::fail ("Preset hat kein numerisches 'schemaVersion'-Feld.");

    const int schemaVersion = (int) schemaVersionVar;
    if (schemaVersion != currentSchemaVersion)
        return juce::Result::fail ("Preset-schemaVersion " + juce::String (schemaVersion)
                                    + " wird nicht unterstuetzt (unterstuetzt: " + juce::String (currentSchemaVersion)
                                    + "). Eine Migration fuer diese Version ist noch nicht implementiert.");

    auto* objectsArray = root.getProperty ("objects", juce::var()).getArray();
    if (objectsArray == nullptr)
        return juce::Result::fail ("Preset hat kein 'objects'-Array.");

    // Erst alle Objekte auf einen sauberen, inaktiven Ausgangszustand
    // zuruecksetzen -- ein Preset ist eine vollstaendige Szene, kein Diff
    // zu dem, was vorher geladen war.
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        const int id = engine.getObject (i).id;
        engine.getObject (i) = SoundObject { id };
    }

    // Szene-weite Parameter -- fehlt der Block, bleiben die Defaults aus
    // SceneSettings unveraendert (Abwaertskompatibilitaet).
    SceneSettings sceneSettings;
    auto sceneResult = sceneSettingsFromVar (root.getProperty ("scene", juce::var()), sceneSettings);
    if (sceneResult.failed())
        return sceneResult;
    engine.getSceneSettings() = sceneSettings;

    for (auto& element : *objectsArray)
    {
        if (! element.isObject())
            return juce::Result::fail ("Preset enthaelt ein Element in 'objects', das kein JSON-Objekt ist.");

        if (! element.hasProperty ("id"))
            return juce::Result::fail ("Preset-Objekt ohne 'id'-Feld.");

        const int id = (int) element.getProperty ("id", -1);
        if (! juce::isPositiveAndBelow (id, engine.getNumObjects()))
            return juce::Result::fail ("Preset-Objekt-id " + juce::String (id) + " liegt ausserhalb des gueltigen Bereichs (0.."
                                        + juce::String (engine.getNumObjects() - 1) + ").");

        auto& obj = engine.getObject (id);
        obj.id = id;
        obj.inputChannel = (int) element.getProperty ("inputChannel", -1);

        if (! varToVec (element.getProperty ("position", juce::var()), obj.position))
            return juce::Result::fail ("Preset-Objekt " + juce::String (id) + ": 'position' fehlt oder ist kein 3er-Array.");

        const auto modeStr = element.getProperty ("mode", juce::var()).toString();
        if (! modeFromString (modeStr, obj.mode))
            return juce::Result::fail ("Preset-Objekt " + juce::String (id) + ": unbekannter mode '" + modeStr + "'.");

        varToVec (element.getProperty ("orbitCenter", juce::var()), obj.orbitCenter); // optional, Default bleibt {0,0,0}
        obj.orbitRadius        = (float) element.getProperty ("orbitRadius", (double) obj.orbitRadius);
        obj.orbitAngularSpeed  = (float) element.getProperty ("orbitAngularSpeed", (double) obj.orbitAngularSpeed);
        obj.attractionStrength = (float) element.getProperty ("attractionStrength", (double) obj.attractionStrength);
        obj.mass                = (float) element.getProperty ("mass", (double) obj.mass);
        obj.damping              = (float) element.getProperty ("damping", (double) obj.damping);
        obj.gain                 = (float) element.getProperty ("gain", (double) obj.gain);

        // Traegheit/Bewegungsgrenzen (optional, Default aus SoundObject{})
        obj.maxVelocity           = (float) element.getProperty ("maxVelocity", (double) obj.maxVelocity);
        obj.dragCoefficient       = (float) element.getProperty ("dragCoefficient", (double) obj.dragCoefficient);
        obj.restitution           = (float) element.getProperty ("restitution", (double) obj.restitution);
        obj.velocitySnapThreshold = (float) element.getProperty ("velocitySnapThreshold", (double) obj.velocitySnapThreshold);

        // n-Body-Verfeinerung (optional)
        obj.forceExponent         = (float) element.getProperty ("forceExponent", (double) obj.forceExponent);
        obj.minDistance           = (float) element.getProperty ("minDistance", (double) obj.minDistance);
        obj.maxRange              = (float) element.getProperty ("maxRange", (double) obj.maxRange);
        obj.attractionPulseRate   = (float) element.getProperty ("attractionPulseRate", (double) obj.attractionPulseRate);
        obj.attractionPulseDepth  = (float) element.getProperty ("attractionPulseDepth", (double) obj.attractionPulseDepth);

        // Orbit-Erweiterungen (optional)
        varToVec (element.getProperty ("orbitPlaneNormal", juce::var()), obj.orbitPlaneNormal); // Default {0,0,1} bleibt bei Fehlen
        obj.orbitEccentricity     = (float) element.getProperty ("orbitEccentricity", (double) obj.orbitEccentricity);
        obj.orbitDecay            = (float) element.getProperty ("orbitDecay", (double) obj.orbitDecay);
        obj.orbitReferenceObjectId = (int) element.getProperty ("orbitReferenceObjectId", obj.orbitReferenceObjectId);

        // Nicht Teil des Schemas (Laufzeitzustand, kein Startparameter):
        // sauber auf Null, damit ein frisch geladenes Orbit-/Puls-Objekt
        // nicht mit der Phase/Geschwindigkeit eines vorherigen Zustands
        // weiterlaeuft.
        obj.orbitPhase = 0.0f;
        obj.attractionPulsePhase = 0.0f;
        obj.velocity = {};
    }

    return juce::Result::ok();
}

juce::Result PresetManager::loadFile (const juce::File& file, TrajectoryEngine& engine, juce::String* outName)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("Datei nicht gefunden: " + file.getFullPathName());

    auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("Datei enthaelt kein gueltiges JSON: " + file.getFullPathName());

    auto result = loadFromVar (parsed, engine);
    if (result.wasOk() && outName != nullptr)
        *outName = parsed.getProperty ("name", file.getFileNameWithoutExtension()).toString();

    return result;
}

juce::Result PresetManager::saveFile (const juce::File& file, TrajectoryEngine& engine, const juce::String& name)
{
    auto sceneVar = sceneToVar (engine, name);
    auto jsonText = juce::JSON::toString (sceneVar);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Konnte Zielordner nicht anlegen: " + file.getParentDirectory().getFullPathName());

    if (! file.replaceWithText (jsonText))
        return juce::Result::fail ("Konnte Datei nicht schreiben: " + file.getFullPathName());

    return juce::Result::ok();
}
