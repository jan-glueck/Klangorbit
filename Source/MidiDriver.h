#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "CanonicalInput.h"

/**
    Second driver on the canonical input layer (see CanonicalInput.h) --
    translates incoming MIDI Control Change, Note On/Off, and Pitch Bend
    messages into CanonicalInputEvents. Confirms the canonical layer's
    own promise: this is the only new code needed to add a MIDI source --
    ParameterRegistry, CanonicalInputHub, and MappingEngine (Learn mode,
    paging, mapping profiles) all needed zero changes.

    Only CC, Note On/Off, and Pitch Bend are translated -- the message
    types that actually appear on a real controller's knobs/faders/pads/
    keys and pitch strip. Aftertouch/Program Change/etc. are left out
    deliberately (not an oversight) -- can be added the same way if ever
    needed. Note velocity is NOT captured as a continuous value -- a Note
    On/Off is dispatched as a Button (value exactly 1.0/0.0), consistent
    with CanonicalInputEvent::Kind::Button's own documented contract, so
    velocity sensitivity is a disclosed scope decision, not a silent
    behavior a mapping consumer might assume is there.

    No device identity: JUCE (and every plugin host) delivers MIDI into
    ONE already-merged MidiBuffer per block, with no per-device
    information available to a plugin -- every sourceId uses the fixed
    "Midi0" prefix (matching the naming already used in
    MappingProfiles/schema/README.md's own examples), a placeholder for
    "this plugin's MIDI input" rather than a literal device index, same
    as GamepadDriver's own "Gamepad0" (single input, not yet multi-
    device, but not architected to make that impossible later).

    Threading: processMidiBuffer() is called from processBlock() -- the
    AUDIO thread, where JUCE/the host delivers MIDI to a plugin -- and
    only ever does a cheap, bounded push into a locked queue; it never
    calls CanonicalInputHub::dispatch() itself. drainAndDispatch() is
    called from KlangorbitProcessor's own control-rate timer (the
    MESSAGE thread, same one GamepadDriver already polls from) and does
    the actual translation + dispatch there instead. This keeps
    MappingEngine's own binding list (mutated by Learn-mode UI actions on
    the message thread) from ever being touched concurrently by two
    different threads.
*/
class MidiDriver
{
public:
    MidiDriver();

    void processMidiBuffer (const juce::MidiBuffer& midiMessages);
    void drainAndDispatch (CanonicalInputHub& hub);

private:
    struct PendingEvent
    {
        enum class Type { ControlChange, NoteOn, NoteOff, PitchBend };
        Type type;
        int channel; // 1-16
        int number;  // CC number or note number -- unused (0) for PitchBend
        float rawValue; // CC: 0..127; Note: velocity 0..127; PitchBend: 0..16383 (center 8192)
    };

    juce::CriticalSection queueLock;
    std::vector<PendingEvent> queue;
};
