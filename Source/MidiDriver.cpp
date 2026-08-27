#include "MidiDriver.h"

MidiDriver::MidiDriver()
{
    // Generous headroom for a block's worth of MIDI activity -- avoids a
    // reallocation from the audio thread (processMidiBuffer()) in the
    // common case. Not a hard real-time guarantee (push_back() can still
    // reallocate if this is ever exceeded), same "practical, not
    // strictly lock-free" tradeoff this project already accepts
    // elsewhere for audio-thread locking (see TrajectoryEngine's own
    // snapshotLock, taken from processBlock() too).
    queue.reserve (256);
}

void MidiDriver::processMidiBuffer (const juce::MidiBuffer& midiMessages)
{
    if (midiMessages.isEmpty())
        return;

    const juce::ScopedLock sl (queueLock);
    for (const auto metadata : midiMessages)
    {
        const auto& m = metadata.getMessage();

        if (m.isNoteOn())
            queue.push_back ({ PendingEvent::Type::NoteOn, m.getChannel(), m.getNoteNumber(), (float) m.getVelocity() });
        else if (m.isNoteOff())
            queue.push_back ({ PendingEvent::Type::NoteOff, m.getChannel(), m.getNoteNumber(), 0.0f });
        else if (m.isController())
            queue.push_back ({ PendingEvent::Type::ControlChange, m.getChannel(), m.getControllerNumber(), (float) m.getControllerValue() });
        else if (m.isPitchWheel())
            queue.push_back ({ PendingEvent::Type::PitchBend, m.getChannel(), 0, (float) m.getPitchWheelValue() });
    }
}

void MidiDriver::drainAndDispatch (CanonicalInputHub& hub)
{
    std::vector<PendingEvent> drained;
    {
        const juce::ScopedLock sl (queueLock);
        if (queue.empty())
            return;
        drained.swap (queue);
    }

    using Kind = CanonicalInputEvent::Kind;
    using Polarity = ParameterRegistry::Polarity;

    for (auto& e : drained)
    {
        switch (e.type)
        {
            case PendingEvent::Type::ControlChange:
                hub.dispatch ({ "Midi0.CC" + juce::String (e.number) + ".Ch" + juce::String (e.channel),
                                 Kind::Continuous, juce::jlimit (0.0f, 1.0f, e.rawValue / 127.0f), Polarity::Unipolar });
                break;

            case PendingEvent::Type::NoteOn:
                hub.dispatch ({ "Midi0.Note" + juce::String (e.number) + ".Ch" + juce::String (e.channel),
                                 Kind::Button, 1.0f, Polarity::Unipolar });
                break;

            case PendingEvent::Type::NoteOff:
                hub.dispatch ({ "Midi0.Note" + juce::String (e.number) + ".Ch" + juce::String (e.channel),
                                 Kind::Button, 0.0f, Polarity::Unipolar });
                break;

            case PendingEvent::Type::PitchBend:
                // 0..16383, center 8192 -> -1..1 (bipolar, matches a
                // pitch-bend wheel's own physical center-detent behavior).
                hub.dispatch ({ "Midi0.PitchBend.Ch" + juce::String (e.channel),
                                 Kind::Continuous, juce::jlimit (-1.0f, 1.0f, (e.rawValue - 8192.0f) / 8192.0f), Polarity::Bipolar });
                break;
        }
    }
}
