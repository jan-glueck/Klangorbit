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

    // Modifier stack (feature/modifier-stack, Source/Modifier.h) -- one
    // generic serialization for the whole ordered list, shared by
    // GrainCloudSettings::modifiers and DelayInstanceSettings::modifiers
    // (both just a std::vector<Modifier>). Reuses randomizedParamToVar/
    // FromVar and voiceFilterSettingsToVar/FromVar above rather than
    // re-encoding RandomizedParam/VoiceFilterSettings a second way.
    juce::String modifierTypeToString (ModifierType t)
    {
        switch (t)
        {
            case ModifierType::Gain:       return "gain";
            case ModifierType::TimeOffset: return "timeOffset";
            case ModifierType::Filter:     return "filter";
            case ModifierType::Pitch:      return "pitch";
            case ModifierType::Movement:   return "movement";
            case ModifierType::SpatialOffset: return "spatialOffset";
        }
        return "gain";
    }

    bool modifierTypeFromString (const juce::String& s, ModifierType& out)
    {
        if (s == "gain")          { out = ModifierType::Gain;          return true; }
        if (s == "timeOffset")    { out = ModifierType::TimeOffset;    return true; }
        if (s == "filter")        { out = ModifierType::Filter;        return true; }
        if (s == "pitch")         { out = ModifierType::Pitch;         return true; }
        if (s == "movement")      { out = ModifierType::Movement;      return true; }
        if (s == "spatialOffset") { out = ModifierType::SpatialOffset; return true; }
        return false;
    }

    juce::String movementModifierShapeToString (MovementModifierShape s)
    {
        switch (s)
        {
            case MovementModifierShape::Linear:   return "linear";
            case MovementModifierShape::Rotating: return "rotating";
        }
        return "linear";
    }

    bool movementModifierShapeFromString (const juce::String& s, MovementModifierShape& out)
    {
        if (s == "linear")   { out = MovementModifierShape::Linear;   return true; }
        if (s == "rotating") { out = MovementModifierShape::Rotating; return true; }
        return false;
    }

    juce::String spatialDistributionShapeToString (SpatialDistributionShape s)
    {
        switch (s)
        {
            case SpatialDistributionShape::Sphere:        return "sphere";
            case SpatialDistributionShape::SphericalShell: return "sphericalShell";
            case SpatialDistributionShape::Ring:           return "ring";
            case SpatialDistributionShape::Line:           return "line";
            case SpatialDistributionShape::Cuboid:         return "cuboid";
        }
        return "sphere";
    }

    bool spatialDistributionShapeFromString (const juce::String& s, SpatialDistributionShape& out)
    {
        if (s == "sphere")         { out = SpatialDistributionShape::Sphere;        return true; }
        if (s == "sphericalShell") { out = SpatialDistributionShape::SphericalShell; return true; }
        if (s == "ring")           { out = SpatialDistributionShape::Ring;          return true; }
        if (s == "line")           { out = SpatialDistributionShape::Line;          return true; }
        if (s == "cuboid")         { out = SpatialDistributionShape::Cuboid;        return true; }
        return false;
    }

    juce::var modifierToVar (const Modifier& m)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("enabled", m.enabled);
        obj->setProperty ("type", modifierTypeToString (m.type));
        obj->setProperty ("gain", randomizedParamToVar (m.gain));
        obj->setProperty ("timeOffset", randomizedParamToVar (m.timeOffset));
        obj->setProperty ("filter", voiceFilterSettingsToVar (m.filter));
        obj->setProperty ("pitchSemitones", randomizedParamToVar (m.pitchSemitones));
        obj->setProperty ("movementShape", movementModifierShapeToString (m.movementShape));
        obj->setProperty ("movementSpeed", randomizedParamToVar (m.movementSpeed));
        obj->setProperty ("movementAcceleration", randomizedParamToVar (m.movementAcceleration));
        obj->setProperty ("spatialShape", spatialDistributionShapeToString (m.spatialShape));
        obj->setProperty ("spatialRadius", randomizedParamToVar (m.spatialRadius));
        return juce::var (obj);
    }

    juce::Result modifierFromVar (const juce::var& mVar, const juce::String& fieldPathForErrors, Modifier& out)
    {
        if (! mVar.isObject())
            return juce::Result::fail ("'" + fieldPathForErrors + "' is not a JSON object.");

        if (mVar.hasProperty ("enabled"))
            out.enabled = (bool) mVar.getProperty ("enabled", out.enabled);
        if (mVar.hasProperty ("type"))
        {
            const auto s = mVar.getProperty ("type", juce::var()).toString();
            if (! modifierTypeFromString (s, out.type))
                return juce::Result::fail ("'" + fieldPathForErrors + ".type': unknown value '" + s + "'.");
        }
        if (auto result = randomizedParamFromVar (mVar.getProperty ("gain", juce::var()), out.gain, fieldPathForErrors + ".gain");
            result.failed())
            return result;
        if (auto result = randomizedParamFromVar (mVar.getProperty ("timeOffset", juce::var()), out.timeOffset, fieldPathForErrors + ".timeOffset");
            result.failed())
            return result;
        if (auto result = voiceFilterSettingsFromVar (mVar.getProperty ("filter", juce::var()), out.filter);
            result.failed())
            return result;
        if (auto result = randomizedParamFromVar (mVar.getProperty ("pitchSemitones", juce::var()), out.pitchSemitones, fieldPathForErrors + ".pitchSemitones");
            result.failed())
            return result;
        if (mVar.hasProperty ("movementShape"))
        {
            const auto s = mVar.getProperty ("movementShape", juce::var()).toString();
            if (! movementModifierShapeFromString (s, out.movementShape))
                return juce::Result::fail ("'" + fieldPathForErrors + ".movementShape': unknown value '" + s + "'.");
        }
        if (auto result = randomizedParamFromVar (mVar.getProperty ("movementSpeed", juce::var()), out.movementSpeed, fieldPathForErrors + ".movementSpeed");
            result.failed())
            return result;
        if (auto result = randomizedParamFromVar (mVar.getProperty ("movementAcceleration", juce::var()), out.movementAcceleration, fieldPathForErrors + ".movementAcceleration");
            result.failed())
            return result;
        if (mVar.hasProperty ("spatialShape"))
        {
            const auto s = mVar.getProperty ("spatialShape", juce::var()).toString();
            if (! spatialDistributionShapeFromString (s, out.spatialShape))
                return juce::Result::fail ("'" + fieldPathForErrors + ".spatialShape': unknown value '" + s + "'.");
        }
        if (auto result = randomizedParamFromVar (mVar.getProperty ("spatialRadius", juce::var()), out.spatialRadius, fieldPathForErrors + ".spatialRadius");
            result.failed())
            return result;
        return juce::Result::ok();
    }

    juce::var modifiersToVar (const std::vector<Modifier>& stack)
    {
        juce::Array<juce::var> arr;
        for (auto& m : stack)
            arr.add (modifierToVar (m));
        return arr;
    }

    // "stackVar" missing entirely -- always true for a preset saved
    // before feature/modifier-stack existed -- leaves `out` at whatever
    // it already was (the caller's default-constructed, empty stack),
    // same "optional, sensible default" convention as every other field
    // here. Does NOT bump schemaVersion for the same reason.
    juce::Result modifiersFromVar (const juce::var& stackVar, const juce::String& fieldPathForErrors, std::vector<Modifier>& out)
    {
        if (stackVar.isVoid())
            return juce::Result::ok();
        auto* arr = stackVar.getArray();
        if (arr == nullptr)
            return juce::Result::fail ("'" + fieldPathForErrors + "' is not a JSON array.");

        out.clear();
        for (int i = 0; i < arr->size(); ++i)
        {
            Modifier m;
            if (auto result = modifierFromVar ((*arr)[i], fieldPathForErrors + "[" + juce::String (i) + "]", m);
                result.failed())
                return result;
            out.push_back (m);
        }
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

    juce::String grainGenerationModeToString (GrainGenerationMode m)
    {
        switch (m)
        {
            case GrainGenerationMode::Emitter: return "emitter";
            case GrainGenerationMode::Group:   return "group";
        }
        return "emitter";
    }

    bool grainGenerationModeFromString (const juce::String& s, GrainGenerationMode& out)
    {
        if (s == "emitter") { out = GrainGenerationMode::Emitter; return true; }
        if (s == "group")   { out = GrainGenerationMode::Group;   return true; }
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

        // Agitation tempo-sync fallback (feature/agitation-and-snapshots,
        // Branch 5) -- see SceneSettings::standaloneTempoBpm's own
        // comment for why this exists.
        obj->setProperty ("standaloneTempoBpm", (double) s.standaloneTempoBpm);
        // String, not a JSON number -- same exact-round-trip reasoning as
        // grainCloudSettingsToVar()'s "seed" field.
        obj->setProperty ("agitationSeed", juce::String (s.agitationSeed));

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

        out.standaloneTempoBpm = (float) sceneVar.getProperty ("standaloneTempoBpm", (double) out.standaloneTempoBpm);
        // Missing entirely (preset saved before this field existed) --
        // out.agitationSeed simply keeps whatever default/already-
        // assigned value it has, same "no value saved yet" treatment as
        // every other seed field in this file.
        if (sceneVar.hasProperty ("agitationSeed"))
            out.agitationSeed = sceneVar.getProperty ("agitationSeed", juce::var()).toString().getLargeIntValue();

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
        obj->setProperty ("modifiers", modifiersToVar (s.modifiers));
        obj->setProperty ("generationMode", grainGenerationModeToString (s.generationMode));
        obj->setProperty ("groupSize", s.groupSize);
        obj->setProperty ("groupStartDelay", randomizedParamToVar (s.groupStartDelay));
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

        if (auto result = modifiersFromVar (gcVar.getProperty ("modifiers", juce::var()), "grainCloud.modifiers", out.modifiers);
            result.failed())
            return result;

        if (gcVar.hasProperty ("generationMode"))
        {
            const auto s = gcVar.getProperty ("generationMode", juce::var()).toString();
            if (! grainGenerationModeFromString (s, out.generationMode))
                return juce::Result::fail ("'grainCloud.generationMode': unknown value '" + s + "'.");
        }
        out.groupSize = (int) gcVar.getProperty ("groupSize", out.groupSize);
        if (auto result = randomizedParamFromVar (gcVar.getProperty ("groupStartDelay", juce::var()), out.groupStartDelay, "grainCloud.groupStartDelay");
            result.failed())
            return result;

        return juce::Result::ok();
    }

    // DelayInstance (feature/delay-instance, Source/DelayTap.h) -- same
    // "fully optional, no schemaVersion bump" treatment as GrainCloud's
    // own filter block above: a preset saved before this existed simply
    // leaves `out` at its default (disabled) VoiceFilterSettings.
    juce::var delayInstanceSettingsToVar (const DelayInstanceSettings& s)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("enabled", s.enabled);
        obj->setProperty ("numTaps", s.numTaps);
        obj->setProperty ("delayTime", (double) s.delayTime);
        obj->setProperty ("delayTimeJitter", (double) s.delayTimeJitter);
        obj->setProperty ("feedback", (double) s.feedback);
        obj->setProperty ("feedbackDamping", (double) s.feedbackDamping);
        obj->setProperty ("tapSpreadRadius", (double) s.tapSpreadRadius);
        obj->setProperty ("tapOrbitAngularSpeed", (double) s.tapOrbitAngularSpeed);
        // String, not a JSON number -- same 64-bit-exact-round-trip reason
        // as GrainCloudSettings::seed (see its own comment).
        obj->setProperty ("seed", juce::String (s.seed));
        obj->setProperty ("modifiers", modifiersToVar (s.modifiers));
        return juce::var (obj);
    }

    juce::Result delayInstanceSettingsFromVar (const juce::var& diVar, DelayInstanceSettings& out)
    {
        if (diVar.isVoid())
            return juce::Result::ok();
        if (! diVar.isObject())
            return juce::Result::fail ("'delayInstance' is not a JSON object.");

        if (diVar.hasProperty ("enabled"))
            out.enabled = (bool) diVar.getProperty ("enabled", out.enabled);
        out.numTaps              = (int) diVar.getProperty ("numTaps", out.numTaps);
        out.delayTime             = (float) diVar.getProperty ("delayTime", (double) out.delayTime);
        out.delayTimeJitter       = (float) diVar.getProperty ("delayTimeJitter", (double) out.delayTimeJitter);
        out.feedback              = (float) diVar.getProperty ("feedback", (double) out.feedback);
        out.feedbackDamping       = (float) diVar.getProperty ("feedbackDamping", (double) out.feedbackDamping);
        out.tapSpreadRadius       = (float) diVar.getProperty ("tapSpreadRadius", (double) out.tapSpreadRadius);
        out.tapOrbitAngularSpeed  = (float) diVar.getProperty ("tapOrbitAngularSpeed", (double) out.tapOrbitAngularSpeed);
        if (diVar.hasProperty ("seed"))
            out.seed = diVar.getProperty ("seed", juce::var()).toString().getLargeIntValue();

        if (auto result = modifiersFromVar (diVar.getProperty ("modifiers", juce::var()), "delayInstance.modifiers", out.modifiers);
            result.failed())
            return result;

        return juce::Result::ok();
    }

    // --- AgitationEngine (feature/agitation-and-snapshots, Branch 5) -------
    // Lives on KlangorbitProcessor, not TrajectoryEngine (see PresetManager.h's
    // own comment on sceneToVar()'s agitationEngine parameter) -- a separate
    // top-level "agitation" block rather than something nested under each
    // object, since a parameter id already encodes which object (or
    // "selectedObject"/"scene") it belongs to, see ParameterRegistry.h.
    juce::String agitationTempoDivisionToString (Agitation::TempoDivision d)
    {
        switch (d)
        {
            case Agitation::TempoDivision::Whole:            return "whole";
            case Agitation::TempoDivision::Half:             return "half";
            case Agitation::TempoDivision::Quarter:          return "quarter";
            case Agitation::TempoDivision::Eighth:           return "eighth";
            case Agitation::TempoDivision::Sixteenth:        return "sixteenth";
            case Agitation::TempoDivision::QuarterTriplet:   return "quarterTriplet";
            case Agitation::TempoDivision::EighthTriplet:    return "eighthTriplet";
            case Agitation::TempoDivision::SixteenthTriplet: return "sixteenthTriplet";
        }
        return "quarter";
    }

    bool agitationTempoDivisionFromString (const juce::String& s, Agitation::TempoDivision& out)
    {
        if (s == "whole")            { out = Agitation::TempoDivision::Whole;            return true; }
        if (s == "half")             { out = Agitation::TempoDivision::Half;             return true; }
        if (s == "quarter")          { out = Agitation::TempoDivision::Quarter;          return true; }
        if (s == "eighth")           { out = Agitation::TempoDivision::Eighth;           return true; }
        if (s == "sixteenth")        { out = Agitation::TempoDivision::Sixteenth;        return true; }
        if (s == "quarterTriplet")   { out = Agitation::TempoDivision::QuarterTriplet;   return true; }
        if (s == "eighthTriplet")    { out = Agitation::TempoDivision::EighthTriplet;    return true; }
        if (s == "sixteenthTriplet") { out = Agitation::TempoDivision::SixteenthTriplet; return true; }
        return false;
    }

    // One array entry per parameter id that has ANY Agitation entry at
    // all (enabled or not) -- same "touching it is enough to persist it"
    // treatment AgitationEngine::settingsFor() itself already has; a
    // disabled-but-configured entry (e.g. user set up Amount/Rate then
    // unticked Enabled) still round-trips its own values rather than
    // being silently discarded.
    juce::var agitationEngineToVar (const AgitationEngine& engine)
    {
        juce::Array<juce::var> array;
        for (auto& [id, settings] : engine.all())
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", id);
            obj->setProperty ("enabled", settings.enabled);
            obj->setProperty ("reversionRate", (double) settings.reversionRate);
            obj->setProperty ("amount", (double) settings.amount);
            obj->setProperty ("smoothing", (double) settings.smoothing);
            obj->setProperty ("tempoSynced", settings.tempoSynced);
            obj->setProperty ("tempoDivision", agitationTempoDivisionToString (settings.tempoDivision));
            array.add (juce::var (obj));
        }
        return juce::var (array);
    }

    // "agitation" is optional -- missing entirely (every preset saved
    // before this field existed, or saved with agitationEngine ==
    // nullptr) leaves `out` untouched if `clearFirst` is false, or
    // cleared to empty if true -- see loadFromVar()'s own call site for
    // which one it needs and why.
    juce::Result agitationEngineFromVar (const juce::var& agitationVar, AgitationEngine& out, bool clearFirst)
    {
        if (clearFirst)
            out.clear();

        if (agitationVar.isVoid())
            return juce::Result::ok();

        auto* array = agitationVar.getArray();
        if (array == nullptr)
            return juce::Result::fail ("'agitation' is not a JSON array.");

        for (auto& element : *array)
        {
            if (! element.isObject())
                return juce::Result::fail ("'agitation' contains an element that is not a JSON object.");
            if (! element.hasProperty ("id"))
                return juce::Result::fail ("'agitation' contains an entry without an 'id' field.");

            const auto id = element.getProperty ("id", juce::var()).toString();
            auto& settings = out.settingsFor (id);

            if (element.hasProperty ("enabled"))
                settings.enabled = (bool) element.getProperty ("enabled", settings.enabled);
            settings.reversionRate = (float) element.getProperty ("reversionRate", (double) settings.reversionRate);
            settings.amount        = (float) element.getProperty ("amount", (double) settings.amount);
            settings.smoothing     = (float) element.getProperty ("smoothing", (double) settings.smoothing);
            if (element.hasProperty ("tempoSynced"))
                settings.tempoSynced = (bool) element.getProperty ("tempoSynced", settings.tempoSynced);
            if (element.hasProperty ("tempoDivision"))
            {
                const auto s = element.getProperty ("tempoDivision", juce::var()).toString();
                if (! agitationTempoDivisionFromString (s, settings.tempoDivision))
                    return juce::Result::fail ("'agitation' entry '" + id + "': unknown tempoDivision '" + s + "'.");
            }
        }

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

juce::var PresetManager::sceneToVar (TrajectoryEngine& engine, const juce::String& name, const AgitationEngine* agitationEngine)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", currentSchemaVersion);
    root->setProperty ("name", name);
    root->setProperty ("scene", sceneSettingsToVar (engine.getSceneSettings()));
    if (agitationEngine != nullptr)
        root->setProperty ("agitation", agitationEngineToVar (*agitationEngine));

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

        // DelayInstance (optional, see DelayTap.h) -- feature/delay-instance
        objVar->setProperty ("delayInstance", delayInstanceSettingsToVar (engine.getDelayInstance (i).getSettings()));

        objectsArray.add (juce::var (objVar));
    }

    root->setProperty ("objects", objectsArray);
    return juce::var (root);
}

juce::Result PresetManager::loadFromVar (const juce::var& originalRoot, TrajectoryEngine& engine, AgitationEngine* agitationEngine)
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
        engine.getDelayInstance (i).reset();
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

        // DelayInstance (optional -- absent for every preset saved before
        // feature/delay-instance existed, defaults/disabled stays)
        auto delayInstanceResult = delayInstanceSettingsFromVar (element.getProperty ("delayInstance", juce::var()),
                                                                   engine.getDelayInstance (id).getSettings());
        if (delayInstanceResult.failed())
            return delayInstanceResult;

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

    // agitationEngine == nullptr: caller doesn't want agitation touched
    // at all (e.g. a test that only cares about the scene) -- leave it
    // untouched, same as any other field this function doesn't know
    // about. Non-null: REPLACE its entire contents, same "a preset
    // describes a complete configuration" policy as the scene objects
    // above -- clearFirst == true even when the preset has no
    // "agitation" block at all, so loading an older/agitation-less
    // preset on top of a scene that currently has Agitation configured
    // actually clears it, rather than leaving stale entries behind.
    if (agitationEngine != nullptr)
    {
        if (auto result = agitationEngineFromVar (root.getProperty ("agitation", juce::var()), *agitationEngine, true);
            result.failed())
            return result;
    }

    return juce::Result::ok();
}

juce::Result PresetManager::loadFile (const juce::File& file, TrajectoryEngine& engine, juce::String* outName,
                                        AgitationEngine* agitationEngine)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("File not found: " + file.getFullPathName());

    auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("File does not contain valid JSON: " + file.getFullPathName());

    auto result = loadFromVar (parsed, engine, agitationEngine);
    if (result.wasOk() && outName != nullptr)
        *outName = parsed.getProperty ("name", file.getFileNameWithoutExtension()).toString();

    return result;
}

juce::Result PresetManager::saveFile (const juce::File& file, TrajectoryEngine& engine, const juce::String& name,
                                        const AgitationEngine* agitationEngine)
{
    auto sceneVar = sceneToVar (engine, name, agitationEngine);
    auto jsonText = juce::JSON::toString (sceneVar);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Could not create destination folder: " + file.getParentDirectory().getFullPathName());

    if (! file.replaceWithText (jsonText))
        return juce::Result::fail ("Could not write file: " + file.getFullPathName());

    return juce::Result::ok();
}
