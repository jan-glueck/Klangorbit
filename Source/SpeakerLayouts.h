#pragma once
#include "Vec3.h"
#include <cmath>
#include <vector>

/**
    Fixed loudspeaker angle tables for the surround/Atmos-bed decoder
    targets (see AmbisonicsDecoder.h). Deliberately NOT a general
    "arbitrary array" system -- only the specific named layouts
    DecoderMode lists, matching real hardware/software delivery formats
    rather than a fully general loudspeaker configuration tool.

    Angle conventions match the rest of the codebase (see
    AmbisonicsEncoder.h): azimuth in radians, 0 = front, positive
    counter-clockwise (mathematically positive, i.e. toward +y/left);
    elevation in radians, 0 = horizontal, +pi/2 = up.

    Sources (see each table's own comment for specifics):
    - Ear-level angles (L/R/C/Ls/Rs/Lss/Rss/Lrs/Rrs): ITU-R BS.775-4,
      "Multichannel stereophonic sound system with and without
      accompanying picture" -- the standard also defining 5.1/7.1.
    - Height/top channel angles for the Atmos-bed layouts (.2/.4
      variants): ITU-R BS.2051-2, "Advanced sound system for programme
      production", which specifies these as ANGLE SECTORS (a permitted
      range), not single fixed values -- e.g. top-front speakers are
      permitted anywhere in azimuth +-30..45 deg / elevation +30..55 deg.
      The concrete values below (+-45 deg azimuth / +45 deg elevation
      front, +-135 deg azimuth / +45 deg elevation rear) were chosen as
      round numbers within those permitted sectors, and were cross-checked
      against Dolby's own commonly published consumer height-speaker
      placement guidance (45 deg front / 135 deg rear, "45 deg" cited as
      the ideal single elevation) -- i.e. a defensible, documented choice
      within the standard's tolerance, not an invented number, but also
      not a literal quote of one single "the" official angle (the
      standard doesn't specify one).
*/
namespace SpeakerLayouts
{
    struct Speaker
    {
        Vec3 direction;      // unit vector, world space (same convention as SoundObject positions)
        bool isLfe = false;  // LFE channels carry no directional Ambisonics content -- see AmbisonicsDecoder's LFE handling
    };

    // JUCE's juce::MathConstants isn't pulled in by this header on purpose
    // (kept dependency-light, like Vec3.h) -- a local pi constant avoids
    // needing juce_core just for one value.
    constexpr float pi = 3.14159265358979323846f;

    inline Vec3 directionFromAngles (float azimuthRad, float elevationRad)
    {
        return { std::cos (elevationRad) * std::cos (azimuthRad),
                 std::cos (elevationRad) * std::sin (azimuthRad),
                 std::sin (elevationRad) };
    }

    // Degrees -> radians, purely for readability of the tables below (matches
    // the angles as commonly published in degrees in the cited standards).
    inline float deg (float degrees) { return degrees * (pi / 180.0f); }

    // --- Quad (4 channels, no LFE) ---------------------------------------
    // Simple equally-spaced square arrangement, +-45/+-135 deg -- the
    // conventional "quadraphonic" layout (matches
    // juce::AudioChannelSet::quadraphonic()'s L/R/Ls/Rs ordering).
    inline std::vector<Speaker> quad()
    {
        return {
            { directionFromAngles (deg (45.0f), 0.0f) },   // L
            { directionFromAngles (deg (-45.0f), 0.0f) },  // R
            { directionFromAngles (deg (135.0f), 0.0f) },  // Ls
            { directionFromAngles (deg (-135.0f), 0.0f) }, // Rs
        };
    }

    // --- 5.1 (6 channels: L R C LFE Ls Rs) -- ITU-R BS.775-4 -------------
    inline std::vector<Speaker> surround5point1()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },    // L
            { directionFromAngles (deg (-30.0f), 0.0f) },   // R
            { directionFromAngles (0.0f, 0.0f) },           // C
            { {}, true },                                    // LFE, no direction
            { directionFromAngles (deg (110.0f), 0.0f) },   // Ls
            { directionFromAngles (deg (-110.0f), 0.0f) },  // Rs
        };
    }

    // --- 7.1 (8 channels: L R C LFE Lss Rss Lrs Rrs) -- ITU-R BS.775-4 ---
    // Side surrounds at +-90 deg, rear surrounds at +-135 deg -- both
    // within BS.775-4's permitted sectors (side 90-110 deg, rear 135-150 deg),
    // chosen as the commonly-used nominal defaults.
    inline std::vector<Speaker> surround7point1()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },    // L
            { directionFromAngles (deg (-30.0f), 0.0f) },   // R
            { directionFromAngles (0.0f, 0.0f) },           // C
            { {}, true },                                    // LFE, no direction
            { directionFromAngles (deg (90.0f), 0.0f) },    // Lss
            { directionFromAngles (deg (-90.0f), 0.0f) },   // Rss
            { directionFromAngles (deg (135.0f), 0.0f) },   // Lrs
            { directionFromAngles (deg (-135.0f), 0.0f) },  // Rrs
        };
    }

    // --- 5.1.2 (8 ch: 5.1 bed + top-side L/R) -- ear level per BS.775-4, ---
    // height per BS.2051-2's top-side sector, see this file's own comment.
    inline std::vector<Speaker> atmos5point1point2()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },
            { directionFromAngles (deg (-30.0f), 0.0f) },
            { directionFromAngles (0.0f, 0.0f) },
            { {}, true },
            { directionFromAngles (deg (110.0f), 0.0f) },
            { directionFromAngles (deg (-110.0f), 0.0f) },
            { directionFromAngles (deg (90.0f), deg (45.0f)) },   // top-side left
            { directionFromAngles (deg (-90.0f), deg (45.0f)) },  // top-side right
        };
    }

    // --- 5.1.4 (10 ch: 5.1 bed + top-front L/R + top-rear L/R) -----------
    inline std::vector<Speaker> atmos5point1point4()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },
            { directionFromAngles (deg (-30.0f), 0.0f) },
            { directionFromAngles (0.0f, 0.0f) },
            { {}, true },
            { directionFromAngles (deg (110.0f), 0.0f) },
            { directionFromAngles (deg (-110.0f), 0.0f) },
            { directionFromAngles (deg (45.0f), deg (45.0f)) },    // top-front left
            { directionFromAngles (deg (-45.0f), deg (45.0f)) },   // top-front right
            { directionFromAngles (deg (135.0f), deg (45.0f)) },   // top-rear left
            { directionFromAngles (deg (-135.0f), deg (45.0f)) },  // top-rear right
        };
    }

    // --- 7.1.2 (10 ch: 7.1 bed + top-side L/R) ----------------------------
    inline std::vector<Speaker> atmos7point1point2()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },
            { directionFromAngles (deg (-30.0f), 0.0f) },
            { directionFromAngles (0.0f, 0.0f) },
            { {}, true },
            { directionFromAngles (deg (90.0f), 0.0f) },
            { directionFromAngles (deg (-90.0f), 0.0f) },
            { directionFromAngles (deg (135.0f), 0.0f) },
            { directionFromAngles (deg (-135.0f), 0.0f) },
            { directionFromAngles (deg (90.0f), deg (45.0f)) },
            { directionFromAngles (deg (-90.0f), deg (45.0f)) },
        };
    }

    // --- 7.1.4 (12 ch: 7.1 bed + top-front L/R + top-rear L/R) -----------
    inline std::vector<Speaker> atmos7point1point4()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },
            { directionFromAngles (deg (-30.0f), 0.0f) },
            { directionFromAngles (0.0f, 0.0f) },
            { {}, true },
            { directionFromAngles (deg (90.0f), 0.0f) },
            { directionFromAngles (deg (-90.0f), 0.0f) },
            { directionFromAngles (deg (135.0f), 0.0f) },
            { directionFromAngles (deg (-135.0f), 0.0f) },
            { directionFromAngles (deg (45.0f), deg (45.0f)) },
            { directionFromAngles (deg (-45.0f), deg (45.0f)) },
            { directionFromAngles (deg (135.0f), deg (45.0f)) },
            { directionFromAngles (deg (-135.0f), deg (45.0f)) },
        };
    }

    // Stereo virtual pair for the (non-HRTF) Stereo decode mode -- see
    // AmbisonicsDecoder.h's own comment on why this is handled separately
    // from Binaural. +-30 deg is the conventional stereo-pair angle
    // (matches ITU-R BS.775-4's L/R positions).
    inline std::vector<Speaker> stereoPair()
    {
        return {
            { directionFromAngles (deg (30.0f), 0.0f) },
            { directionFromAngles (deg (-30.0f), 0.0f) },
        };
    }

    // --- Generic regular circular array (horizontal-only, no LFE) -------
    // numSpeakers evenly spaced every 360/numSpeakers degrees, starting at
    // startAzimuthRad and sweeping toward positive azimuth (this file's
    // own counter-clockwise/left convention, see the header comment
    // above) -- a plain monotonic sweep, channel index == sweep order.
    // No symmetric-pair grouping (unlike quad()/surround5point1() etc.
    // above) -- an arbitrary N has no inherent "front stage" hierarchy to
    // preserve, so the simplest, most predictable convention was chosen:
    // channel 0 sits exactly at startAzimuthRad, every next channel one
    // step further around the ring. Used both directly (Circular Array
    // decoder mode, AmbisonicsDecoder::Mode::CircularArray, with
    // startAzimuthRad == 0, i.e. channel 0 = front) and indirectly, as
    // the conceptual basis for octophonic() below (which has its own,
    // deliberately different, hand-written channel ordering).
    inline std::vector<Speaker> circularArray (int numSpeakers, float startAzimuthRad)
    {
        std::vector<Speaker> speakers;
        speakers.reserve ((size_t) numSpeakers);
        const float step = (2.0f * pi) / (float) numSpeakers;
        for (int i = 0; i < numSpeakers; ++i)
            speakers.push_back ({ directionFromAngles (startAzimuthRad + step * (float) i, 0.0f) });
        return speakers;
    }

    // --- Octophonic (8 channels, no LFE, horizontal-only circular array) ---
    // Angle convention: +-22.5/+-67.5/+-112.5/+-157.5 deg -- a symmetric
    // front L/R pair straddling 0 deg, NOT a single speaker at dead-front
    // (0 deg) or dead-rear (180 deg). Chosen to match Blue Ripple Sound's
    // "O3A Decoder - Octagon" (an Ambisonics-native, B-format-ecosystem
    // decoder product, not a generic/unrelated convention) -- no single
    // canonical IEM/AllRAD-published octagon example was found during
    // research (IEM's own AllRADecoder/configuration-file docs don't ship
    // one), so this was a deliberate, documented choice rather than an
    // invented one: it matches the one concrete Ambisonics-ecosystem
    // reference found, and it mirrors this file's own existing pattern
    // for every other layout with a front L/R pair (quad(), 5.1, 7.1,
    // stereoPair() above -- all straddle front symmetrically).
    // Channel order is this project's OWN choice (Blue Ripple's own
    // channel numbering isn't reproduced -- there is no JUCE-named bus to
    // match against here, unlike the 5.1/7.1/Atmos layouts above, so
    // there was no external ordering to preserve): four symmetric L/R
    // pairs sweeping outward from front to back, L before R in each pair,
    // exactly like quad()'s and surround5point1()'s own ordering idiom.
    inline std::vector<Speaker> octophonic()
    {
        return {
            { directionFromAngles (deg (22.5f), 0.0f) },    // front-left
            { directionFromAngles (deg (-22.5f), 0.0f) },   // front-right
            { directionFromAngles (deg (67.5f), 0.0f) },    // side-left (front)
            { directionFromAngles (deg (-67.5f), 0.0f) },   // side-right (front)
            { directionFromAngles (deg (112.5f), 0.0f) },   // side-left (rear)
            { directionFromAngles (deg (-112.5f), 0.0f) },  // side-right (rear)
            { directionFromAngles (deg (157.5f), 0.0f) },   // rear-left
            { directionFromAngles (deg (-157.5f), 0.0f) },  // rear-right
        };
    }
}
