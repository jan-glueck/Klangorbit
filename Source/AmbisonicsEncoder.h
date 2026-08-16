#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

/**
    Encodes N mono source signals into an Ambisonics B-format based on their
    3D position (ACN channel order, SN3D normalization, as customary in the
    AmbiX standard -- compatible with IEM Plugin Suite, SPARTA).

    Number of channels = (order+1)^2. Order can be changed at runtime
    (setOrder()); coefficients are then recomputed per object on every
    audio block (cheap enough at block rate, NOT per sample -- see
    encodeBlock()).

    The spherical harmonics are computed generically via associated
    Legendre polynomials (no per-order hardcoded formulas), so higher
    orders work without code changes.
*/
class AmbisonicsEncoder
{
public:
    AmbisonicsEncoder();

    void setOrder (int newOrder);
    int getOrder() const { return order; }
    int getNumChannels() const { return (order + 1) * (order + 1); }

    void prepare (double sampleRate, int maxBlockSize);

    /**
        Encodes one block of a single source signal into the destination
        Ambisonics buffer (gets ADDED, not overwritten -- so several
        objects can be summed one after another into the same bus).

        azimuthRad:   angle in the horizontal plane, 0 = front, positive
                      counter-clockwise (mathematically positive).
        elevationRad: 0 = horizontal, +pi/2 = up.
        distanceMeters: for distance attenuation/air absorption.
        gain: additional manual gain (SoundObject::gain).

        sourceBlock: mono input, length numSamples.
        destAmbiBuffer: must have at least getNumChannels() channels.
        previousChannelGains: persistent state PER OBJECT (not per encoder
            instance!) -- held by the caller, e.g. as a member in
            SoundObject or as a parallel array in PluginProcessor. Linearly
            ramped here from the old to the new gains, to avoid zipper
            noise on fast movement. Must be initialized to getNumChannels()
            zeros before the first call.
    */
    void encodeBlock (const float* sourceBlock,
                       int numSamples,
                       float azimuthRad,
                       float elevationRad,
                       float distanceMeters,
                       float gain,
                       juce::AudioBuffer<float>& destAmbiBuffer,
                       std::vector<float>& previousChannelGains);

    // Pure coefficient computation (for tests/debugging), without distance/gain.
    // out must have room for getNumChannels() elements.
    void computeShCoefficients (float azimuthRad, float elevationRad, std::vector<float>& out) const;

private:
    int order = 3;
    double sampleRate = 48000.0;

    // Reference distance for 0dB (typically 1m), 1/r attenuation beyond that.
    float referenceDistance = 1.0f;

    // Very simple air absorption (high-frequency loss over distance) as a
    // one-pole low-pass, cutoff drops with distance. Per-object separate,
    // hence handled here as simple scalar state per caller -- deliberately
    // kept simple for the POC (no persistent per-object filter, just an
    // approximation via gain rolloff instead of a real filter).
    static float distanceGain (float distanceMeters, float referenceDistance);

    // Associated Legendre polynomial P_l^m(x), needed for the real-valued SH.
    static double associatedLegendre (int l, int m, double x);
    static double factorial (int n);
};
