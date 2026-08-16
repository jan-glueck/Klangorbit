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

    juce::String directivityPatternToString (SoundObject::DirectivityPattern p)
    {
        switch (p)
        {
            case SoundObject::DirectivityPattern::Omni:     return "omni";
            case SoundObject::DirectivityPattern::Cardioid: return "cardioid";
            case SoundObject::DirectivityPattern::Figure8:  return "figure8";
        }
        return "omni";
    }

    bool directivityPatternFromString (const juce::String& s, SoundObject::DirectivityPattern& out)
    {
        if (s == "omni")     { out = SoundObject::DirectivityPattern::Omni;     return true; }
        if (s == "cardioid") { out = SoundObject::DirectivityPattern::Cardioid; return true; }
        if (s == "figure8")  { out = SoundObject::DirectivityPattern::Figure8;  return true; }
        return false;
    }

    juce::var sceneSettingsToVar (const SceneSettings& s)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("roomSize", (double) s.roomSize);
        obj->setProperty ("boundaryBehavior", boundaryBehaviorToString (s.boundaryBehavior));
        obj->setProperty ("globalField", vecToVar (s.globalField));
        obj->setProperty ("timeScale", (double) s.timeScale);

        // Acoustic propagation (medium properties, see SceneSettings.h)
        obj->setProperty ("speedOfSound", (double) s.speedOfSound);
        obj->setProperty ("temperature", (double) s.temperature);
        obj->setProperty ("relativeHumidity", (double) s.relativeHumidity);
        obj->setProperty ("atmosphericPressure", (double) s.atmosphericPressure);
        obj->setProperty ("windVector", vecToVar (s.windVector));

        return juce::var (obj);
    }

    // "scene" is optional -- if missing entirely, the SceneSettings defaults
    // stay unchanged (important for backward compatibility with presets that
    // don't have this block).
    juce::Result sceneSettingsFromVar (const juce::var& sceneVar, SceneSettings& out)
    {
        if (sceneVar.isVoid())
            return juce::Result::ok();

        if (! sceneVar.isObject())
            return juce::Result::fail ("'scene' is not a JSON object.");

        out.roomSize = (float) sceneVar.getProperty ("roomSize", (double) out.roomSize);

        if (sceneVar.hasProperty ("boundaryBehavior"))
        {
            const auto s = sceneVar.getProperty ("boundaryBehavior", juce::var()).toString();
            if (! boundaryBehaviorFromString (s, out.boundaryBehavior))
                return juce::Result::fail ("'scene.boundaryBehavior': unknown value '" + s + "'.");
        }

        if (sceneVar.hasProperty ("globalField"))
        {
            if (! varToVec (sceneVar.getProperty ("globalField", juce::var()), out.globalField))
                return juce::Result::fail ("'scene.globalField' is not a 3-element array.");
        }

        out.timeScale = (float) sceneVar.getProperty ("timeScale", (double) out.timeScale);

        // Acoustic propagation (optional, defaults from SceneSettings{})
        out.speedOfSound        = (float) sceneVar.getProperty ("speedOfSound", (double) out.speedOfSound);
        out.temperature          = (float) sceneVar.getProperty ("temperature", (double) out.temperature);
        out.relativeHumidity     = (float) sceneVar.getProperty ("relativeHumidity", (double) out.relativeHumidity);
        out.atmosphericPressure = (float) sceneVar.getProperty ("atmosphericPressure", (double) out.atmosphericPressure);

        if (sceneVar.hasProperty ("windVector"))
        {
            if (! varToVec (sceneVar.getProperty ("windVector", juce::var()), out.windVector))
                return juce::Result::fail ("'scene.windVector' is not a 3-element array.");
        }

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
            continue; // inactive objects are not part of the saved scene

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

        // Inertia/motion limits (optional fields, see Presets/schema/README.md)
        objVar->setProperty ("maxVelocity", (double) obj.maxVelocity);
        objVar->setProperty ("dragCoefficient", (double) obj.dragCoefficient);
        objVar->setProperty ("restitution", (double) obj.restitution);
        objVar->setProperty ("velocitySnapThreshold", (double) obj.velocitySnapThreshold);

        // n-body refinement (optional)
        objVar->setProperty ("forceExponent", (double) obj.forceExponent);
        objVar->setProperty ("minDistance", (double) obj.minDistance);
        objVar->setProperty ("maxRange", (double) obj.maxRange);
        objVar->setProperty ("attractionPulseRate", (double) obj.attractionPulseRate);
        objVar->setProperty ("attractionPulseDepth", (double) obj.attractionPulseDepth);

        // Orbit extensions (optional)
        objVar->setProperty ("orbitPlaneNormal", vecToVar (obj.orbitPlaneNormal));
        objVar->setProperty ("orbitEccentricity", (double) obj.orbitEccentricity);
        objVar->setProperty ("orbitDecay", (double) obj.orbitDecay);
        objVar->setProperty ("orbitReferenceObjectId", obj.orbitReferenceObjectId);

        // Acoustic propagation (optional, see SoundObject.h)
        objVar->setProperty ("dopplerFactor", (double) obj.dopplerFactor);
        objVar->setProperty ("dopplerSmoothing", (double) obj.dopplerSmoothing);
        objVar->setProperty ("directivityPattern", directivityPatternToString (obj.directivityPattern));
        objVar->setProperty ("sourceOrientation", vecToVar (obj.sourceOrientation));

        objectsArray.add (juce::var (objVar));
    }

    root->setProperty ("objects", objectsArray);
    return juce::var (root);
}

juce::Result PresetManager::loadFromVar (const juce::var& root, TrajectoryEngine& engine)
{
    if (! root.isObject())
        return juce::Result::fail ("Preset is not a valid JSON object.");

    auto schemaVersionVar = root.getProperty ("schemaVersion", juce::var());
    if (! (schemaVersionVar.isInt() || schemaVersionVar.isDouble() || schemaVersionVar.isInt64()))
        return juce::Result::fail ("Preset has no numeric 'schemaVersion' field.");

    const int schemaVersion = (int) schemaVersionVar;
    if (schemaVersion != currentSchemaVersion)
        return juce::Result::fail ("Preset schemaVersion " + juce::String (schemaVersion)
                                    + " is not supported (supported: " + juce::String (currentSchemaVersion)
                                    + "). Migration for this version is not implemented yet.");

    auto* objectsArray = root.getProperty ("objects", juce::var()).getArray();
    if (objectsArray == nullptr)
        return juce::Result::fail ("Preset has no 'objects' array.");

    // First reset all objects to a clean, inactive starting state -- a
    // preset is a complete scene, not a diff against what was loaded
    // before.
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        const int id = engine.getObject (i).id;
        engine.getObject (i) = SoundObject { id };
    }

    // Scene-wide parameters -- if the block is missing, the SceneSettings
    // defaults stay unchanged (backward compatibility).
    SceneSettings sceneSettings;
    auto sceneResult = sceneSettingsFromVar (root.getProperty ("scene", juce::var()), sceneSettings);
    if (sceneResult.failed())
        return sceneResult;
    engine.getSceneSettings() = sceneSettings;

    for (auto& element : *objectsArray)
    {
        if (! element.isObject())
            return juce::Result::fail ("Preset contains an element in 'objects' that is not a JSON object.");

        if (! element.hasProperty ("id"))
            return juce::Result::fail ("Preset object without an 'id' field.");

        const int id = (int) element.getProperty ("id", -1);
        if (! juce::isPositiveAndBelow (id, engine.getNumObjects()))
            return juce::Result::fail ("Preset object id " + juce::String (id) + " is out of the valid range (0.."
                                        + juce::String (engine.getNumObjects() - 1) + ").");

        auto& obj = engine.getObject (id);
        obj.id = id;
        obj.inputChannel = (int) element.getProperty ("inputChannel", -1);

        if (! varToVec (element.getProperty ("position", juce::var()), obj.position))
            return juce::Result::fail ("Preset object " + juce::String (id) + ": 'position' is missing or not a 3-element array.");

        const auto modeStr = element.getProperty ("mode", juce::var()).toString();
        if (! modeFromString (modeStr, obj.mode))
            return juce::Result::fail ("Preset object " + juce::String (id) + ": unknown mode '" + modeStr + "'.");

        varToVec (element.getProperty ("orbitCenter", juce::var()), obj.orbitCenter); // optional, default stays {0,0,0}
        obj.orbitRadius        = (float) element.getProperty ("orbitRadius", (double) obj.orbitRadius);
        obj.orbitAngularSpeed  = (float) element.getProperty ("orbitAngularSpeed", (double) obj.orbitAngularSpeed);
        obj.attractionStrength = (float) element.getProperty ("attractionStrength", (double) obj.attractionStrength);
        obj.mass                = (float) element.getProperty ("mass", (double) obj.mass);
        obj.damping              = (float) element.getProperty ("damping", (double) obj.damping);
        obj.gain                 = (float) element.getProperty ("gain", (double) obj.gain);

        // Inertia/motion limits (optional, default from SoundObject{})
        obj.maxVelocity           = (float) element.getProperty ("maxVelocity", (double) obj.maxVelocity);
        obj.dragCoefficient       = (float) element.getProperty ("dragCoefficient", (double) obj.dragCoefficient);
        obj.restitution           = (float) element.getProperty ("restitution", (double) obj.restitution);
        obj.velocitySnapThreshold = (float) element.getProperty ("velocitySnapThreshold", (double) obj.velocitySnapThreshold);

        // n-body refinement (optional)
        obj.forceExponent         = (float) element.getProperty ("forceExponent", (double) obj.forceExponent);
        obj.minDistance           = (float) element.getProperty ("minDistance", (double) obj.minDistance);
        obj.maxRange              = (float) element.getProperty ("maxRange", (double) obj.maxRange);
        obj.attractionPulseRate   = (float) element.getProperty ("attractionPulseRate", (double) obj.attractionPulseRate);
        obj.attractionPulseDepth  = (float) element.getProperty ("attractionPulseDepth", (double) obj.attractionPulseDepth);

        // Orbit extensions (optional)
        varToVec (element.getProperty ("orbitPlaneNormal", juce::var()), obj.orbitPlaneNormal); // default {0,0,1} stays if missing
        obj.orbitEccentricity     = (float) element.getProperty ("orbitEccentricity", (double) obj.orbitEccentricity);
        obj.orbitDecay            = (float) element.getProperty ("orbitDecay", (double) obj.orbitDecay);
        obj.orbitReferenceObjectId = (int) element.getProperty ("orbitReferenceObjectId", obj.orbitReferenceObjectId);

        // Acoustic propagation (optional, default from SoundObject{})
        obj.dopplerFactor    = (float) element.getProperty ("dopplerFactor", (double) obj.dopplerFactor);
        obj.dopplerSmoothing = (float) element.getProperty ("dopplerSmoothing", (double) obj.dopplerSmoothing);

        if (element.hasProperty ("directivityPattern"))
        {
            const auto patternStr = element.getProperty ("directivityPattern", juce::var()).toString();
            if (! directivityPatternFromString (patternStr, obj.directivityPattern))
                return juce::Result::fail ("Preset object " + juce::String (id) + ": unknown directivityPattern '" + patternStr + "'.");
        }
        varToVec (element.getProperty ("sourceOrientation", juce::var()), obj.sourceOrientation); // default {1,0,0} stays if missing

        // Not part of the schema (runtime state, not a starting parameter):
        // reset cleanly, so a freshly loaded orbit/pulse object doesn't
        // keep running with the phase/velocity of a previous state.
        obj.orbitPhase = 0.0f;
        obj.attractionPulsePhase = 0.0f;
        obj.velocity = {};
    }

    return juce::Result::ok();
}

juce::Result PresetManager::loadFile (const juce::File& file, TrajectoryEngine& engine, juce::String* outName)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("File not found: " + file.getFullPathName());

    auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("File does not contain valid JSON: " + file.getFullPathName());

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
        return juce::Result::fail ("Could not create destination folder: " + file.getParentDirectory().getFullPathName());

    if (! file.replaceWithText (jsonText))
        return juce::Result::fail ("Could not write file: " + file.getFullPathName());

    return juce::Result::ok();
}
