#pragma once
#include "Vec3.h"
#include <cmath>
#include <vector>

/**
    Vector Base Amplitude Panning (Pulkki 1997) for a FIXED, small set of
    loudspeaker directions -- used by AmbisonicsDecoder to remap AllRAD's
    dense virtual array onto the real (sparse, irregular) target layout.

    Not a general-purpose panning library for arbitrary/large speaker
    counts: triangulate() uses a brute-force O(n^3) convex-hull face test,
    fine for our layouts (4-12 speakers, computed once per mode switch,
    never per-sample) but not meant to scale further.

    Two triangulation cases, chosen automatically:
    - All speakers at (near) zero elevation ("horizontal ring", e.g. Quad,
      5.1, 7.1, the Stereo pair): panning is inherently 2D, so adjacent
      speakers (sorted by azimuth) form PAIRS, not triangles -- a real 3D
      convex hull of coplanar points is degenerate (zero volume) and
      would not produce usable faces.
    - Anything with height speakers (the Atmos-bed layouts): a genuine 3D
      convex hull, found by testing every triple of speakers and keeping
      the ones where every other speaker lies behind that triple's plane
      (i.e. it's an outward-facing hull face). This is what makes AllRAD's
      remap step correct for irregular arrays instead of a naive
      nearest-speaker or simple pairwise approach.
*/
namespace VBAP
{
    // c == -1 marks a horizontal PAIR (2-speaker region) rather than a
    // proper 3D triangle -- see this header's own comment.
    struct Region
    {
        int a = -1, b = -1, c = -1;
    };

    // directions must be unit vectors; LFE entries (no direction) must
    // already be excluded by the caller -- see SpeakerLayouts::Speaker::isLfe.
    std::vector<Region> triangulate (const std::vector<Vec3>& directions);

    // Gains for panning a source at `direction` (unit vector) across
    // `layoutDirections` using the given triangulation -- same length as
    // layoutDirections, zero everywhere except the 2 or 3 speakers of
    // whichever region actually contains that direction, energy-normalized
    // (sum of squares == 1) over those active speakers. If `direction`
    // doesn't fall cleanly inside any region (shouldn't happen for a
    // proper hull covering the whole sphere, but guards against
    // near-degenerate layouts), falls back to the single nearest speaker
    // at full gain rather than producing silence.
    std::vector<float> computeGains (Vec3 direction, const std::vector<Vec3>& layoutDirections,
                                      const std::vector<Region>& regions);
}
