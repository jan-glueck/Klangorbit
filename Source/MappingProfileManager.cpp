#include "MappingProfileManager.h"

juce::var MappingProfileManager::profileToVar (const MappingEngine& engine, const juce::String& name)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", currentSchemaVersion);
    root->setProperty ("name", name);
    root->setProperty ("modifierSourceId", engine.getModifierSourceId());

    juce::Array<juce::var> bindingsArray;
    for (auto& b : engine.getBindings())
    {
        auto* bindingVar = new juce::DynamicObject();
        bindingVar->setProperty ("sourceId", b.sourceId);
        bindingVar->setProperty ("parameterId", b.parameterId);
        bindingVar->setProperty ("bank", b.bank);
        bindingsArray.add (juce::var (bindingVar));
    }
    root->setProperty ("bindings", bindingsArray);

    return juce::var (root);
}

juce::Result MappingProfileManager::loadFromVar (const juce::var& root, MappingEngine& engine)
{
    if (! root.isObject())
        return juce::Result::fail ("Mapping profile is not a valid JSON object.");

    const int schemaVersion = (int) root.getProperty ("schemaVersion", 0);
    if (schemaVersion != currentSchemaVersion)
        return juce::Result::fail ("Unsupported mapping profile schemaVersion: " + juce::String (schemaVersion)
                                    + " (this build supports " + juce::String (currentSchemaVersion) + ").");

    auto* bindingsArray = root.getProperty ("bindings", juce::var()).getArray();
    if (bindingsArray == nullptr)
        return juce::Result::fail ("Mapping profile is missing its \"bindings\" array.");

    std::vector<MappingBinding> parsedBindings;
    parsedBindings.reserve ((size_t) bindingsArray->size());
    for (auto& entry : *bindingsArray)
    {
        if (! entry.isObject())
            return juce::Result::fail ("Mapping profile contains a binding that isn't a JSON object.");

        const auto sourceId = entry.getProperty ("sourceId", juce::var()).toString();
        const auto parameterId = entry.getProperty ("parameterId", juce::var()).toString();
        if (sourceId.isEmpty() || parameterId.isEmpty())
            return juce::Result::fail ("Mapping profile contains a binding with a missing sourceId or parameterId.");

        MappingBinding binding;
        binding.sourceId = sourceId;
        binding.parameterId = parameterId;
        binding.bank = (int) entry.getProperty ("bank", 0);
        parsedBindings.push_back (std::move (binding));
    }

    // Only commit to the engine once the whole file has parsed
    // successfully -- a malformed profile should never leave the engine
    // in a half-replaced state (same all-or-nothing policy
    // PresetManager::loadFromVar() already established for scenes).
    engine.clearAllBindings();
    for (auto& b : parsedBindings)
        engine.addBinding (b);

    engine.setModifierSourceId (root.getProperty ("modifierSourceId", engine.getModifierSourceId()).toString());

    return juce::Result::ok();
}

juce::Result MappingProfileManager::loadFile (const juce::File& file, MappingEngine& engine, juce::String* outName)
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

juce::Result MappingProfileManager::saveFile (const juce::File& file, const MappingEngine& engine, const juce::String& name)
{
    const auto json = juce::JSON::toString (profileToVar (engine, name));
    if (! file.replaceWithText (json))
        return juce::Result::fail ("Could not write to file: " + file.getFullPathName());

    return juce::Result::ok();
}
