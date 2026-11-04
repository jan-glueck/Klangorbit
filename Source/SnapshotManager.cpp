#include "SnapshotManager.h"
#include <algorithm>

SnapshotManager::Snapshot SnapshotManager::capture (const ParameterRegistry& registry, const juce::String& name,
                                                      const std::vector<juce::String>& categories)
{
    Snapshot snapshot;
    snapshot.name = name;
    snapshot.categories = categories;

    for (auto& descriptor : registry.all())
    {
        if (descriptor.scope != ParameterRegistry::Scope::Global
            && descriptor.scope != ParameterRegistry::Scope::SelectedObject)
            continue; // SpecificObject excluded -- see this namespace's own comment

        if (! categories.empty()
            && std::find (categories.begin(), categories.end(), descriptor.category) == categories.end())
            continue;

        snapshot.entries.push_back ({ descriptor.id, descriptor.getValue() });
    }

    return snapshot;
}

void SnapshotManager::recall (const Snapshot& snapshot, const ParameterRegistry& registry)
{
    for (auto& entry : snapshot.entries)
    {
        const auto* descriptor = registry.find (entry.id);
        if (descriptor != nullptr)
            descriptor->setValue (entry.value);
    }
}

juce::var SnapshotManager::snapshotToVar (const Snapshot& snapshot)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", currentSchemaVersion);
    root->setProperty ("name", snapshot.name);

    juce::Array<juce::var> categoriesArray;
    for (auto& c : snapshot.categories)
        categoriesArray.add (c);
    root->setProperty ("categories", categoriesArray);

    juce::Array<juce::var> entriesArray;
    for (auto& e : snapshot.entries)
    {
        auto* entryVar = new juce::DynamicObject();
        entryVar->setProperty ("id", e.id);
        entryVar->setProperty ("value", (double) e.value);
        entriesArray.add (juce::var (entryVar));
    }
    root->setProperty ("entries", entriesArray);

    return juce::var (root);
}

juce::Result SnapshotManager::snapshotFromVar (const juce::var& root, Snapshot& out)
{
    if (! root.isObject())
        return juce::Result::fail ("Snapshot is not a valid JSON object.");

    const int schemaVersion = (int) root.getProperty ("schemaVersion", 0);
    if (schemaVersion != currentSchemaVersion)
        return juce::Result::fail ("Unsupported snapshot schemaVersion: " + juce::String (schemaVersion)
                                    + " (this build supports " + juce::String (currentSchemaVersion) + ").");

    Snapshot parsed;
    parsed.name = root.getProperty ("name", juce::var()).toString();

    if (auto* categoriesArray = root.getProperty ("categories", juce::var()).getArray())
        for (auto& c : *categoriesArray)
            parsed.categories.push_back (c.toString());

    auto* entriesArray = root.getProperty ("entries", juce::var()).getArray();
    if (entriesArray == nullptr)
        return juce::Result::fail ("Snapshot is missing its \"entries\" array.");

    parsed.entries.reserve ((size_t) entriesArray->size());
    for (auto& element : *entriesArray)
    {
        if (! element.isObject())
            return juce::Result::fail ("Snapshot contains an entry that isn't a JSON object.");

        const auto id = element.getProperty ("id", juce::var()).toString();
        if (id.isEmpty())
            return juce::Result::fail ("Snapshot contains an entry with a missing id.");

        Entry entry;
        entry.id = id;
        entry.value = (float) (double) element.getProperty ("value", 0.0);
        parsed.entries.push_back (std::move (entry));
    }

    out = std::move (parsed);
    return juce::Result::ok();
}

juce::Result SnapshotManager::loadFile (const juce::File& file, Snapshot& out, juce::String* outName)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("File not found: " + file.getFullPathName());

    auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return juce::Result::fail ("File does not contain valid JSON: " + file.getFullPathName());

    auto result = snapshotFromVar (parsed, out);
    if (result.wasOk() && outName != nullptr)
        *outName = parsed.getProperty ("name", file.getFileNameWithoutExtension()).toString();

    return result;
}

juce::Result SnapshotManager::saveFile (const juce::File& file, const Snapshot& snapshot)
{
    const auto json = juce::JSON::toString (snapshotToVar (snapshot));
    if (! file.replaceWithText (json))
        return juce::Result::fail ("Could not write to file: " + file.getFullPathName());

    return juce::Result::ok();
}
