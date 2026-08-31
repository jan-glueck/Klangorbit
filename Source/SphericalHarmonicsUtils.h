#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include "Vec3.h"

/**
    Small, pure-math helpers shared by anything that builds an Ambisonics
    decode matrix onto a dense virtual array of directions -- originally
    written for AmbisonicsDecoder's own AllRAD implementation
    (buildAllRadMatrix()), extracted here so BinauralDecoder (a virtual-
    loudspeaker-array + per-direction HRTF convolution decode, see its own
    class comment) can reuse the identical point distribution and decode
    weighting instead of a second, easy-to-drift-out-of-sync copy of the
    same math. No JUCE GUI/audio dependency -- just juce_core (for
    juce::MathConstants/jlimit) and Vec3.
*/
namespace SphericalHarmonicsUtils
{
    // Uniform-ish sphere coverage for a dense virtual array -- a Fibonacci
    // lattice, deliberately simpler than a formal spherical t-design (which
    // is what Zotter/Frank's own AllRAD reference implementation uses) but
    // a well-known, easy-to-verify way to get good, roughly-even coverage
    // without needing a lookup table of precomputed t-design points.
    // Points are unit-length Vec3s in this project's own front(+x)/
    // left(+y)/up(+z) convention (see SoundObject.h) -- ready to feed
    // directly into atan2(y,x)/asin(z) for azimuth/elevation, or, for
    // BinauralDecoder specifically, directly into libmysofa's
    // mysofa_getfilter_float() as Cartesian x/y/z, since SOFA's own
    // coordinate convention (confirmed against libmysofa's coordinates.c)
    // matches this project's convention exactly -- no remapping needed
    // either way.
    inline std::vector<Vec3> fibonacciSphere (int numPoints)
    {
        std::vector<Vec3> points;
        points.reserve ((size_t) numPoints);
        const float goldenAngle = 2.39996323f; // pi * (3 - sqrt(5))

        for (int i = 0; i < numPoints; ++i)
        {
            const float t = (numPoints > 1) ? ((float) i / (float) (numPoints - 1)) : 0.0f;
            const float z = 1.0f - 2.0f * t;
            const float radius = std::sqrt (std::max (0.0f, 1.0f - z * z));
            const float theta = goldenAngle * (float) i;
            points.push_back ({ radius * std::cos (theta), radius * std::sin (theta), z });
        }
        return points;
    }

    // Legendre polynomial P_l(x) (degree l, order m=0), via Bonnet's
    // recursion -- used only by maxReWeights() below.
    inline double legendreP (int l, double x)
    {
        if (l == 0) return 1.0;
        if (l == 1) return x;

        double pPrev2 = 1.0, pPrev1 = x, p = x;
        for (int n = 2; n <= l; ++n)
        {
            p = ((2.0 * n - 1.0) * x * pPrev1 - (n - 1.0) * pPrev2) / n;
            pPrev2 = pPrev1;
            pPrev1 = p;
        }
        return p;
    }

    // Max-rE weighting (Daniel 2003; also the standard virtual-array decode
    // weighting in AllRAD itself, Zotter & Frank 2012 sec. 3.2): tapers
    // down higher Ambisonics orders on the DECODE side only, trading a
    // slightly wider main lobe for much better suppression of the
    // reconstruction sidelobes a raw, un-windowed order-limited SH decode
    // otherwise produces. Necessary here, not optional polish: without it,
    // a dense virtual array's many small-but-numerous sidelobe
    // contributions swamp the true on-axis peak once summed -- verified
    // empirically for AllRAD (a straight-ahead 5.1 test source came out
    // nearly EQUAL across all five channels without this weighting), and
    // the same reasoning applies identically to BinauralDecoder's own
    // virtual-array stage.
    inline std::vector<float> maxReWeights (int order)
    {
        const double t = 137.9 * (juce::MathConstants<double>::pi / 180.0) / (double) (order + 2);
        const double cosT = std::cos (t);

        std::vector<float> weights ((size_t) order + 1);
        for (int l = 0; l <= order; ++l)
            weights[(size_t) l] = (float) legendreP (l, cosT);
        return weights;
    }

    // Applies maxReWeights() to a full ACN-ordered coefficient vector in
    // place -- channel c belongs to Ambisonics order l = floor(sqrt(c)),
    // a standard property of ACN channel numbering.
    inline void applyMaxReWeights (std::vector<float>& coeffs, const std::vector<float>& weights)
    {
        for (int c = 0; c < (int) coeffs.size(); ++c)
        {
            const int l = (int) std::floor (std::sqrt ((double) c) + 1.0e-9);
            coeffs[(size_t) c] *= weights[(size_t) l];
        }
    }
}
