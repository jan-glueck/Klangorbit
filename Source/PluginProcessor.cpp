#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GrainRenderer.h"
#include "GrainDoppler.h"
#include "MuteSoloLogic.h"
#include "MappingProfileManager.h"
#include "BinaryData.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Cartesian (x=front, y=left, z=up) -> spherical coordinates for the encoder.
    void cartesianToSpherical (Vec3 pos, float& azimuth, float& elevation, float& distance)
    {
        distance = juce::jmax (pos.length(), 0.001f);
        azimuth  = std::atan2 (pos.y, pos.x);
        elevation = std::asin (juce::jlimit (-1.0f, 1.0f, pos.z / distance));
    }

    // Writes an embedded BinaryData asset to a cached temp file, skipping
    // the write if a file of the expected size is already there -- avoids
    // re-writing SADIE's ~36MB on every plugin instantiation once one
    // instance has already written it on this machine. libmysofa needs a
    // real filesystem path (mysofa_open takes a path, not a buffer), so
    // the embedded bytes have to land on disk somewhere before
    // HrtfDataset can open them -- see the class comment.
    juce::File writeBinaryDataToTempFileIfNeeded (const char* fileName, const char* data, int size)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("Klangorbit").getChildFile (fileName);
        if (file.existsAsFile() && file.getSize() == (juce::int64) size)
            return file;

        file.getParentDirectory().createDirectory();
        file.replaceWithData (data, (size_t) size);
        return file;
    }

}

KlangorbitProcessor::BusesProperties KlangorbitProcessor::makeBusLayout()
{
    // Stereo, not raw Ambisonics, for every format -- raw B-format is
    // silent/unusable without an external decoder, so a fresh plugin
    // instance used to produce no audible output at all until the user
    // either wired up an external decoder or switched Output Format
    // manually. Stereo is audible immediately, and (for AU specifically)
    // is also a NAMED layout Logic recognizes -- see the constructor's
    // own AU-specific comment for why that matters beyond this default.
    return BusesProperties()
        .withInput  ("Live Inputs", juce::AudioChannelSet::discreteChannels (SAPOC_MAX_LIVE_INPUTS), true)
        .withOutput ("Ambisonics", AmbisonicsDecoder::outputChannelSetFor (AmbisonicsDecoder::Mode::Stereo), true);
}

KlangorbitProcessor::KlangorbitProcessor()
    : juce::AudioProcessor (makeBusLayout())
{
    // AU (Logic Pro etc.): switch the default input bus away from
    // makeBusLayout()'s own fixed 8-channel discrete "Live Inputs"
    // default (unconditionally the same for every format there, since
    // wrapperType isn't known yet at that point in construction -- see
    // that method's own comment) to Stereo, a NAMED layout Logic
    // recognizes. Not just cosmetic: auval's own "Default Layout must be
    // published as a supported layout tag" check requires this --
    // discreteChannels(N) has no corresponding named CoreAudio
    // AudioChannelLayoutTag at all, so the plugin's OWN starting state
    // can never satisfy that check while it stays the default, regardless
    // of what isBusesLayoutSupported() accepts (confirmed empirically: an
    // earlier attempt that only widened isBusesLayoutSupported() to also
    // ACCEPT discreteChannels(numLiveInputs) did NOT fix this -- accepting
    // a layout and it having a publishable tag are different things).
    // isBusesLayoutSupported() below is what actually lets a user pick
    // Mono/Quad/7.1 too, by inserting Klangorbit on a matching Logic
    // track; this is only about what the plugin starts up as, before any
    // host negotiation happens. The OUTPUT bus here is left as whatever
    // getBusesLayout() already reports -- makeBusLayout()'s own default
    // is Stereo for every format now (see that method's own comment), so
    // this ends up requesting (Stereo in, Stereo out), a self-consistent,
    // Logic-friendly starting point without needing to touch the output
    // side at all. isBusesLayoutSupported()'s AU output-side branch (see
    // below) doesn't depend on decoder.getMode() the way the VST3/
    // Standalone branch does, so the exact ordering of this call relative
    // to decoder.setMode() below doesn't matter for the output side.
    if (wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_AudioUnitv3)
    {
        auto auDefaultLayout = getBusesLayout();
        if (! auDefaultLayout.inputBuses.isEmpty())
            auDefaultLayout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        setBusesLayout (auDefaultLayout);
    }

    // Stereo, matching makeBusLayout()'s own new default -- see that
    // method's own comment. ambisonicsOrderFor(Stereo) is 3, the fixed
    // internal order every decoded mode already uses regardless of the
    // target format's own channel count.
    encoder.setOrder (AmbisonicsDecoder::ambisonicsOrderFor (AmbisonicsDecoder::Mode::Stereo));
    decoder.setMode (AmbisonicsDecoder::Mode::Stereo);

    // See writeBinaryDataToTempFileIfNeeded()'s own comment. Cheap/
    // sample-rate-independent, so done once here rather than deferred to
    // first Binaural use -- actually LOADING these into an HrtfDataset
    // (sample-rate-dependent resampling) and preparing binauralDecoder
    // from them happens lazily instead, see prepareBinauralDecoder()/
    // setDecoderMode()/prepareToPlay().
    kemarSofaTempFile = writeBinaryDataToTempFileIfNeeded ("kemar_44100.sofa", BinaryData::kemar_44100_sofa, BinaryData::kemar_44100_sofaSize);
    sadieSofaTempFile = writeBinaryDataToTempFileIfNeeded ("sadie_d1_44100.sofa", BinaryData::sadie_d1_44100_sofa, BinaryData::sadie_d1_44100_sofaSize);
    ku100SofaTempFile = writeBinaryDataToTempFileIfNeeded ("ku100_48000.sofa", BinaryData::ku100_48000_sofa, BinaryData::ku100_48000_sofaSize);

    // Only object 0 starts active (input channel 0). Further objects are
    // added via the GUI (TrajectoryEngine::activateObject()) -- the input
    // channel assignment stays the simple 1:1 mapping of object index ==
    // channel index.
    trajectoryEngine.activateObject (0);

    previousGainsPerObject.resize ((size_t) numLiveInputs);
    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);

    muteRampGain.assign ((size_t) numLiveInputs, 1.0f); // start unmuted/audible
    sourceMuteRampGain.assign ((size_t) numLiveInputs, 1.0f);

    propagationPerObject.resize ((size_t) numLiveInputs);
    wasActiveLastBlock.assign ((size_t) numLiveInputs, false);

    for (auto& wh : grainRingBufferWriteHead)
        wh.store (0, std::memory_order_relaxed);

    buildParameterRegistry();

    // Wires the mapping engine into the canonical input hub (so it sees
    // every event any driver posts -- currently just gamepadDriver) and
    // gives gamepadDriver a way to ask "is the left stick explicitly
    // bound right now?" (see GamepadDriver::setLeftStickOverrideQuery()'s
    // own comment on why this exists).
    canonicalInputHub.addListener (&mappingEngine);
    gamepadDriver.setLeftStickOverrideQuery ([this] (const juce::String& sourceId)
    {
        return mappingEngine.hasBindingFor (sourceId, mappingEngine.getCurrentBank());
    });

    // Best-effort: load the shipped factory default mapping profile if
    // this build has one (see SAPOC_MAPPING_PROFILES_FACTORY_DIR's own
    // comment in CMakeLists.txt -- a local-dev-only convenience path, not
    // present in an installed plugin). Ships empty by design (see
    // MappingProfiles/factory/default.json's own comment) -- absence or
    // failure to load is silently fine, mappingEngine simply starts with
    // no bindings either way, same end state.
   #if defined (SAPOC_MAPPING_PROFILES_FACTORY_DIR)
    {
        const juce::File defaultProfile = juce::File (SAPOC_MAPPING_PROFILES_FACTORY_DIR).getChildFile ("default.json");
        if (defaultProfile.existsAsFile())
            MappingProfileManager::loadFile (defaultProfile, mappingEngine);
    }
   #endif

    // Starts the control-rate simulation loop -- see the class comment in
    // PluginProcessor.h for why this runs from here rather than the
    // editor. 90Hz matches the rate the editor's own timer previously
    // drove TrajectoryEngine/GrainCloud updates at.
    lastControlRateTimerMs = juce::Time::getMillisecondCounter();
    startTimerHz (90);
}

KlangorbitProcessor::~KlangorbitProcessor()
{
    // Explicit, before any other member starts tearing down -- same
    // ordering reasoning KlangorbitEditor's own destructor already
    // established for its (now editor-only) timer. juce::Timer's own
    // destructor would stop it safely too, but stopping it first here
    // means timerCallback() can never fire mid-teardown of this class's
    // own members (trajectoryEngine, grainRandom, etc.).
    stopTimer();

    // Explicit, before mappingEngine itself is destroyed below (member
    // destruction order), so canonicalInputHub never holds a dangling
    // listener pointer even for the brief window between the two
    // members' destructors -- nothing would actually dereference it
    // there (the timer is already stopped, nothing else calls dispatch()
    // during teardown), but there's no reason to leave a dangling
    // pointer sitting around when removing it costs nothing.
    canonicalInputHub.removeListener (&mappingEngine);
}

void KlangorbitProcessor::timerCallback()
{
    // Moved verbatim from what used to be KlangorbitEditor::timerCallback()
    // (see the class comment in PluginProcessor.h for why) -- same two
    // calls, same control rate, same dt clamping. The editor's own timer
    // (still running, at the same rate, when an editor exists) no longer
    // touches TrajectoryEngine/GrainCloud at all -- only this one does now,
    // so there is no double-update.
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jlimit (0.0, 0.1, (double) (now - lastControlRateTimerMs) / 1000.0); // clamp against outliers
    lastControlRateTimerMs = now;

    // Polled BEFORE trajectoryEngine.update() below, so this same tick's
    // integration step already reflects the freshly-read stick position
    // (lowest latency, one control-rate tick) rather than lagging by one.
    // See GamepadDriver's own class comment for why this happens here
    // (message-thread timer) and not processBlock().
    gamepadDriver.poll (canonicalInputHub, selectedObjectIndex, dt);

    // Drains whatever CC/Note/PitchBend messages processBlock() queued
    // since the last tick and dispatches them here -- see MidiDriver's
    // own class comment for why (MIDI arrives on the audio thread, but
    // dispatch/mapping application needs to happen on the message
    // thread, same as gamepad polling above). oscDriver needs no
    // equivalent call -- it dispatches directly from its own JUCE-
    // marshaled message-thread callback whenever a packet arrives.
    midiDriver.drainAndDispatch (canonicalInputHub);

    trajectoryEngine.update (dt);

    // GrainCloud: control-rate update, same loop/rate as TrajectoryEngine
    // above. A single global spawn budget is shared across all clouds so
    // the total number of simultaneously active grains never exceeds
    // maxConcurrentGrainsGlobal, no matter how many objects are
    // granulating at once -- each active grain costs a full Ambisonics
    // encode pass in processBlock() below.
    int globalGrainBudget = maxConcurrentGrainsGlobal;
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
        globalGrainBudget -= trajectoryEngine.getGrainCloud (i).getNumActiveGrains();

    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        auto& obj = trajectoryEngine.getObject (i);
        if (obj.inputChannel < 0)
            continue; // no active parent -- freeze this cloud instead of updating it with a meaningless position

        auto& cloud = trajectoryEngine.getGrainCloud (i);
        cloud.setRingBufferContext (getGrainRingBufferWriteHead (i), getSampleRate());
        cloud.update (dt, obj.position, obj.velocity, globalGrainBudget, grainRandom);
    }
}

void KlangorbitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSizeSamples = samplesPerBlock;
    encoder.prepare (sampleRate, samplesPerBlock);
    decoder.prepare (sampleRate);

    // Only rebuild binauralDecoder here if Binaural is actually the
    // active mode -- reloading a dataset from disk (SADIE II is ~36MB) on
    // every prepareToPlay() regardless of mode would add needless latency
    // to ordinary playback start/stop for the common case where Binaural
    // isn't even selected. If the user switches INTO Binaural later,
    // setDecoderMode() below does this instead, at that point using
    // whatever sampleRate/currentBlockSizeSamples is current then.
    if (decoder.getMode() == AmbisonicsDecoder::Mode::Binaural)
        prepareBinauralDecoder();

    ambiScratch.setSize (ambiScratchChannels, samplesPerBlock);

    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);

    for (auto& p : propagationPerObject)
        p.prepare (sampleRate, samplesPerBlock);
    std::fill (wasActiveLastBlock.begin(), wasActiveLastBlock.end(), false);
    std::fill (muteRampGain.begin(), muteRampGain.end(), 1.0f);
    std::fill (sourceMuteRampGain.begin(), sourceMuteRampGain.end(), 1.0f);

    propagationScratch.setSize (1, samplesPerBlock);

    grainRingBuffers.resize ((size_t) numLiveInputs);
    const int ringBufferSamples = juce::jmax (1, (int) (grainRingBufferSeconds * sampleRate));
    for (auto& ring : grainRingBuffers)
    {
        ring.setSize (1, ringBufferSamples);
        ring.clear();
    }
    for (auto& wh : grainRingBufferWriteHead)
        wh.store (0, std::memory_order_relaxed);

    grainAudioState.assign ((size_t) numLiveInputs, std::vector<GrainAudioState> ((size_t) grainPoolSizePerCloud));
    for (auto& perObject : grainAudioState)
    {
        for (auto& state : perObject)
        {
            state.lastSeenGeneration = -1;
            state.samplesPlayed = 0;
            state.previousChannelGains.assign ((size_t) encoder.getNumChannels(), 0.0f);
        }
    }

    grainScratch.setSize (1, samplesPerBlock);
}

void KlangorbitProcessor::releaseResources() {}

bool KlangorbitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto inSet = layouts.getMainInputChannelSet();

    // AU (Logic Pro etc.) only, detected at RUNTIME via the inherited
    // wrapperType member -- NOT #if JucePlugin_Build_AU, which would be
    // wrong here: this file is compiled exactly once into
    // libKlangorbit_SharedCode.a and that single compiled object is
    // linked into all three format targets (confirmed via `find build
    // -iname PluginProcessor.cpp.o`, only one exists), so a compile-time
    // macro would silently apply to VST3/Standalone too.
    //
    // VST3/Standalone (the existing, working Reaper workflow) keep the
    // exact original behavior below unconditionally -- a fixed
    // discreteChannels(numLiveInputs) bus, never touched by this branch.
    //
    // AU specifically: the fixed 8-channel discrete "Live Inputs" bus
    // doesn't match ANY of Logic Pro's standard track/bus formats (Mono=1,
    // Stereo=2, Quad=4, 7.1=8-but-a-NAMED-layout-not-raw-discrete), so
    // Logic's own per-track Audio-Unit filtering excluded Klangorbit
    // everywhere -- confirmed directly by the user (didn't show up on
    // Mono, Stereo, or any other track type). JUCE's AU wrapper builds the
    // AU's kAudioUnitProperty_SupportedNumChannels list by probing
    // checkBusesLayoutSupported() (-> this function) across a matrix of
    // standard channel-count layouts for both input and output (see
    // AudioUnitHelpers::getAUChannelInfo(), JUCE/modules/
    // juce_audio_processors_headless/format_types/juce_AU_Shared.h) -- so
    // accepting more input layouts here directly makes Logic offer
    // Klangorbit on more track types. NAMED layouts (mono()/stereo()/
    // quadraphonic()/create7point1()), not raw discreteChannels(N): a
    // host's own format-matching keys off the named identity, not just
    // the channel count (same reasoning AmbisonicsDecoder::
    // outputChannelSetFor() already documents for its own real-speaker
    // formats) -- likely exactly why even Logic's 8-channel 7.1 tracks
    // didn't show Klangorbit today, since discreteChannels(8) isn't
    // "7.1" as far as Logic's own layout-tag matching is concerned.
    //
    // Accepting FOUR input layouts at once here is safe in a way widening
    // the OUTPUT-side check below was NOT (see that check's own comment
    // on the Reaper regression it fixes): AmbisonicsDecoder::decode()
    // needs to know exactly which mode is active to use the right decode
    // matrix, so a host settling on the wrong OUTPUT config produced
    // silence. Reading live input has no equivalent stored-state
    // dependency -- it already self-adapts every block to however many
    // channels are ACTUALLY present (see processBlock()'s own numInCh
    // clamp, and every per-object read already gated by obj.inputChannel
    // < numInCh) -- so there's no "wrong config" for a host to get stuck
    // on here, regardless of which of the four it settles on.
    if (wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_AudioUnitv3)
    {
        // discreteChannels(numLiveInputs) is included here TOO (not
        // replaced) -- makeBusLayout()'s own construction-time default
        // bus is still that fixed 8-discrete-channel layout for every
        // format (wrapperType isn't known yet at that point, see that
        // method's own comment), so it must stay one of the layouts this
        // function accepts, or auval fails with "Default Layout is not
        // published as a supported layout tag" (confirmed empirically --
        // this exact error appeared before this line was added). It's
        // effectively inert for the actual Mono/Stereo/Quad/7.1-track
        // matching this branch exists for (a raw 8-channel layout doesn't
        // match any of those track types' own formats either), just a
        // valid fallback so the plugin's own starting state is
        // self-consistent.
        if (inSet != juce::AudioChannelSet::mono()
            && inSet != juce::AudioChannelSet::stereo()
            && inSet != juce::AudioChannelSet::quadraphonic()
            && inSet != juce::AudioChannelSet::create7point1()
            && inSet != juce::AudioChannelSet::discreteChannels (numLiveInputs))
            return false;
    }
    else
    {
        if (inSet != juce::AudioChannelSet::discreteChannels (numLiveInputs))
            return false;
    }

    const auto outSet = layouts.getMainOutputChannelSet();

    // AU: accept any of the 9 AmbisonicsDecoder::Mode formats that are
    // actually usable in Logic Pro -- a NAMED JUCE layout (so Logic's own
    // layout-tag matching recognizes it -- discreteChannels(N) never gets
    // a publishable tag, confirmed empirically) AND <= 12 channels
    // (Logic's own ceiling, 7.1.4 -- confirmed by the user). Raw
    // Ambisonics Order 1/2/3 (unnamed; Order 3 alone already exceeds
    // 12ch), Octophonic, and CircularArray (both unnamed) are excluded
    // here unconditionally -- OutputPanel greys them out of its own
    // dropdown too when running as AU (see isOutputModeAvailable()), so
    // they're never actually reachable, but this function stays correct
    // on its own terms regardless.
    //
    // Deliberately NOT scoped to "only the current mode" the way the
    // VST3/Standalone branch below is: this accepts several layouts at
    // once so JUCE's AU wrapper can discover all of them (see
    // AudioUnitHelpers::getAUChannelInfo() probing this function across a
    // matrix of candidates, JUCE/modules/juce_audio_processors_headless/
    // format_types/juce_AU_Shared.h), letting Logic offer Klangorbit on
    // Stereo/5.1/7.1/Atmos-bed tracks alike. This does NOT reintroduce
    // the Reaper regression the VST3/Standalone restriction below exists
    // to prevent: this plugin never asks an AU host to change the output
    // channel COUNT after the initial negotiation (see
    // setDecoderMode()'s own AU-specific early return, below) -- Logic
    // fixes that count once, at insertion, and switching between modes
    // that fit within it (via isOutputModeAvailable()) works internally,
    // using fewer channels than what's available rather than requesting
    // a different bus (see AmbisonicsDecoder::decode()'s own channel-
    // clearing fix for the unused remainder). There is no live
    // renegotiation attempt here for a host to get stuck on.
    if (wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_AudioUnitv3)
    {
        return outSet == juce::AudioChannelSet::stereo()          // Stereo, Binaural
            || outSet == juce::AudioChannelSet::quadraphonic()    // Quad
            || outSet == juce::AudioChannelSet::create5point1()
            || outSet == juce::AudioChannelSet::create7point1()
            || outSet == juce::AudioChannelSet::create5point1point2()
            || outSet == juce::AudioChannelSet::create5point1point4()
            || outSet == juce::AudioChannelSet::create7point1point2()
            || outSet == juce::AudioChannelSet::create7point1point4();
    }

    // VST3/Standalone: output must match the CURRENTLY ACTIVE decoder
    // mode's layout -- NOT any of AmbisonicsDecoder::Mode's other fixed
    // target formats. This used to accept all 13 (deliberately, so a host
    // could switch modes via its own native bus-negotiation UI instead of
    // only this plugin's own "Output..." picker) -- reverted after a
    // real-world regression: some hosts (confirmed: Reaper) probe several
    // candidate layouts on first insertion and simply settle on whichever
    // one this function accepts first (often plain Stereo, a common
    // host-side default probe), permanently stuck there regardless of
    // which mode the decoder itself -- and the "Output..." window's own
    // dropdown -- actually show as selected. Accepting only the current
    // mode's own layout closes that gap: a host has exactly one valid
    // layout to negotiate to, the same "no ambiguity" guarantee the
    // plugin's original single-format (Ambisonics-only) version always
    // had, which never exhibited this symptom. setDecoderMode() switching
    // modes still works from OUR OWN UI: it calls decoder.setMode(newMode)
    // BEFORE calling setBusesLayout() below, so by the time that call's
    // internal validation reaches this function, decoder.getMode()
    // already equals the NEW mode -- the layout being requested and the
    // "currently active" mode this function checks against agree.
    if (decoder.getMode() == AmbisonicsDecoder::Mode::CircularArray)
    {
        // CircularArray's own channel count is independently adjustable
        // (setCircularArraySpeakerCount()) without a decoder mode change,
        // so accept any count in its supported range while this mode is
        // active, not just the exact count currently configured.
        for (int n = AmbisonicsDecoder::minCircularSpeakers; n <= AmbisonicsDecoder::maxCircularSpeakers; ++n)
            if (outSet == juce::AudioChannelSet::discreteChannels (n))
                return true;
        return false;
    }

    return outSet == AmbisonicsDecoder::outputChannelSetFor (decoder.getMode());
}

bool KlangorbitProcessor::isOutputModeAvailable (AmbisonicsDecoder::Mode mode) const
{
    if (wrapperType != wrapperType_AudioUnit && wrapperType != wrapperType_AudioUnitv3)
        return true;

    using Mode = AmbisonicsDecoder::Mode;
    const bool namedAndInRange = mode == Mode::Stereo || mode == Mode::Binaural
        || mode == Mode::Quad || mode == Mode::Surround5_1 || mode == Mode::Surround7_1
        || mode == Mode::Atmos5_1_2 || mode == Mode::Atmos5_1_4
        || mode == Mode::Atmos7_1_2 || mode == Mode::Atmos7_1_4;
    if (! namedAndInRange)
        return false;

    return AmbisonicsDecoder::numOutputChannels (mode) <= getTotalNumOutputChannels();
}

void KlangorbitProcessor::setDecoderMode (AmbisonicsDecoder::Mode newMode)
{
    if (newMode == decoder.getMode())
        return;

    // OutputPanel's own combo already greys out anything
    // isOutputModeAvailable() rejects, so this shouldn't normally be
    // reachable -- a defensive no-op guard regardless (e.g. if something
    // else ever calls setDecoderMode() directly).
    if (! isOutputModeAvailable (newMode))
        return;

    encoder.setOrder (AmbisonicsDecoder::ambisonicsOrderFor (newMode));
    decoder.setMode (newMode);

    // Switching INTO Binaural: (re)build binauralDecoder from whichever
    // dataset is currently selected, at the current sampleRate/block size
    // -- see prepareToPlay()'s own comment on why this isn't done
    // unconditionally on every prepareToPlay() call instead.
    if (newMode == AmbisonicsDecoder::Mode::Binaural)
        prepareBinauralDecoder();

    // A changed Ambisonics order invalidates any in-flight gain ramp
    // target -- reset rather than resize-and-keep.
    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);
    for (auto& perObject : grainAudioState)
        for (auto& state : perObject)
            state.previousChannelGains.assign ((size_t) encoder.getNumChannels(), 0.0f);

    // AU: never request a new output channel COUNT after the initial
    // negotiation -- Logic fixes that once, at insertion (see
    // isBusesLayoutSupported()'s own AU branch), and isOutputModeAvailable()
    // above already guarantees newMode's own channel count fits within
    // it. AmbisonicsDecoder::decode() (its own channel-clearing fix)
    // handles using fewer channels than what's negotiated; there is
    // nothing to renegotiate here, and no live re-negotiation attempt for
    // Logic to get stuck on the way Reaper once did (see below).
    if (wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_AudioUnitv3)
        return;

    // Best-effort request for the host to renegotiate the output bus to
    // match. This JUCE version's VST3 wrapper has no dedicated "please
    // rescan my bus layout" restart flag a plugin can raise on its own
    // (Vst::kIoChanged is never sent) -- updateHostDisplay() below can at
    // most mark the plugin's non-parameter state dirty, which some hosts
    // (Reaper, confirmed) use as a cue to recheck buses live; others will
    // only pick up the new layout the next time they call
    // checkBusesLayoutSupported() themselves (e.g. on project reload, or
    // after the plugin is removed and reinserted). Calling setBusesLayout()
    // here still matters even then: it keeps this AudioProcessor's own
    // reported bus layout consistent with the active decode mode, so a
    // host that DOES rescan sees the right answer immediately. This
    // tradeoff (per-mode bus layouts, live-switch reliability varies by
    // host) was chosen deliberately over a fixed always-16-channel bus --
    // see CHANGELOG.
    auto layouts = getBusesLayout();
    if (! layouts.outputBuses.isEmpty())
        layouts.outputBuses.getReference (0) = AmbisonicsDecoder::outputChannelSetFor (newMode, decoder.getCircularArraySpeakerCount());
    setBusesLayout (layouts);
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void KlangorbitProcessor::setCircularArraySpeakerCount (int n)
{
    const int clamped = juce::jlimit (AmbisonicsDecoder::minCircularSpeakers, AmbisonicsDecoder::maxCircularSpeakers, n);
    if (clamped == decoder.getCircularArraySpeakerCount())
        return;

    decoder.setCircularArraySpeakerCount (clamped); // rebuilds the decode matrix live if CircularArray is already active

    // Only CircularArray's own output channel count depends on this --
    // the encoder side (ambiScratch, gain-ramp state) stays fixed at
    // order 3 regardless, so unlike setDecoderMode() above there is no
    // ramp state to reset here.
    if (decoder.getMode() != AmbisonicsDecoder::Mode::CircularArray)
        return;

    // Same best-effort bus renegotiation as setDecoderMode() -- see its comment.
    auto layouts = getBusesLayout();
    if (! layouts.outputBuses.isEmpty())
        layouts.outputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (clamped);
    setBusesLayout (layouts);
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void KlangorbitProcessor::prepareBinauralDecoder()
{
    HrtfDataset* dataset = nullptr;
    juce::File sourceFile;
    switch (binauralDatasetSource)
    {
        case BinauralDatasetSource::Kemar:      dataset = &kemarDataset;  sourceFile = kemarSofaTempFile;  break;
        case BinauralDatasetSource::SadieD1:    dataset = &sadieDataset;  sourceFile = sadieSofaTempFile;  break;
        case BinauralDatasetSource::Ku100:      dataset = &ku100Dataset;  sourceFile = ku100SofaTempFile;  break;
        case BinauralDatasetSource::CustomFile: dataset = &customDataset; sourceFile = customSofaFilePath; break;
    }
    if (dataset == nullptr)
        return;

    // Re-opens via libmysofa at the CURRENT sampleRate every time this is
    // called (not just once) -- HrtfDataset::load()'s own sampleRate
    // argument is what libmysofa resamples the stored HRIRs to, so a
    // sample-rate change requires reloading, not just re-preparing
    // binauralDecoder against a stale resample. If this fails (or
    // sourceFile doesn't exist yet, e.g. CustomFile before the user has
    // picked one), dataset->isLoaded() is false and binauralDecoder.
    // prepare() below just leaves itself unprepared -- decode() clears
    // its output rather than crashing or playing stale data, see both
    // classes' own comments.
    if (sourceFile.existsAsFile())
    {
        juce::String error;
        dataset->load (sourceFile, currentSampleRate, error);
    }

    binauralDecoder.prepare (*dataset, currentSampleRate, currentBlockSizeSamples);
}

void KlangorbitProcessor::setBinauralDataset (BinauralDatasetSource newSource)
{
    if (newSource == BinauralDatasetSource::CustomFile && ! customSofaFilePath.existsAsFile())
        return; // no custom file loaded yet -- see the header's own comment, don't silently go silent
    if (newSource == binauralDatasetSource)
        return;

    binauralDatasetSource = newSource;
    prepareBinauralDecoder();
}

bool KlangorbitProcessor::loadCustomSofaFile (const juce::File& file)
{
    juce::String error;
    if (! customDataset.load (file, currentSampleRate, error))
        return false; // previously active dataset stays untouched, see the header's own comment

    customSofaFilePath = file;
    binauralDatasetSource = BinauralDatasetSource::CustomFile;
    binauralDecoder.prepare (customDataset, currentSampleRate, currentBlockSizeSamples);
    return true;
}

void KlangorbitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    // Cheap, bounded queue push only -- see MidiDriver's own class
    // comment on why the actual translation/dispatch happens later, from
    // timerCallback() on the message thread, not here.
    midiDriver.processMidiBuffer (midiMessages);

    const int numSamples = buffer.getNumSamples();
    // getTotalNumInputChannels() (the currently negotiated main input bus
    // width), not just buffer.getNumChannels() -- this used to be
    // equivalent (input was ALWAYS forced to exactly numLiveInputs
    // channels for every format, and JUCE's shared in-place buffer is
    // sized to max(totalNumInputChannels, totalNumOutputChannels), always
    // >= numLiveInputs as a result). Now that AU can legitimately
    // negotiate FEWER real input channels (Mono/Stereo/Quad, see
    // isBusesLayoutSupported()) while still having a much wider output
    // bus (e.g. 16, raw Ambisonics), buffer.getNumChannels() would be 16
    // even with only 2 real input channels -- without this extra bound,
    // channels 2-15 (uninitialized/output-purposed buffer memory) would
    // be silently treated as valid extra live-input channels.
    const int numInCh    = juce::jmin (numLiveInputs, getTotalNumInputChannels(), buffer.getNumChannels());

    // Preserve the input channels before overwriting -- the output buffer
    // is the same memory as the input (in-place), so copy first.
    juce::AudioBuffer<float> inputCopy (numInCh, numSamples);
    for (int ch = 0; ch < numInCh; ++ch)
        inputCopy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    buffer.clear();

    // Raw passthrough modes encode straight into the output buffer, exactly
    // as before this decoder feature existed (zero added overhead, see
    // AmbisonicsDecoder's class comment). Every other mode encodes into
    // ambiScratch instead, decoded down to the real output once both
    // rendering passes below are done.
    const bool rawPassthrough = AmbisonicsDecoder::isRawPassthrough (decoder.getMode());
    juce::AudioBuffer<float>& encodeTarget = rawPassthrough ? buffer : ambiScratch;
    if (! rawPassthrough)
        ambiScratch.clear();

    std::vector<TrajectoryEngine::Snapshot> snapshot;
    trajectoryEngine.getSnapshot (snapshot);

    // Read-only access to scene-wide acoustic parameters (speedOfSound,
    // temperature, humidity, pressure, wind) from the audio thread. Same
    // established pattern as obj.gain/obj.inputChannel below: plain,
    // unsynchronized reads of values the message thread writes -- see the
    // TrajectoryEngine class comment and README known limitations.
    const auto& sceneSettings = trajectoryEngine.getSceneSettings();
    float* propagated = propagationScratch.getWritePointer (0);

    // Solo: if ANY active object is soloed, every object that is NOT
    // soloed goes silent (classic non-exclusive DAW solo -- several
    // objects can be soloed together and all stay audible). Computed once
    // per block, not per object.
    bool anySoloed = false;
    for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
    {
        auto& o = trajectoryEngine.getObject (i);
        if (o.inputChannel >= 0 && o.soloed) { anySoloed = true; break; }
    }

    for (int objIdx = 0; objIdx < trajectoryEngine.getNumObjects(); ++objIdx)
    {
        auto& obj = trajectoryEngine.getObject (objIdx);
        const bool active = obj.inputChannel >= 0 && obj.inputChannel < numInCh
                             && objIdx < (int) snapshot.size() && snapshot[(size_t) objIdx].active;

        // Keep this object's grain-cloud ring buffer filled with its live
        // input every block, regardless of whether granulation is
        // currently enabled -- so turning it on always has recent history
        // to read from, no silent warm-up period.
        if (active)
        {
            auto& ring = grainRingBuffers[(size_t) objIdx];
            const int ringSize = ring.getNumSamples();
            if (ringSize > 0)
            {
                int head = grainRingBufferWriteHead[(size_t) objIdx].load (std::memory_order_relaxed);
                const float* src = inputCopy.getReadPointer (obj.inputChannel);
                float* dst = ring.getWritePointer (0);

                for (int i = 0; i < numSamples; ++i)
                {
                    dst[head] = src[i];
                    head = (head + 1) % ringSize;
                }
                grainRingBufferWriteHead[(size_t) objIdx].store (head, std::memory_order_relaxed);
            }
        }

        if (! active)
        {
            wasActiveLastBlock[(size_t) objIdx] = false;
            continue;
        }

        // Solo/mute: own mute always wins over solo (an object can't be
        // simultaneously "definitely silent" and "definitely audible");
        // otherwise silenced if some other object is soloed and this one
        // isn't. Smoothly ramped over muteRampSeconds rather than switched
        // instantly, so toggling never clicks (the encoder's own
        // per-sample gain interpolation from previousChannelGains smooths
        // it further still, within the ramp).
        const bool effectivelyMuted = MuteSoloLogic::isEffectivelyMuted (obj.muted, obj.soloed, anySoloed);
        const float muteTarget = effectivelyMuted ? 0.0f : 1.0f;
        auto& ramp = muteRampGain[(size_t) objIdx];
        const float rampStep = (float) ((double) numSamples / juce::jmax (1.0, currentSampleRate)) / muteRampSeconds;
        if (ramp < muteTarget)      ramp = juce::jmin (muteTarget, ramp + rampStep);
        else if (ramp > muteTarget) ramp = juce::jmax (muteTarget, ramp - rampStep);

        // GrainCloudSettings::sourceMuted: mutes just this object's own dry
        // audio (this loop), never its grains (the separate loop below,
        // which only ever looks at muted/soloed above) -- lets the grains
        // be heard in isolation. Same ramped treatment, own ramp array so
        // it can't affect the grain loop's muteRampGain read.
        const bool sourceMuted = trajectoryEngine.getGrainCloud (objIdx).getSettings().sourceMuted;
        const float sourceMuteTarget = sourceMuted ? 0.0f : 1.0f;
        auto& sourceRamp = sourceMuteRampGain[(size_t) objIdx];
        if (sourceRamp < sourceMuteTarget)      sourceRamp = juce::jmin (sourceMuteTarget, sourceRamp + rampStep);
        else if (sourceRamp > sourceMuteTarget) sourceRamp = juce::jmax (sourceMuteTarget, sourceRamp - rampStep);

        // Only once a ramp has actually reached silence (not just close to
        // it) do we skip propagation+encoding entirely for this object --
        // the actual performance win, rather than paying full cost every
        // block just to encode silence while muted/soloed/source-muted-out.
        const bool audioProcessingActive = ! (effectivelyMuted && ramp <= 0.0f)
                                         && ! (sourceMuted && sourceRamp <= 0.0f);

        if (! audioProcessingActive)
        {
            // Treated like "became inactive" for PropagationProcessor's
            // internal delay-line state -- resuming later re-syncs
            // (.reset()) instead of resuming from state that's now stale
            // relative to however far the object has actually moved.
            wasActiveLastBlock[(size_t) objIdx] = false;
            continue;
        }

        if (! wasActiveLastBlock[(size_t) objIdx])
            propagationPerObject[(size_t) objIdx].reset();
        wasActiveLastBlock[(size_t) objIdx] = true;

        float directivityGain = 1.0f;
        propagationPerObject[(size_t) objIdx].process (inputCopy.getReadPointer (obj.inputChannel),
                                                         propagated, numSamples,
                                                         snapshot[(size_t) objIdx].position,
                                                         obj, sceneSettings,
                                                         directivityGain);

        float azimuth, elevation, distance;
        cartesianToSpherical (snapshot[(size_t) objIdx].position, azimuth, elevation, distance);

        encoder.encodeBlock (propagated,
                              numSamples,
                              azimuth, elevation, distance,
                              obj.gain * directivityGain * ramp * sourceRamp,
                              encodeTarget,
                              previousGainsPerObject[(size_t) objIdx]);
    }

    // --- GrainCloud rendering ------------------------------------------
    // Grain positions/trigger parameters come from a control-rate snapshot
    // (like SoundObjects); the audio-thread-owned GrainAudioState tracks
    // each active grain's sample-accurate envelope/pitch progress
    // independently of that coarser update rate. Deliberately no
    // PropagationProcessor pass for grains (no per-grain delay line/air
    // absorption/directivity) -- with dozens of simultaneous short-lived
    // grains, that would be disproportionately expensive; grains only get
    // AmbisonicsEncoder's built-in spatial encoding + distance gain, plus
    // an optional, much cheaper per-grain Doppler pitch shift (see
    // GrainCloudSettings::dopplerEnabled/GrainDoppler.h below -- a single
    // per-block ratio, not a delay line).
    float* grainOut = grainScratch.getWritePointer (0);

    for (int objIdx = 0; objIdx < trajectoryEngine.getNumObjects(); ++objIdx)
    {
        auto& obj = trajectoryEngine.getObject (objIdx);
        if (obj.inputChannel < 0)
            continue; // no parent, no cloud -- see GrainCloud/PluginEditor: an inactive object's cloud is frozen, not rendered

        // Mute/solo applies to a muted object's grains too (see
        // SoundObject::muted/soloed) -- reuses the ramp already advanced
        // for the parent object in the loop above (same block, same
        // smoothed fade), skipping this object's grains entirely once
        // that ramp has actually reached silence, same performance
        // reasoning as the main-object loop above. Deliberately NOT
        // gated by GrainCloudSettings::sourceMuted (that one only silences
        // the main-object loop's own dry-audio rendering above) -- so
        // muting just the source still leaves these grains audible.
        const bool effectivelyMuted = MuteSoloLogic::isEffectivelyMuted (obj.muted, obj.soloed, anySoloed);
        const float muteRamp = muteRampGain[(size_t) objIdx];
        if (effectivelyMuted && muteRamp <= 0.0f)
            continue;

        std::vector<GrainCloud::Snapshot> grainSnapshot;
        trajectoryEngine.getGrainCloud (objIdx).getSnapshot (grainSnapshot);

        auto& ring = grainRingBuffers[(size_t) objIdx];
        const int ringSize = ring.getNumSamples();
        if (ringSize <= 0)
            continue;
        const float* ringData = ring.getReadPointer (0);

        auto& audioStatesForObject = grainAudioState[(size_t) objIdx];
        const size_t numSlots = juce::jmin (grainSnapshot.size(), audioStatesForObject.size());

        for (size_t slot = 0; slot < numSlots; ++slot)
        {
            auto& snap = grainSnapshot[slot];
            auto& state = audioStatesForObject[slot];

            if (! snap.active)
            {
                state.lastSeenGeneration = -1; // force a reset if this slot is reused later
                continue;
            }

            if (snap.spawnGeneration != state.lastSeenGeneration)
            {
                state.lastSeenGeneration = snap.spawnGeneration;
                state.samplesPlayed = 0;
                state.readPosition = (double) snap.bufferReadStartSample;
                std::fill (state.previousChannelGains.begin(), state.previousChannelGains.end(), 0.0f);
            }

            if (state.samplesPlayed >= snap.grainLengthSamples)
                continue; // this grain's audio envelope has already finished (physics may still be fading out)

            // Optional per-grain Doppler (default off, see
            // GrainCloudSettings::dopplerEnabled) -- a per-block-constant
            // pitch ratio multiplied into the existing pitchJitter-based
            // rate, deliberately not a delay line (see GrainDoppler.h).
            float effectivePlaybackRate = snap.playbackRate;
            if (trajectoryEngine.getGrainCloud (objIdx).getSettings().dopplerEnabled)
                effectivePlaybackRate *= GrainDoppler::computeDopplerRatio (snap.position, snap.velocity,
                                                                             obj.dopplerFactor, sceneSettings.speedOfSound);

            renderGrainBlock (ringData, ringSize, state.readPosition, effectivePlaybackRate,
                               snap.grainLengthSamples, state.samplesPlayed, grainOut, numSamples);

            float azimuth, elevation, distance;
            cartesianToSpherical (snap.position, azimuth, elevation, distance);

            encoder.encodeBlock (grainOut, numSamples, azimuth, elevation, distance,
                                  obj.gain * muteRamp, encodeTarget, state.previousChannelGains);
        }
    }

    // Decode ambiScratch down to the real output bus -- skipped entirely
    // for raw passthrough modes, which already wrote straight into buffer
    // above. Binaural is decoded via the separate binauralDecoder (HRTF
    // convolution, not part of AmbisonicsDecoder -- see both classes' own
    // comments), every other decoded mode via decoder.decode() as before.
    if (! rawPassthrough)
    {
        if (decoder.getMode() == AmbisonicsDecoder::Mode::Binaural)
            binauralDecoder.decode (ambiScratch, buffer, numSamples);
        else
            decoder.decode (ambiScratch, buffer, numSamples);
    }

    // Real, measured CPU-load estimate -- see maxConcurrentGrainsGlobal's
    // comment in the header for why this exists (128 concurrent grains is
    // a rough estimate, not a profiled number; this lets the user check
    // for themselves instead of trusting the estimate blindly). Smoothed
    // (exponential moving average) so the UI reading doesn't flicker
    // block-to-block.
    const auto blockEndTicks = juce::Time::getHighResolutionTicks();
    const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds (blockEndTicks - blockStartTicks);
    const double blockDurationSeconds = (double) numSamples / juce::jmax (1.0, currentSampleRate);
    const float instantLoad = (float) (elapsedSeconds / blockDurationSeconds);

    constexpr float smoothing = 0.9f;
    const float previousLoad = processBlockLoadFraction.load (std::memory_order_relaxed);
    processBlockLoadFraction.store (previousLoad * smoothing + instantLoad * (1.0f - smoothing), std::memory_order_relaxed);
}

// --- Parameter registry ---------------------------------------------------
// See ParameterRegistry.h for the "why not juce::AudioProcessorParameter"
// reasoning, and PluginProcessor.h for what each helper below does. Every
// field registered here is one that already had a slider/toggle in
// ParameterPanel -- pure runtime physics STATE (position, velocity,
// orbitPhase, etc.) is deliberately excluded, same scope ParameterPanel
// itself already draws the line at.

void KlangorbitProcessor::registerObjectFloatParam (const juce::String& key, const juce::String& displayName,
                                                      const juce::String& category, float SoundObject::* member,
                                                      float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + "." + key, displayName, category, minValue, maxValue, polarity,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getObject (i).*member; },
            [this, member, i] (float v) { trajectoryEngine.getObject (i).*member = v; }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject." + key, displayName, category, minValue, maxValue, polarity,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumObjects()) ? trajectoryEngine.getObject (idx).*member : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumObjects())
                trajectoryEngine.getObject (idx).*member = v;
        }
    });
}

void KlangorbitProcessor::registerObjectBoolParam (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, bool SoundObject::* member)
{
    for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + "." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getObject (i).*member ? 1.0f : 0.0f; },
            [this, member, i] (float v) { trajectoryEngine.getObject (i).*member = (v >= 0.5f); }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumObjects()) ? (trajectoryEngine.getObject (idx).*member ? 1.0f : 0.0f) : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumObjects())
                trajectoryEngine.getObject (idx).*member = (v >= 0.5f);
        }
    });
}

namespace
{
    // One entry per Vec3 axis -- shared by registerObjectVec3Param() and
    // registerSceneVec3Param() below. idSuffix/labelSuffix append onto the
    // field's own key/displayName (e.g. key "orbitCenter" + this ->
    // "orbitCenter.x" / "Orbit Center X"), field is the pointer-to-member
    // used to read/write that one axis of a Vec3 already reached via
    // another pointer-to-member (obj.*vec3Member).*field -- see the
    // callers below for why two chained pointer-to-member dereferences are
    // needed here.
    struct Vec3AxisField { const char* idSuffix; const char* labelSuffix; float Vec3::* field; };
    constexpr Vec3AxisField vec3Axes[] = {
        { "x", " X", &Vec3::x },
        { "y", " Y", &Vec3::y },
        { "z", " Z", &Vec3::z },
    };
}

void KlangorbitProcessor::registerObjectVec3Param (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, Vec3 SoundObject::* member,
                                                     float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (auto& axis : vec3Axes)
    {
        for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
        {
            parameterRegistry.registerParameter ({
                "object." + juce::String (i) + "." + key + "." + axis.idSuffix, displayName + axis.labelSuffix, category,
                minValue, maxValue, polarity, ParameterRegistry::Scope::SpecificObject, i,
                [this, member, field = axis.field, i] { return (trajectoryEngine.getObject (i).*member).*field; },
                [this, member, field = axis.field, i] (float v) { (trajectoryEngine.getObject (i).*member).*field = v; }
            });
        }

        parameterRegistry.registerParameter ({
            "selectedObject." + key + "." + axis.idSuffix, displayName + axis.labelSuffix, category,
            minValue, maxValue, polarity, ParameterRegistry::Scope::SelectedObject, -1,
            [this, member, field = axis.field]
            {
                const int idx = selectedObjectIndex;
                return (idx >= 0 && idx < trajectoryEngine.getNumObjects()) ? (trajectoryEngine.getObject (idx).*member).*field : 0.0f;
            },
            [this, member, field = axis.field] (float v)
            {
                const int idx = selectedObjectIndex;
                if (idx >= 0 && idx < trajectoryEngine.getNumObjects())
                    (trajectoryEngine.getObject (idx).*member).*field = v;
            }
        });
    }
}

void KlangorbitProcessor::registerGrainFloatParam (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, float GrainCloudSettings::* member,
                                                     float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + ".grain." + key, displayName, category, minValue, maxValue, polarity,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getGrainCloud (i).getSettings().*member; },
            [this, member, i] (float v) { trajectoryEngine.getGrainCloud (i).getSettings().*member = v; }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject.grain." + key, displayName, category, minValue, maxValue, polarity,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds()) ? trajectoryEngine.getGrainCloud (idx).getSettings().*member : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds())
                trajectoryEngine.getGrainCloud (idx).getSettings().*member = v;
        }
    });
}

void KlangorbitProcessor::registerGrainBoolParam (const juce::String& key, const juce::String& displayName,
                                                    const juce::String& category, bool GrainCloudSettings::* member)
{
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + ".grain." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getGrainCloud (i).getSettings().*member ? 1.0f : 0.0f; },
            [this, member, i] (float v) { trajectoryEngine.getGrainCloud (i).getSettings().*member = (v >= 0.5f); }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject.grain." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds()) ? (trajectoryEngine.getGrainCloud (idx).getSettings().*member ? 1.0f : 0.0f) : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds())
                trajectoryEngine.getGrainCloud (idx).getSettings().*member = (v >= 0.5f);
        }
    });
}

void KlangorbitProcessor::registerGrainIntParam (const juce::String& key, const juce::String& displayName,
                                                   const juce::String& category, int GrainCloudSettings::* member,
                                                   float minValue, float maxValue)
{
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + ".grain." + key, displayName, category, minValue, maxValue, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return (float) (trajectoryEngine.getGrainCloud (i).getSettings().*member); },
            [this, member, i] (float v) { trajectoryEngine.getGrainCloud (i).getSettings().*member = (int) std::round (v); }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject.grain." + key, displayName, category, minValue, maxValue, ParameterRegistry::Polarity::Unipolar,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds()) ? (float) (trajectoryEngine.getGrainCloud (idx).getSettings().*member) : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds())
                trajectoryEngine.getGrainCloud (idx).getSettings().*member = (int) std::round (v);
        }
    });
}

void KlangorbitProcessor::registerSceneFloatParam (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, float SceneSettings::* member,
                                                     float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    parameterRegistry.registerParameter ({
        "scene." + key, displayName, category, minValue, maxValue, polarity, ParameterRegistry::Scope::Global, -1,
        [this, member] { return trajectoryEngine.getSceneSettings().*member; },
        [this, member] (float v) { trajectoryEngine.getSceneSettings().*member = v; }
    });
}

void KlangorbitProcessor::registerSceneBoolParam (const juce::String& key, const juce::String& displayName,
                                                    const juce::String& category, bool SceneSettings::* member)
{
    parameterRegistry.registerParameter ({
        "scene." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar, ParameterRegistry::Scope::Global, -1,
        [this, member] { return trajectoryEngine.getSceneSettings().*member ? 1.0f : 0.0f; },
        [this, member] (float v) { trajectoryEngine.getSceneSettings().*member = (v >= 0.5f); }
    });
}

void KlangorbitProcessor::registerSceneVec3Param (const juce::String& key, const juce::String& displayName,
                                                    const juce::String& category, Vec3 SceneSettings::* member,
                                                    float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (auto& axis : vec3Axes)
    {
        parameterRegistry.registerParameter ({
            "scene." + key + "." + axis.idSuffix, displayName + axis.labelSuffix, category,
            minValue, maxValue, polarity, ParameterRegistry::Scope::Global, -1,
            [this, member, field = axis.field] { return (trajectoryEngine.getSceneSettings().*member).*field; },
            [this, member, field = axis.field] (float v) { (trajectoryEngine.getSceneSettings().*member).*field = v; }
        });
    }
}

void KlangorbitProcessor::buildParameterRegistry()
{
    // Ranges/categories mirror ParameterPanel's own rows exactly (see
    // ParameterPanel.cpp) -- both are meant to describe the same set of
    // "things a user/controller can dial in," just through two different
    // control surfaces (mouse-driven sliders vs. this registry's
    // controller-mapping consumers). orbitOrientation is the one
    // exception: it's a real, already-existing SoundObject field with a
    // well-defined range that ParameterPanel's own UI happens not to
    // expose a slider for yet -- included here anyway so a future mapping
    // consumer isn't missing a parameter that genuinely already exists.
    //
    // Deliberately excluded: pure runtime physics STATE (position,
    // velocity, orbitPhase, attractionPulsePhase, orbitRadiusNoiseSmoothed,
    // slingshotTargetId/Strength) and every enum-valued field (SoundObject
    // ::mode/directivityPattern, GrainCloudSettings::windowShape/
    // movementMode/grainReadDepthDistribution/pitchJitterMode/
    // pitchQuantizeScale, SceneSettings::boundaryBehavior) -- a single
    // float range doesn't naturally fit a fixed choice of N discrete
    // options; see the class comment.

    // --- Scene (Global scope) -------------------------------------------
    // Display names only, below -- the id strings (first argument:
    // "roomSize", "globalField", "windVector") are a stable identifier
    // used by saved mapping profiles/presets and are NOT renamed here,
    // only what's shown to the user in the Learn-mode target picker (see
    // ParameterPanel.cpp's matching label renames for the same reasoning).
    registerSceneFloatParam ("roomSize", "Boundary Size", "Global", &SceneSettings::roomSize, 0.0f, 50.0f);
    registerSceneBoolParam ("showRoomBoundary", "Show Boundary", "Global", &SceneSettings::showRoomBoundary);
    registerSceneVec3Param ("globalField", "Force Field", "Global", &SceneSettings::globalField, -20.0f, 20.0f);
    registerSceneFloatParam ("timeScale", "Time Scale", "Global", &SceneSettings::timeScale, 0.05f, 5.0f);
    registerSceneFloatParam ("speedOfSound", "Speed of Sound", "Global", &SceneSettings::speedOfSound, 1.0f, 400.0f);
    registerSceneFloatParam ("temperature", "Temperature", "Global", &SceneSettings::temperature, -20.0f, 45.0f);
    registerSceneFloatParam ("relativeHumidity", "Relative Humidity", "Global", &SceneSettings::relativeHumidity, 0.0f, 100.0f);
    registerSceneFloatParam ("atmosphericPressure", "Atmospheric Pressure", "Global", &SceneSettings::atmosphericPressure, 80.0f, 110.0f);
    registerSceneVec3Param ("windVector", "Propagation Wind", "Global", &SceneSettings::windVector, -50.0f, 50.0f);

    // --- Object physics ---------------------------------------------------
    registerObjectFloatParam ("mass", "Mass", "Object Physics", &SoundObject::mass, 0.01f, 20.0f);
    registerObjectFloatParam ("gain", "Gain", "Object Physics", &SoundObject::gain, 0.0f, 2.0f);
    registerObjectFloatParam ("damping", "Damping", "Object Physics", &SoundObject::damping, 0.0f, 1.0f);
    registerObjectFloatParam ("maxVelocity", "Max Velocity", "Object Physics", &SoundObject::maxVelocity, 0.0f, 30.0f);
    registerObjectFloatParam ("dragCoefficient", "Drag Coefficient", "Object Physics", &SoundObject::dragCoefficient, 0.0f, 10.0f);
    registerObjectFloatParam ("restitution", "Restitution", "Object Physics", &SoundObject::restitution, 0.0f, 1.0f);
    registerObjectFloatParam ("velocitySnapThreshold", "Stop Threshold", "Object Physics", &SoundObject::velocitySnapThreshold, 0.0f, 1.0f);
    registerObjectBoolParam ("muted", "Mute", "Object Physics", &SoundObject::muted);
    registerObjectBoolParam ("soloed", "Solo", "Object Physics", &SoundObject::soloed);

    // --- Attraction ---------------------------------------------------------
    registerObjectFloatParam ("attractionStrength", "Attraction Strength", "Attraction", &SoundObject::attractionStrength, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
    registerObjectFloatParam ("forceExponent", "Force Exponent", "Attraction", &SoundObject::forceExponent, 1.0f, 3.0f);
    registerObjectFloatParam ("minDistance", "Min. Distance", "Attraction", &SoundObject::minDistance, 0.01f, 2.0f);
    registerObjectFloatParam ("maxRange", "Max. Range", "Attraction", &SoundObject::maxRange, 0.0f, 20.0f);
    registerObjectFloatParam ("attractionPulseRate", "Pulse Rate", "Attraction", &SoundObject::attractionPulseRate, 0.0f, 5.0f);
    registerObjectFloatParam ("attractionPulseDepth", "Pulse Depth", "Attraction", &SoundObject::attractionPulseDepth, 0.0f, 1.0f);

    // --- Orbit ----------------------------------------------------------
    registerObjectVec3Param ("orbitCenter", "Orbit Center", "Orbit", &SoundObject::orbitCenter, -20.0f, 20.0f);
    registerObjectFloatParam ("orbitRadius", "Orbit Radius", "Orbit", &SoundObject::orbitRadius, 0.05f, 10.0f);
    registerObjectFloatParam ("orbitAngularSpeed", "Orbit Angular Speed", "Orbit", &SoundObject::orbitAngularSpeed, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
    registerObjectVec3Param ("orbitPlaneNormal", "Orbit Plane Normal", "Orbit", &SoundObject::orbitPlaneNormal, -1.0f, 1.0f);
    registerObjectFloatParam ("orbitEccentricity", "Orbit Eccentricity", "Orbit", &SoundObject::orbitEccentricity, 0.0f, 0.95f);
    registerObjectFloatParam ("orbitOrientation", "Orbit Orientation", "Orbit", &SoundObject::orbitOrientation, 0.0f, juce::MathConstants<float>::twoPi);
    registerObjectFloatParam ("orbitDecay", "Orbit Decay", "Orbit", &SoundObject::orbitDecay, -2.0f, 2.0f, ParameterRegistry::Polarity::Bipolar);
    registerObjectFloatParam ("orbitRadiusBaseline", "Radius Baseline", "Orbit", &SoundObject::orbitRadiusBaseline, 0.05f, 10.0f);
    registerObjectFloatParam ("orbitRadiusReversionRate", "Radius Reversion Rate", "Orbit", &SoundObject::orbitRadiusReversionRate, 0.0f, 5.0f);
    registerObjectFloatParam ("orbitRadiusNoiseAmplitude", "Radius Noise Amplitude", "Orbit", &SoundObject::orbitRadiusNoiseAmplitude, 0.0f, 5.0f);
    registerObjectFloatParam ("orbitRadiusNoiseSmoothing", "Radius Noise Smoothing", "Orbit", &SoundObject::orbitRadiusNoiseSmoothing, 0.0f, 5.0f);

    // --- Doppler ----------------------------------------------------------
    registerObjectBoolParam ("dopplerEnabled", "Doppler Enabled", "Doppler", &SoundObject::dopplerEnabled);
    registerObjectFloatParam ("dopplerFactor", "Doppler Factor", "Doppler", &SoundObject::dopplerFactor, 0.0f, 5.0f);
    registerObjectFloatParam ("dopplerSmoothing", "Doppler Smoothing", "Doppler", &SoundObject::dopplerSmoothing, 0.0f, 2.0f);
    registerObjectVec3Param ("sourceOrientation", "Source Orientation", "Doppler", &SoundObject::sourceOrientation, -1.0f, 1.0f);

    // --- Grain Cloud ------------------------------------------------------
    registerGrainBoolParam ("enabled", "Enabled", "Grain Cloud", &GrainCloudSettings::enabled);
    registerGrainBoolParam ("sourceMuted", "Isolate Grains", "Grain Cloud", &GrainCloudSettings::sourceMuted);
    registerGrainBoolParam ("dopplerEnabled", "Doppler", "Grain Cloud", &GrainCloudSettings::dopplerEnabled);
    registerGrainFloatParam ("grainRate", "Grain Rate", "Grain Cloud", &GrainCloudSettings::grainRate, 0.1f, GrainLimits::maxGrainRate);
    registerGrainFloatParam ("grainRateJitter", "Grain Rate Jitter", "Grain Cloud", &GrainCloudSettings::grainRateJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("grainDuration", "Grain Duration", "Grain Cloud", &GrainCloudSettings::grainDuration, 0.01f, GrainLimits::maxGrainDuration);
    registerGrainFloatParam ("grainDurationJitter", "Grain Duration Jitter", "Grain Cloud", &GrainCloudSettings::grainDurationJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("pitchJitter", "Pitch Jitter", "Grain Cloud", &GrainCloudSettings::pitchJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("positionJitterInBuffer", "Position Jitter In Buffer", "Grain Cloud", &GrainCloudSettings::positionJitterInBuffer, 0.0f, GrainLimits::maxPositionJitterInBuffer);
    registerGrainFloatParam ("grainReadDepthRangeMin", "Read Depth Min", "Grain Cloud", &GrainCloudSettings::grainReadDepthRangeMin, 0.0f, GrainLimits::maxGrainReadDepthRange);
    registerGrainFloatParam ("grainReadDepthRangeMax", "Read Depth Max", "Grain Cloud", &GrainCloudSettings::grainReadDepthRangeMax, 0.0f, GrainLimits::maxGrainReadDepthRange);
    registerGrainIntParam ("maxConcurrentGrains", "Max Concurrent Grains", "Grain Cloud", &GrainCloudSettings::maxConcurrentGrains, 1.0f, 256.0f);
    registerGrainFloatParam ("randomWalkSpeed", "Random Walk Speed", "Grain Cloud", &GrainCloudSettings::randomWalkSpeed, 0.0f, 10.0f);
    registerGrainFloatParam ("boundaryRadius", "Boundary Radius", "Grain Cloud", &GrainCloudSettings::boundaryRadius, 0.05f, 5.0f);
    registerGrainFloatParam ("boundaryRadiusJitter", "Boundary Radius Jitter", "Grain Cloud", &GrainCloudSettings::boundaryRadiusJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("restitution", "Restitution", "Grain Cloud", &GrainCloudSettings::restitution, 0.0f, 1.0f);
    registerGrainFloatParam ("initialSpeed", "Initial Speed", "Grain Cloud", &GrainCloudSettings::initialSpeed, 0.0f, 20.0f);
    registerGrainFloatParam ("initialSpeedJitter", "Initial Speed Jitter", "Grain Cloud", &GrainCloudSettings::initialSpeedJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("acceleration", "Acceleration", "Grain Cloud", &GrainCloudSettings::acceleration, -20.0f, 20.0f, ParameterRegistry::Polarity::Bipolar);
    registerGrainFloatParam ("orbitRadius", "Orbit Radius", "Grain Cloud", &GrainCloudSettings::orbitRadius, 0.05f, 5.0f);
    registerGrainFloatParam ("orbitRadiusJitter", "Orbit Radius Jitter", "Grain Cloud", &GrainCloudSettings::orbitRadiusJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("orbitAngularSpeed", "Orbit Angular Speed", "Grain Cloud", &GrainCloudSettings::orbitAngularSpeed, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
    registerGrainFloatParam ("orbitSphereSpread", "Orbit Sphere Spread", "Grain Cloud", &GrainCloudSettings::orbitSphereSpread, 0.0f, 1.0f);
    registerGrainFloatParam ("attractionStrength", "Attraction Strength", "Grain Cloud", &GrainCloudSettings::attractionStrength, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
}

juce::AudioProcessorEditor* KlangorbitProcessor::createEditor()
{
    return new KlangorbitEditor (*this);
}

void KlangorbitProcessor::getStateInformation (juce::MemoryBlock&) { /* TODO: save object/trajectory presets */ }
void KlangorbitProcessor::setStateInformation (const void*, int)   { /* TODO */ }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KlangorbitProcessor();
}
