#include "VBAP.h"
#include <algorithm>
#include <limits>

namespace
{
    constexpr float planarityEpsilon = 1.0e-4f; // "on the plane" tolerance for the hull-face test
    constexpr float gainEpsilon = 1.0e-4f;       // "non-negative enough" tolerance when accepting a region

    bool allHorizontal (const std::vector<Vec3>& dirs)
    {
        for (auto& d : dirs)
            if (std::abs (d.z) > 1.0e-3f)
                return false;
        return true;
    }

    // Solves the 2x2 system [s1 s2] * [g1 g2]^T = target (using only x/y),
    // returns false if the speakers are degenerate (collinear from the
    // origin, i.e. the 2x2 matrix is singular).
    bool solve2x2 (Vec3 s1, Vec3 s2, Vec3 target, float& g1, float& g2)
    {
        const float det = s1.x * s2.y - s2.x * s1.y;
        if (std::abs (det) < 1.0e-8f)
            return false;

        g1 = (target.x * s2.y - s2.x * target.y) / det;
        g2 = (s1.x * target.y - target.x * s1.y) / det;
        return true;
    }

    // Solves the 3x3 system [s1 s2 s3] * [g1 g2 g3]^T = target via Cramer's
    // rule (fine for a handful of one-off solves at mode-setup time, not a
    // per-sample hot path). Returns false if the speakers are degenerate
    // (coplanar with the origin, i.e. the 3x3 matrix is singular).
    bool solve3x3 (Vec3 s1, Vec3 s2, Vec3 s3, Vec3 target, float& g1, float& g2, float& g3)
    {
        const float det = s1.x * (s2.y * s3.z - s3.y * s2.z)
                         - s2.x * (s1.y * s3.z - s3.y * s1.z)
                         + s3.x * (s1.y * s2.z - s2.y * s1.z);
        if (std::abs (det) < 1.0e-8f)
            return false;

        auto cramerDet = [] (Vec3 c1, Vec3 c2, Vec3 c3)
        {
            return c1.x * (c2.y * c3.z - c3.y * c2.z)
                 - c2.x * (c1.y * c3.z - c3.y * c1.z)
                 + c3.x * (c1.y * c2.z - c2.y * c1.z);
        };

        g1 = cramerDet (target, s2, s3) / det;
        g2 = cramerDet (s1, target, s3) / det;
        g3 = cramerDet (s1, s2, target) / det;
        return true;
    }
}

namespace VBAP
{
    std::vector<Region> triangulate (const std::vector<Vec3>& directions)
    {
        std::vector<Region> regions;
        const int n = (int) directions.size();
        if (n < 2)
            return regions;

        if (allHorizontal (directions))
        {
            // 2D case: sort by azimuth, pair up consecutive speakers
            // (wrapping around) -- see this header's own comment on why a
            // true 3D hull is degenerate for coplanar points.
            std::vector<int> order (directions.size());
            for (int i = 0; i < n; ++i) order[(size_t) i] = i;
            std::sort (order.begin(), order.end(), [&] (int a, int b)
            {
                return std::atan2 (directions[(size_t) a].y, directions[(size_t) a].x)
                     < std::atan2 (directions[(size_t) b].y, directions[(size_t) b].x);
            });

            for (int i = 0; i < n; ++i)
                regions.push_back ({ order[(size_t) i], order[(size_t) ((i + 1) % n)], -1 });

            return regions;
        }

        // 3D case: brute-force convex hull -- every triple is a hull face
        // if every OTHER point lies on/behind its plane. See this header's
        // own comment for why this is fine at our speaker counts.
        for (int i = 0; i < n; ++i)
        {
            for (int j = i + 1; j < n; ++j)
            {
                for (int k = j + 1; k < n; ++k)
                {
                    Vec3 normal = cross (directions[(size_t) j] - directions[(size_t) i],
                                          directions[(size_t) k] - directions[(size_t) i]);
                    const float normalLen = normal.length();
                    if (normalLen < 1.0e-6f)
                        continue; // collinear, not a real triangle

                    normal = normal / normalLen;
                    // Orient outward: the plane's normal should point away
                    // from the origin (same side as the triangle itself).
                    if (normal.dot (directions[(size_t) i]) < 0.0f)
                        normal = -normal;

                    bool isHullFace = true;
                    for (int m = 0; m < n; ++m)
                    {
                        if (m == i || m == j || m == k) continue;
                        if (normal.dot (directions[(size_t) m] - directions[(size_t) i]) > planarityEpsilon)
                        {
                            isHullFace = false;
                            break;
                        }
                    }

                    if (isHullFace)
                        regions.push_back ({ i, j, k });
                }
            }
        }

        return regions;
    }

    std::vector<float> computeGains (Vec3 direction, const std::vector<Vec3>& layoutDirections,
                                      const std::vector<Region>& regions)
    {
        std::vector<float> gains (layoutDirections.size(), 0.0f);
        if (layoutDirections.empty())
            return gains;

        for (auto& r : regions)
        {
            if (r.c < 0)
            {
                // Horizontal pair: pan using only the azimuthal (x/y)
                // component of `direction` -- a layout with no height
                // speakers has nowhere else to put any elevation content,
                // so it's folded down onto the horizontal ring by design.
                Vec3 flatDir { direction.x, direction.y, 0.0f };
                const float flatLen = flatDir.length();
                if (flatLen > 1.0e-6f)
                    flatDir = flatDir / flatLen;

                float g1 = 0.0f, g2 = 0.0f;
                if (! solve2x2 (layoutDirections[(size_t) r.a], layoutDirections[(size_t) r.b], flatDir, g1, g2))
                    continue;
                if (g1 < -gainEpsilon || g2 < -gainEpsilon)
                    continue;

                g1 = std::max (0.0f, g1);
                g2 = std::max (0.0f, g2);
                const float norm = std::sqrt (g1 * g1 + g2 * g2);
                if (norm < 1.0e-6f)
                    continue;

                gains[(size_t) r.a] = g1 / norm;
                gains[(size_t) r.b] = g2 / norm;
                return gains;
            }

            float g1 = 0.0f, g2 = 0.0f, g3 = 0.0f;
            if (! solve3x3 (layoutDirections[(size_t) r.a], layoutDirections[(size_t) r.b], layoutDirections[(size_t) r.c],
                             direction, g1, g2, g3))
                continue;
            if (g1 < -gainEpsilon || g2 < -gainEpsilon || g3 < -gainEpsilon)
                continue;

            g1 = std::max (0.0f, g1);
            g2 = std::max (0.0f, g2);
            g3 = std::max (0.0f, g3);
            const float norm = std::sqrt (g1 * g1 + g2 * g2 + g3 * g3);
            if (norm < 1.0e-6f)
                continue;

            gains[(size_t) r.a] = g1 / norm;
            gains[(size_t) r.b] = g2 / norm;
            gains[(size_t) r.c] = g3 / norm;
            return gains;
        }

        // Fallback: no region cleanly contains this direction (shouldn't
        // happen for a hull that actually covers the whole sphere/ring,
        // but guards against a near-degenerate layout) -- nearest single
        // speaker at full gain, rather than silence.
        int nearest = 0;
        float bestDot = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < (int) layoutDirections.size(); ++i)
        {
            const float d = direction.dot (layoutDirections[(size_t) i]);
            if (d > bestDot) { bestDot = d; nearest = i; }
        }
        gains[(size_t) nearest] = 1.0f;
        return gains;
    }
}
