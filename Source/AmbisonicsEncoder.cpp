#include "AmbisonicsEncoder.h"

AmbisonicsEncoder::AmbisonicsEncoder() {}

void AmbisonicsEncoder::setOrder (int newOrder)
{
    order = juce::jlimit (0, 7, newOrder); // 7th order as a rough upper bound for the POC
}

void AmbisonicsEncoder::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;
}

double AmbisonicsEncoder::factorial (int n)
{
    double f = 1.0;
    for (int i = 2; i <= n; ++i) f *= (double) i;
    return f;
}

// Associated Legendre polynomial P_l^m(x), m >= 0, without the Condon-Shortley
// phase (customary in the Ambisonics literature, since the sign is already
// baked into the channel definition). Standard recursion.
double AmbisonicsEncoder::associatedLegendre (int l, int m, double x)
{
    jassert (m >= 0 && m <= l);

    double pmm = 1.0;
    if (m > 0)
    {
        double somx2 = std::sqrt (juce::jmax (0.0, (1.0 - x) * (1.0 + x)));
        double fact = 1.0;
        for (int i = 1; i <= m; ++i)
        {
            pmm *= fact * somx2;
            fact += 2.0;
        }
    }

    if (l == m)
        return pmm;

    double pmmp1 = x * (2.0 * m + 1.0) * pmm;
    if (l == m + 1)
        return pmmp1;

    double pll = 0.0;
    for (int ll = m + 2; ll <= l; ++ll)
    {
        pll = (x * (2.0 * ll - 1.0) * pmmp1 - (ll + m - 1.0) * pmm) / (ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

void AmbisonicsEncoder::computeShCoefficients (float azimuthRad, float elevationRad, std::vector<float>& out) const
{
    const int numCh = getNumChannels();
    out.resize ((size_t) numCh);

    const double sinElev = std::sin ((double) elevationRad);

    for (int l = 0; l <= order; ++l)
    {
        for (int m = -l; m <= l; ++m)
        {
            const int acn = l * l + l + m;
            const int absM = std::abs (m);

            const double legendre = associatedLegendre (l, absM, sinElev);

            // SN3D normalization
            const double kronecker = (m == 0) ? 1.0 : 0.0;
            const double norm = std::sqrt ((2.0 - kronecker) * factorial (l - absM) / factorial (l + absM));

            double azimuthal;
            if (m > 0)      azimuthal = std::cos (absM * (double) azimuthRad);
            else if (m < 0) azimuthal = std::sin (absM * (double) azimuthRad);
            else            azimuthal = 1.0;

            out[(size_t) acn] = (float) (norm * legendre * azimuthal);
        }
    }
}

float AmbisonicsEncoder::distanceGain (float distanceMeters, float referenceDistance)
{
    const float d = juce::jmax (distanceMeters, 0.05f);
    // 1/r law beyond the reference distance, no boost within it (clamped to 1)
    return juce::jmin (1.0f, referenceDistance / d);
}

void AmbisonicsEncoder::encodeBlock (const float* sourceBlock,
                                      int numSamples,
                                      float azimuthRad,
                                      float elevationRad,
                                      float distanceMeters,
                                      float gain,
                                      juce::AudioBuffer<float>& destAmbiBuffer,
                                      std::vector<float>& previousChannelGains)
{
    std::vector<float> coeffs;
    computeShCoefficients (azimuthRad, elevationRad, coeffs);

    const float distGain = distanceGain (distanceMeters, referenceDistance);
    const float totalGain = gain * distGain;

    const int numCh = juce::jmin ((int) coeffs.size(), destAmbiBuffer.getNumChannels());
    if ((int) previousChannelGains.size() < numCh)
        previousChannelGains.resize ((size_t) numCh, 0.0f);

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float startGain = previousChannelGains[(size_t) ch];
        const float endGain   = coeffs[(size_t) ch] * totalGain;

        if (std::abs (startGain) > 1.0e-8f || std::abs (endGain) > 1.0e-8f)
            destAmbiBuffer.addFromWithRamp (ch, 0, sourceBlock, numSamples, startGain, endGain);

        previousChannelGains[(size_t) ch] = endGain;
    }
}
