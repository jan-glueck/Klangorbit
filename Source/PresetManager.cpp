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
        }
        return "static";
    }

    // "attracted" is accepted on READ only, for backward compatibility
    // with presets saved before Mode::Attracted was merged into Impulse
    // (identical physics, see SoundObject.h's own comment) -- never
    // written again by modeToString() above.
    bool modeFromString (const juce::String& s, SoundObject::Mode& out)
    {
        if (s == "static")    { out = SoundObject::Mode::Static;  return true; }
        if (s == "manual")    { out = SoundObject::Mode::Manual;  return true; }
        if (s == "orbit")     { out = SoundObject::Mode::Orbit;   return true; }
        if (s == "impulse")   { out = SoundObject::Mode::Impulse; return true; }
        if (s == "attracted") { out = SoundObject::Mode::Impulse; return true; }
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

    juce::String windowShapeToString (GrainWindowShape s)
    {
        switch (s)
        {
            case GrainWindowShape::Hann: return "hann";
        }
        return "hann";
    }

    bool windowShapeFromString (const juce::String& s, GrainWindowShape& out)
    {
        if (s == "hann") { out = GrainWindowShape::Hann; return true; }
        return false;
    }

    juce::String voiceFilterTypeToString (VoiceFilterType t)
    {
        switch (t)
        {
            case VoiceFilterType::LowPass:  return "lowPass";
            case VoiceFilterType::HighPass: return "highPass";
            case VoiceFilterType::BandPass: return "bandPass";
            case VoiceFilterType::Notch:    return "notch";
        }
        return "lowPass";
    }

    bool voiceFilterTypeFromString (const juce::String& s, VoiceFilterType& out)
    {
        if (s == "lowPass")  { out = VoiceFilterType::LowPass;  return true; }
        if (s == "highPass") { out = VoiceFilterType::HighPass; return true; }
        if (s == "bandPass") { out = VoiceFilterType::BandPass; return true; }
        if (s == "notch")    { out = VoiceFilterType::Notch;    return true; }
        return false;
    }

    juce::String randomDistributionToString (RandomDistribution d)
    {
        switch (d)
        {
            case RandomDistribution::Uniform:    return "uniform";
            case RandomDistribution::Normal:     return "normal";
            case RandomDistribution::Ramp:       return "ramp";
            case RandomDistribution::LogUniform: return "logUniform";
            case RandomDistribution::PowerLaw:   return "powerLaw";
            case RandomDistribution::Discrete:   return "discrete";
        }
        return "uniform";
    }

    bool randomDistributionFromString (const juce::String& s, RandomDistribution& out)
    {
        if (s == "uniform")    { out = RandomDistribution::Uniform;    return true; }
        if (s == "normal")     { out = RandomDistribution::Normal;     return true; }
        if (s == "ramp")       { out = RandomDistribution::Ramp;       return true; }
        if (s == "logUniform") { out = RandomDistribution::LogUniform; return true; }
        if (s == "powerLaw")   { out = RandomDistribution::PowerLaw;   return true; }
        if (s == "discrete")   { out = RandomDistribution::Discrete;   return true; }
        return false;
    }

    // Generic RandomizedParam <-> var round-trip (RandomEngine.h) -- the
    // ONE serialization every future RandomizedParam-based field routes
    // through (VoiceFilterSettings::cutoffHz/resonance here, more to come
    // in later branches), rather than each hand-rolling its own.
    juce::var randomizedParamToVar (const RandomizedParam& p)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("base", (double) p.base);
        obj->setProperty ("spread", (double) p.spread);
        obj->setProperty ("distribution", randomDistributionToString (p.distribution));
        obj->setProperty ("rampTowardMax", p.rampTowardMax);
        obj->setProperty ("powerExponent", (double) p.powerExponent);
        juce::Array<juce::var> discrete;
        for (float v : p.discreteValues) discrete.add ((double) v);
        obj->setProperty ("discreteValues", discrete);
        obj->setProperty ("quantizeToSet", p.quantizeToSet);
        juce::Array<juce::var> quantize;
        for (float v : p.quantizeValues) quantize.add ((double) v);
        obj->setProperty ("quantizeValues", quantize);
        return juce::var (obj);
    }

    // "paramVar" missing/not-an-object leaves `out` at whatever default
    // the caller already set it to (same "optional, sensible default"
    // convention as every other field here) -- not a failure case.
    juce::Result randomizedParamFromVar (const juce::var& paramVar, RandomizedParam& out, const juce::String& fieldPathForErrors)
    {
        if (paramVar.isVoid())
            return juce::Result::ok();
        if (! paramVar.isObject())
            return juce::Result::fail ("'" + fieldPathForErrors + "' is not a JSON object.");

        out.base   = (float) paramVar.getProperty ("base", (double) out.base);
        out.spread = (float) paramVar.getProperty ("spread", (double) out.spread);
        if (paramVar.hasProperty ("distribution"))
        {
            const auto s = paramVar.getProperty ("distribution", juce::var()).toString();
            if (! randomDistributionFromString (s, out.distribution))
                return juce::Result::fail ("'" + fieldPathForErrors + ".distribution': unknown value '" + s + "'.");
        }
        out.rampTowardMax  = (bool) paramVar.getProperty ("rampTowardMax", out.rampTowardMax);
        out.powerExponent  = (float) paramVar.getProperty ("powerExponent", (double) out.powerExponent);

        if (auto* discreteArr = paramVar.getProperty ("discreteValues", juce::var()).getArray())
        {
            out.discreteValues.clear();
            for (auto& v : *discreteArr)
                out.discreteValues.push_back ((float) (double) v);
        }
        out.quantizeToSet = (bool) paramVar.getProperty ("quantizeToSet", out.quantizeToSet);
        if (auto* quantizeArr = paramVar.getProperty ("quantizeValues", juce::var()).getArray())
        {
            out.quantizeValues.clear();
            for (auto& v : *quantizeArr)
                out.quantizeValues.push_back ((float) (double) v);
        }
        return juce::Result::ok();
    }

    juce::var voiceFilterSettingsToVar (const VoiceFilterSettings& f)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("enabled", f.enabled);
        obj->setProperty ("type", voiceFilterTypeToString (f.type));
        obj->setProperty ("cutoffHz", randomizedParamToVar (f.cutoffHz));
        obj->setProperty ("resonance", randomizedParamToVar (f.resonance));
        obj->setProperty ("driftReversionRate", (double) f.driftReversionRate);
        obj->setProperty ("driftAmplitude", (double) f.driftAmplitude);
        obj->setProperty ("driftSmoothing", (double) f.driftSmoothing);
        return juce::var (obj);
    }

    // "filterVar" missing entirely -- always true for a preset saved
    // before feature/grain-filter-integration existed -- leaves `out` at
    // GrainCloudSettings' default VoiceFilterSettings, i.e. disabled, so
    // such a preset's sound is unaffected. Does NOT bump schemaVersion
    // for the same reason GrainCloudSettings::seed didn't (see its own
    // comment): an optional field with a harmless, sensible default.
    juce::Result voiceFilterSettingsFromVar (const juce::var& filterVar, VoiceFilterSettings& out)
    {
        if (filterVar.isVoid())
            return juce::Result::ok();
        if (! filterVar.isObject())
            return juce::Result::fail ("'grainCloud.filter' is not a JSON object.");

        if (filterVar.hasProperty ("enabled"))
            out.enabled = (bool) filterVar.getProperty ("enabled", out.enabled);
        if (filterVar.hasProperty ("type"))
        {
            const auto s = filterVar.getProperty ("type", juce::var()).toString();
            if (! voiceFilterTypeFromString (s, out.type))
                return juce::Result::fail ("'grainCloud.filter.type': unknown value '" + s + "'.");
        }
        if (auto result = randomizedParamFromVar (filterVar.getProperty ("cutoffHz", juce::var()), out.cutoffHz, "grainCloud.filter.cutoffHz");
            result.failed())
            return result;
        if (auto result = randomizedParamFromVar (filterVar.getProperty ("resonance", juce::var()), out.resonance, "grainCloud.filter.resonance");
            result.failed())
            return result;
        out.driftReversionRate = (float) filterVar.getProperty ("driftReversionRate", (double) out.driftReversionRate);
        out.driftAmplitude     = (float) filterVar.getProperty ("driftAmplitude", (double) out.driftAmplitude);
        out.driftSmoothing     = (float) filterVar.getProperty ("driftSmoothing", (double) out.driftSmoothing);
        return juce::Result::ok();
    }

    juce::String movementModeToString (GrainMovementMode m)
    {
        switch (m)
        {
            case GrainMovementMode::RandomWalk:          return "randomWalk";
            case GrainMovementMode::Bounce:               return "bounce";
            case GrainMovementMode::RadialExplosion:      return "radialExplosion";
            case GrainMovementMode::OrbitAroundParent:    return "orbitAroundParent";
            case GrainMovementMode::AttractRepelSiblings: return "attractRepelSiblings";
        }
        return "randomWalk";
    }

    bool movementModeFromString (const juce::String& s, GrainMovementMode& out)
    {
        if (s == "randomWalk")          { out = GrainMovementMode::RandomWalk;          return true; }
        if (s == "bounce")               { out = GrainMovementMode::Bounce;               return true; }
        if (s == "radialExplosion")      { out = GrainMovementMode::RadialExplosion;      return true; }
        if (s == "orbitAroundParent")    { out = GrainMovementMode::OrbitAroundParent;    return true; }
        if (s == "attractRepelSiblings") { out = GrainMovementMode::AttractRepelSiblings; return true; }
        return false;
    }

    // LEGACY: still needed to read old presets saved before dedicated
    // per-parameter jitter fields existed (grainRateJitter,
    // grainDurationJitter, initialSpeedJitter, boundaryRadiusJitter,
    // orbitRadiusJitter) -- see grainCloudSettingsFromVar()'s migration of
    // the old "jitterTarget"/"jitterRange" pair onto the matching new
    // field. Never written by grainCloudSettingsToVar() anymore, only read.
    bool jitterTargetFromString (const juce::String& s, GrainJitterTarget& out)
    {
        if (s == "none")           { out = GrainJitterTarget::None;          return true; }
        if (s == "initialSpeed")   { out = GrainJitterTarget::InitialSpeed;   return true; }
        if (s == "lifetime")       { out = GrainJitterTarget::Lifetime;       return true; }
        if (s == "boundaryRadius") { out = GrainJitterTarget::BoundaryRadius; return true; }
        if (s == "orbitRadius")    { out = GrainJitterTarget::OrbitRadius;    return true; }
        return false;
    }

    juce::String pitchJitterModeToString (PitchJitterMode m)
    {
        switch (m)
        {
            case PitchJitterMode::Random: return "random";
            case PitchJitterMode::Scale:  return "scale";
        }
        return "random";
    }

    bool pitchJitterModeFromString (const juce::String& s, PitchJitterMode& out)
    {
        if (s == "random") { out = PitchJitterMode::Random; return true; }
        if (s == "scale")  { out = PitchJitterMode::Scale;  return true; }
        return false;
    }

    juce::String pitchQuantizeScaleToString (PitchQuantizeScale s)
    {
        switch (s)
        {
            case PitchQuantizeScale::Octaves:    return "octaves";
            case PitchQuantizeScale::Fifths:     return "fifths";
            case PitchQuantizeScale::MajorTriad: return "majorTriad";
            case PitchQuantizeScale::MinorTriad: return "minorTriad";
            case PitchQuantizeScale::MajorScale: return "majorScale";
            case PitchQuantizeScale::Dorian:     return "dorian";
            case PitchQuantizeScale::Lydian:     return "lydian";
            case PitchQuantizeScale::Mixolydian: return "mixolydian";
            case PitchQuantizeScale::Aeolian:    return "aeolian";
            case PitchQuantizeScale::WholeTone:  return "wholeTone";
            case PitchQuantizeScale::Octatonic:  return "octatonic";
            case PitchQuantizeScale::Hexatonic:  return "hexatonic";
            case PitchQuantizeScale::Acoustic:   return "acoustic";
        }
        return "majorScale";
    }

    bool pitchQuantizeScaleFromString (const juce::String& s, PitchQuantizeScale& out)
    {
        if (s == "octaves")    { out = PitchQuantizeScale::Octaves;    return true; }
        if (s == "fifths")     { out = PitchQuantizeScale::Fifths;     return true; }
        if (s == "majorTriad") { out = PitchQuantizeScale::MajorTriad; return true; }
        if (s == "minorTriad") { out = PitchQuantizeScale::MinorTriad; return true; }
        if (s == "majorScale") { out = PitchQuantizeScale::MajorScale; return true; }
        if (s == "dorian")     { out = PitchQuantizeScale::Dorian;     return true; }
        if (s == "lydian")     { out = PitchQuantizeScale::Lydian;     return true; }
        if (s == "mixolydian") { out = PitchQuantizeScale::Mixolydian; return true; }
        if (s == "aeolian")    { out = PitchQuantizeScale::Aeolian;    return true; }
        if (s == "wholeTone")  { out = PitchQuantizeScale::WholeTone;  return true; }
        if (s == "octatonic")  { out = PitchQuantizeScale::Octatonic;  return true; }
        if (s == "hexatonic")  { out = PitchQuantizeScale::Hexatonic;  return true; }
        if (s == "acoustic")   { out = PitchQuantizeScale::Acoustic;   return true; }
        return false;
    }

    juce::String readDepthDistributionToString (GrainReadDepthDistribution d)
    {
        switch (d)
        {
            case GrainReadDepthDistribution::Uniform:              return "uniform";
            case GrainReadDepthDistribution::WeightedTowardRecent: return "weightedTowardRecent";
            case GrainReadDepthDistribution::WeightedTowardOld:    return "weightedTowardOld";
        }
        return "uniform";
    }

    bool readDepthDistributionFromString (const juce::String& s, GrainReadDepthDistribution& out)
    {
        if (s == "uniform")              { out = GrainReadDepthDistribution::Uniform;              return true; }
        if (s == "weightedTowardRecent") { out = GrainReadDepthDistribution::WeightedTowardRecent; return true; }
        if (s == "weightedTowardOld")    { out = GrainReadDepthDistribution::WeightedTowardOld;    return true; }
        return false;
    }

    juce::var sceneSettingsToVar (const SceneSettings& s)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("roomSize", (double) s.roomSize);
        obj->setProperty ("boundaryBehavior", boundaryBehaviorToString (s.boundaryBehavior));
        obj->setProperty ("showRoomBoundary", s.showRoomBoundary);
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

        if (sceneVar.hasProperty ("showRoomBoundary"))
            out.showRoomBoundary = (bool) sceneVar.getProperty ("showRoomBoundary", out.showRoomBoundary);

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

    juce::var grainCloudSettingsToVar (const GrainCloudSettings& s)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("enabled", s.enabled);
        obj->setProperty ("sourceMuted", s.sourceMuted);
        obj->setProperty ("dopplerEnabled", s.dopplerEnabled);
        obj->setProperty ("grainRate", (double) s.grainRate);
        obj->setProperty ("grainRateJitter", (double) s.grainRateJitter);
        obj->setProperty ("grainDuration", (double) s.grainDuration);
        obj->setProperty ("grainDurationJitter", (double) s.grainDurationJitter);
        obj->setProperty ("pitchJitter", (double) s.pitchJitter);
        obj->setProperty ("pitchJitterMode", pitchJitterModeToString (s.pitchJitterMode));
        obj->setProperty ("pitchQuantizeScale", pitchQuantizeScaleToString (s.pitchQuantizeScale));
        obj->setProperty ("positionJitterInBuffer", (double) s.positionJitterInBuffer);
        obj->setProperty ("maxConcurrentGrains", s.maxConcurrentGrains);
        obj->setProperty ("windowShape", windowShapeToString (s.windowShape));
        obj->setProperty ("movementMode", movementModeToString (s.movementMode));
        obj->setProperty ("randomWalkSpeed", (double) s.randomWalkSpeed);
        obj->setProperty ("boundaryRadius", (double) s.boundaryRadius);
        obj->setProperty ("boundaryRadiusJitter", (double) s.boundaryRadiusJitter);
        obj->setProperty ("restitution", (double) s.restitution);
        obj->setProperty ("initialSpeed", (double) s.initialSpeed);
        obj->setProperty ("initialSpeedJitter", (double) s.initialSpeedJitter);
        obj->setProperty ("acceleration", (double) s.acceleration);
        obj->setProperty ("orbitRadius", (double) s.orbitRadius);
        obj->setProperty ("orbitRadiusJitter", (double) s.orbitRadiusJitter);
        obj->setProperty ("orbitAngularSpeed", (double) s.orbitAngularSpeed);
        obj->setProperty ("orbitSphereSpread", (double) s.orbitSphereSpread);
        obj->setProperty ("attractionStrength", (double) s.attractionStrength);
        obj->setProperty ("grainReadDepthRangeMin", (double) s.grainReadDepthRangeMin);
        obj->setProperty ("grainReadDepthRangeMax", (double) s.grainReadDepthRangeMax);
        obj->setProperty ("grainReadDepthDistribution", readDepthDistributionToString (s.grainReadDepthDistribution));
        // Stored as a decimal STRING, not a JSON number -- juce::var's
        // numeric storage is a double, whose 53-bit mantissa can't
        // represent every int64 exactly, and this seed's whole purpose
        // (see Grain.h's own comment) is to make a specific random
        // sequence exactly reproducible on reload, not just approximately
        // close. A string round-trips through JSON exactly.
        obj->setProperty ("seed", juce::String (s.seed));
        obj->setProperty ("filter", voiceFilterSettingsToVar (s.filter));
        return juce::var (obj);
    }

    // "grainCloud" is optional -- if missing entirely (always true for
    // schemaVersion 1 presets, migrated or not), the GrainCloudSettings
    // defaults stay unchanged, i.e. the cloud stays disabled.
    juce::Result grainCloudSettingsFromVar (const juce::var& gcVar, GrainCloudSettings& out)
    {
        if (gcVar.isVoid())
            return juce::Result::ok();

        if (! gcVar.isObject())
            return juce::Result::fail ("'grainCloud' is not a JSON object.");

        if (gcVar.hasProperty ("enabled"))
            out.enabled = (bool) gcVar.getProperty ("enabled", out.enabled);
        if (gcVar.hasProperty ("sourceMuted"))
            out.sourceMuted = (bool) gcVar.getProperty ("sourceMuted", out.sourceMuted);
        if (gcVar.hasProperty ("dopplerEnabled"))
            out.dopplerEnabled = (bool) gcVar.getProperty ("dopplerEnabled", out.dopplerEnabled);

        out.grainRate               = (float) gcVar.getProperty ("grainRate", (double) out.grainRate);
        out.grainRateJitter         = (float) gcVar.getProperty ("grainRateJitter", (double) out.grainRateJitter);
        out.grainDuration           = (float) gcVar.getProperty ("grainDuration", (double) out.grainDuration);
        out.grainDurationJitter     = (float) gcVar.getProperty ("grainDurationJitter", (double) out.grainDurationJitter);
        out.pitchJitter             = (float) gcVar.getProperty ("pitchJitter", (double) out.pitchJitter);
        if (gcVar.hasProperty ("pitchJitterMode"))
        {
            const auto s = gcVar.getProperty ("pitchJitterMode", juce::var()).toString();
            if (! pitchJitterModeFromString (s, out.pitchJitterMode))
                return juce::Result::fail ("'grainCloud.pitchJitterMode': unknown value '" + s + "'.");
        }
        if (gcVar.hasProperty ("pitchQuantizeScale"))
        {
            const auto s = gcVar.getProperty ("pitchQuantizeScale", juce::var()).toString();
            if (! pitchQuantizeScaleFromString (s, out.pitchQuantizeScale))
                return juce::Result::fail ("'grainCloud.pitchQuantizeScale': unknown value '" + s + "'.");
        }
        out.positionJitterInBuffer = (float) gcVar.getProperty ("positionJitterInBuffer", (double) out.positionJitterInBuffer);
        out.maxConcurrentGrains     = (int) gcVar.getProperty ("maxConcurrentGrains", out.maxConcurrentGrains);

        if (gcVar.hasProperty ("windowShape"))
        {
            const auto s = gcVar.getProperty ("windowShape", juce::var()).toString();
            if (! windowShapeFromString (s, out.windowShape))
                return juce::Result::fail ("'grainCloud.windowShape': unknown value '" + s + "'.");
        }
        if (gcVar.hasProperty ("movementMode"))
        {
            const auto s = gcVar.getProperty ("movementMode", juce::var()).toString();
            if (! movementModeFromString (s, out.movementMode))
                return juce::Result::fail ("'grainCloud.movementMode': unknown value '" + s + "'.");
        }

        out.randomWalkSpeed     = (float) gcVar.getProperty ("randomWalkSpeed", (double) out.randomWalkSpeed);
        out.boundaryRadius      = (float) gcVar.getProperty ("boundaryRadius", (double) out.boundaryRadius);
        out.boundaryRadiusJitter = (float) gcVar.getProperty ("boundaryRadiusJitter", (double) out.boundaryRadiusJitter);
        out.restitution          = (float) gcVar.getProperty ("restitution", (double) out.restitution);
        out.initialSpeed         = (float) gcVar.getProperty ("initialSpeed", (double) out.initialSpeed);
        out.initialSpeedJitter   = (float) gcVar.getProperty ("initialSpeedJitter", (double) out.initialSpeedJitter);
        out.acceleration         = (float) gcVar.getProperty ("acceleration", (double) out.acceleration);
        out.orbitRadius          = (float) gcVar.getProperty ("orbitRadius", (double) out.orbitRadius);
        out.orbitRadiusJitter    = (float) gcVar.getProperty ("orbitRadiusJitter", (double) out.orbitRadiusJitter);
        out.orbitAngularSpeed   = (float) gcVar.getProperty ("orbitAngularSpeed", (double) out.orbitAngularSpeed);
        out.orbitSphereSpread   = (float) gcVar.getProperty ("orbitSphereSpread", (double) out.orbitSphereSpread);
        out.attractionStrength  = (float) gcVar.getProperty ("attractionStrength", (double) out.attractionStrength);

        // LEGACY: presets saved before dedicated per-parameter jitter
        // fields existed used one shared "jitterTarget"/"jitterRange"
        // pair (mutually exclusive -- only one field could be jittered at
        // a time). Migrate onto whichever new field it targeted, so an
        // old preset's jitter setting isn't silently lost. Never written
        // by grainCloudSettingsToVar() anymore -- re-saving upgrades a
        // preset to the new format automatically.
        if (gcVar.hasProperty ("jitterTarget"))
        {
            const auto s = gcVar.getProperty ("jitterTarget", juce::var()).toString();
            GrainJitterTarget legacyTarget;
            if (! jitterTargetFromString (s, legacyTarget))
                return juce::Result::fail ("'grainCloud.jitterTarget': unknown value '" + s + "'.");

            const float legacyRange = (float) gcVar.getProperty ("jitterRange", 0.0);
            switch (legacyTarget)
            {
                case GrainJitterTarget::None:           break;
                case GrainJitterTarget::InitialSpeed:   out.initialSpeedJitter   = legacyRange; break;
                case GrainJitterTarget::Lifetime:       out.grainDurationJitter  = legacyRange; break;
                case GrainJitterTarget::BoundaryRadius: out.boundaryRadiusJitter = legacyRange; break;
                case GrainJitterTarget::OrbitRadius:    out.orbitRadiusJitter    = legacyRange; break;
            }
        }

        out.grainReadDepthRangeMin = (float) gcVar.getProperty ("grainReadDepthRangeMin", (double) out.grainReadDepthRangeMin);
        out.grainReadDepthRangeMax = (float) gcVar.getProperty ("grainReadDepthRangeMax", (double) out.grainReadDepthRangeMax);
        if (gcVar.hasProperty ("grainReadDepthDistribution"))
        {
            const auto s = gcVar.getProperty ("grainReadDepthDistribution", juce::var()).toString();
            if (! readDepthDistributionFromString (s, out.grainReadDepthDistribution))
                return juce::Result::fail ("'grainCloud.grainReadDepthDistribution': unknown value '" + s + "'.");
        }

        // Missing entirely (preset saved before this field existed, or
        // schemaVersion-1 content) -- out.seed simply keeps whatever the
        // GrainCloud that owns it already assigned itself (a fresh random
        // seed, see GrainCloud's constructor/reset()), same as every other
        // optional field's "no value saved yet" default here.
        if (gcVar.hasProperty ("seed"))
            out.seed = gcVar.getProperty ("seed", juce::var()).toString().getLargeIntValue();

        if (auto result = voiceFilterSettingsFromVar (gcVar.getProperty ("filter", juce::var()), out.filter);
            result.failed())
            return result;

        return juce::Result::ok();
    }
}

juce::var PresetManager::migrateSchemaV1toV2 (const juce::var& v1Root)
{
    // Every schemaVersion-2 addition (per-object "grainCloud") is optional
    // with a sensible default -- v1 content is already valid v2 content,
    // this just needs to relabel it. A real field rename/removal in a
    // future version would do its actual transformation work here instead.
    if (! v1Root.isObject())
        return v1Root;

    auto* migrated = new juce::DynamicObject (*v1Root.getDynamicObject());
    migrated->setProperty ("schemaVersion", currentSchemaVersion);
    return juce::var (migrated);
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
        objVar->setProperty ("momentumEnabled", obj.momentumEnabled);
        objVar->setProperty ("orbitCenter", vecToVar (obj.orbitCenter));
        objVar->setProperty ("orbitRadius", (double) obj.orbitRadius);
        objVar->setProperty ("orbitAngularSpeed", (double) obj.orbitAngularSpeed);
        objVar->setProperty ("attractionStrength", (double) obj.attractionStrength);
        objVar->setProperty ("mass", (double) obj.mass);
        objVar->setProperty ("damping", (double) obj.damping);
        objVar->setProperty ("gain", (double) obj.gain);
        objVar->setProperty ("muted", obj.muted);
        objVar->setProperty ("soloed", obj.soloed);

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
        objVar->setProperty ("orbitOrientation", (double) obj.orbitOrientation);
        objVar->setProperty ("orbitDecay", (double) obj.orbitDecay);
        objVar->setProperty ("orbitRadiusBaseline", (double) obj.orbitRadiusBaseline);
        objVar->setProperty ("orbitRadiusReversionRate", (double) obj.orbitRadiusReversionRate);
        objVar->setProperty ("orbitRadiusNoiseAmplitude", (double) obj.orbitRadiusNoiseAmplitude);
        objVar->setProperty ("orbitRadiusNoiseSmoothing", (double) obj.orbitRadiusNoiseSmoothing);
        // String, not a JSON number -- same exact-round-trip reasoning as
        // grainCloudSettingsToVar()'s "seed" field above.
        objVar->setProperty ("orbitNoiseSeed", juce::String (obj.orbitNoiseSeed));
        objVar->setProperty ("orbitReferenceObjectId", obj.orbitReferenceObjectId);

        // Acoustic propagation (optional, see SoundObject.h)
        objVar->setProperty ("dopplerEnabled", obj.dopplerEnabled);
        objVar->setProperty ("dopplerFactor", (double) obj.dopplerFactor);
        objVar->setProperty ("dopplerSmoothing", (double) obj.dopplerSmoothing);
        objVar->setProperty ("directivityPattern", directivityPatternToString (obj.directivityPattern));
        objVar->setProperty ("sourceOrientation", vecToVar (obj.sourceOrientation));

        // GrainCloud (optional, schemaVersion 2+, see Grain.h)
        objVar->setProperty ("grainCloud", grainCloudSettingsToVar (engine.getGrainCloud (i).getSettings()));

        objectsArray.add (juce::var (objVar));
    }

    root->setProperty ("objects", objectsArray);
    return juce::var (root);
}

juce::Result PresetManager::loadFromVar (const juce::var& originalRoot, TrajectoryEngine& engine)
{
    if (! originalRoot.isObject())
        return juce::Result::fail ("Preset is not a valid JSON object.");

    auto schemaVersionVar = originalRoot.getProperty ("schemaVersion", juce::var());
    if (! (schemaVersionVar.isInt() || schemaVersionVar.isDouble() || schemaVersionVar.isInt64()))
        return juce::Result::fail ("Preset has no numeric 'schemaVersion' field.");

    const int schemaVersion = (int) schemaVersionVar;
    if (schemaVersion < oldestSupportedSchemaVersion || schemaVersion > currentSchemaVersion)
        return juce::Result::fail ("Preset schemaVersion " + juce::String (schemaVersion)
                                    + " is not supported (supported: " + juce::String (oldestSupportedSchemaVersion)
                                    + ".." + juce::String (currentSchemaVersion) + ").");

    // Migrate step by step up to the current version. Only one step exists
    // right now (1 -> 2); a future 2 -> 3 migration would chain the same way.
    juce::var root = originalRoot;
    if (schemaVersion == 1)
        root = migrateSchemaV1toV2 (root);

    auto* objectsArray = root.getProperty ("objects", juce::var()).getArray();
    if (objectsArray == nullptr)
        return juce::Result::fail ("Preset has no 'objects' array.");

    // First reset all objects (and their grain clouds) to a clean, inactive
    // starting state -- a preset is a complete scene, not a diff against
    // what was loaded before.
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        const int id = engine.getObject (i).id;
        engine.getObject (i) = SoundObject { id };
        engine.getGrainCloud (i).reset();
        engine.reseedOrbitNoise (i);
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

        if (element.hasProperty ("momentumEnabled"))
            obj.momentumEnabled = (bool) element.getProperty ("momentumEnabled", obj.momentumEnabled);

        varToVec (element.getProperty ("orbitCenter", juce::var()), obj.orbitCenter); // optional, default stays {0,0,0}
        obj.orbitRadius        = (float) element.getProperty ("orbitRadius", (double) obj.orbitRadius);
        obj.orbitAngularSpeed  = (float) element.getProperty ("orbitAngularSpeed", (double) obj.orbitAngularSpeed);
        obj.attractionStrength = (float) element.getProperty ("attractionStrength", (double) obj.attractionStrength);
        obj.mass                = (float) element.getProperty ("mass", (double) obj.mass);
        obj.damping              = (float) element.getProperty ("damping", (double) obj.damping);
        obj.gain                 = (float) element.getProperty ("gain", (double) obj.gain);
        if (element.hasProperty ("muted"))
            obj.muted = (bool) element.getProperty ("muted", obj.muted);
        if (element.hasProperty ("soloed"))
            obj.soloed = (bool) element.getProperty ("soloed", obj.soloed);

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
        obj.orbitOrientation      = (float) element.getProperty ("orbitOrientation", (double) obj.orbitOrientation);
        obj.orbitDecay            = (float) element.getProperty ("orbitDecay", (double) obj.orbitDecay);
        obj.orbitRadiusBaseline         = (float) element.getProperty ("orbitRadiusBaseline", (double) obj.orbitRadiusBaseline);
        obj.orbitRadiusReversionRate    = (float) element.getProperty ("orbitRadiusReversionRate", (double) obj.orbitRadiusReversionRate);
        obj.orbitRadiusNoiseAmplitude   = (float) element.getProperty ("orbitRadiusNoiseAmplitude", (double) obj.orbitRadiusNoiseAmplitude);
        obj.orbitRadiusNoiseSmoothing   = (float) element.getProperty ("orbitRadiusNoiseSmoothing", (double) obj.orbitRadiusNoiseSmoothing);
        // Missing entirely (preset saved before this field existed) --
        // obj.orbitNoiseSeed keeps whatever TrajectoryEngine already
        // assigned it (a fresh random seed, see activateObject()/the
        // constructor), same "no value saved yet" default as grainCloud's
        // own seed field above.
        if (element.hasProperty ("orbitNoiseSeed"))
            obj.orbitNoiseSeed = element.getProperty ("orbitNoiseSeed", juce::var()).toString().getLargeIntValue();
        obj.orbitReferenceObjectId = (int) element.getProperty ("orbitReferenceObjectId", obj.orbitReferenceObjectId);

        // Acoustic propagation (optional, default from SoundObject{})
        obj.dopplerEnabled   = (bool) element.getProperty ("dopplerEnabled", obj.dopplerEnabled);
        obj.dopplerFactor    = (float) element.getProperty ("dopplerFactor", (double) obj.dopplerFactor);
        obj.dopplerSmoothing = (float) element.getProperty ("dopplerSmoothing", (double) obj.dopplerSmoothing);

        if (element.hasProperty ("directivityPattern"))
        {
            const auto patternStr = element.getProperty ("directivityPattern", juce::var()).toString();
            if (! directivityPatternFromString (patternStr, obj.directivityPattern))
                return juce::Result::fail ("Preset object " + juce::String (id) + ": unknown directivityPattern '" + patternStr + "'.");
        }
        varToVec (element.getProperty ("sourceOrientation", juce::var()), obj.sourceOrientation); // default {1,0,0} stays if missing

        // GrainCloud (optional, schemaVersion 2+ -- absent for migrated v1 presets, defaults/disabled stays)
        auto grainCloudResult = grainCloudSettingsFromVar (element.getProperty ("grainCloud", juce::var()),
                                                              engine.getGrainCloud (id).getSettings());
        if (grainCloudResult.failed())
            return grainCloudResult;

        // Not part of the schema (runtime state, not a starting parameter):
        // reset cleanly, so a freshly loaded orbit/pulse object doesn't
        // keep running with the phase/velocity of a previous state.
        obj.orbitPhase = 0.0f;
        obj.orbitRadiusNoiseSmoothed = 0.0f;
        obj.attractionPulsePhase = 0.0f;
        obj.velocity = {};
        obj.slingshotTargetId = -1;
        obj.slingshotStrength = 0.0f;
        obj.manualVelocityActive = false;
        obj.manualVelocity = {};
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
