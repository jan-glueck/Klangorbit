#include "CanonicalInput.h"
#include <algorithm>

void CanonicalInputHub::addListener (Listener* listener)
{
    const juce::ScopedLock sl (lock);
    if (std::find (listeners.begin(), listeners.end(), listener) == listeners.end())
        listeners.push_back (listener);
}

void CanonicalInputHub::removeListener (Listener* listener)
{
    const juce::ScopedLock sl (lock);
    listeners.erase (std::remove (listeners.begin(), listeners.end(), listener), listeners.end());
}

void CanonicalInputHub::dispatch (const CanonicalInputEvent& event)
{
    // Snapshot under lock, call outside it -- see the header's own
    // comment on why (reentrant add/remove safety).
    std::vector<Listener*> snapshot;
    {
        const juce::ScopedLock sl (lock);
        snapshot = listeners;
    }

    for (auto* l : snapshot)
        l->canonicalInputReceived (event);
}
