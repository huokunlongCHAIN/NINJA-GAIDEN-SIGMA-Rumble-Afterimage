#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class ChargeMove { None, ET, UT };
enum class ChargeFamily { Dragon, DragonSpin, Chain, Heavy, Lunar, Dual };
enum class ChargeVisual { Full, AirTrail, Off };

struct ChargeFrame {
    ChargeMove move = ChargeMove::None;
    unsigned frame = 0;
    ChargeFamily family = ChargeFamily::Dragon;
    ChargeVisual visual = ChargeVisual::Full;
};

constexpr unsigned ChargeReturnFrame = 72;
constexpr unsigned AnchorEndAge = 63;
constexpr unsigned ChargeTravelAge = 10;
constexpr unsigned WindRunApexFrame = 19;
constexpr unsigned WindRunStartFrame = 6;
constexpr unsigned WindRunLastTrailFrame = 14;
constexpr unsigned WindRunLastHiddenFrame = 13;
constexpr unsigned WindRunRevealFrames = 9;

inline ChargeFrame ReadChargeFrame(unsigned script, float motionFrame) {
    if (!std::isfinite(motionFrame) || motionFrame < 1 || motionFrame > ChargeReturnFrame) return {};
    const auto frame = static_cast<unsigned>(motionFrame);
    struct Release {
        unsigned start, end;
        ChargeMove move;
        ChargeFamily family;
        unsigned windup = 0;
        ChargeVisual visual = ChargeVisual::Full;
    };
    // The common Ryu bank dispatches weapon variants through these releases.
    // Header pointers alone can be visible during a preceding action.
    constexpr Release releases[]{
        {0x19c4f8, 0x19c6de, ChargeMove::UT, ChargeFamily::Dragon},
        {0x19e62a, 0x19e824, ChargeMove::ET, ChargeFamily::Dragon},
        {0x19e89a, 0x19ea9c, ChargeMove::UT, ChargeFamily::Dragon},
        {0x1b0330, 0x1b06ae, ChargeMove::ET, ChargeFamily::DragonSpin, 0, ChargeVisual::Off},
        {0x1b06c4, 0x1b0a5c, ChargeMove::UT, ChargeFamily::DragonSpin, 0, ChargeVisual::Off},
        {0x1b6396, 0x1b65c8, ChargeMove::ET, ChargeFamily::Chain},
        {0x1b65d2, 0x1b67c4, ChargeMove::UT, ChargeFamily::Chain},
        {0x1ba1fa, 0x1ba3b0, ChargeMove::ET, ChargeFamily::Heavy, 0, ChargeVisual::AirTrail},
        {0x1ba3c6, 0x1ba590, ChargeMove::UT, ChargeFamily::Heavy, 0, ChargeVisual::AirTrail},
        // The Dark Dragon Blade has distinct long-windup ET and UT motions.
        {0x1ba7fe, 0x1baa2e, ChargeMove::ET, ChargeFamily::Heavy, 4, ChargeVisual::Off},
        {0x1baa38, 0x1babcc, ChargeMove::UT, ChargeFamily::Heavy, 15},
        // Unlabored Flawlessness's dedicated UT winds up before the jump.
        {0x1bb07e, 0x1bb2a0, ChargeMove::UT, ChargeFamily::Heavy, 6, ChargeVisual::AirTrail},
        // Dabilahro's ET and UT share the jumping slash motion (11955).
        {0x1bb714, 0x1bb8d6, ChargeMove::ET, ChargeFamily::Heavy, 0, ChargeVisual::AirTrail},
        {0x1bb8e0, 0x1bbac2, ChargeMove::UT, ChargeFamily::Heavy, 0, ChargeVisual::AirTrail},
        {0x1c04fc, 0x1c077a, ChargeMove::ET, ChargeFamily::Lunar},
        {0x1c0790, 0x1c0dae, ChargeMove::UT, ChargeFamily::Lunar},
        {0x1c893a, 0x1c8b3e, ChargeMove::ET, ChargeFamily::Dual},
        {0x1c8b48, 0x1c8d40, ChargeMove::UT, ChargeFamily::Dual},
    };
    for (const auto& release : releases)
        if (script >= release.start && script < release.end && frame > release.windup)
            return {release.move, frame - release.windup, release.family, release.visual};
    return {};
}

inline bool ChargeFollowThrough(unsigned script) {
    return (script >= 0x19c6e8 && script < 0x19cb9c) ||
        (script >= 0x1ae360 && script < 0x1ae844) ||
        (script >= 0x1aea4c && script < 0x1aeba0) ||
        (script >= 0x1b5b38 && script < 0x1b5ee0) ||
        (script >= 0x1babcc && script < 0x1baec2) ||
        (script >= 0x1cab66 && script < 0x1cada8);
}

inline float AnchorAlpha(uint64_t age) {
    if (age <= ChargeTravelAge) return 1;
    if (age < 40) return 1.0f - 0.40f * float(age - ChargeTravelAge) / (40 - ChargeTravelAge);
    return age < AnchorEndAge ? 0.60f * float(AnchorEndAge - age) / (AnchorEndAge - 40) : 0;
}

inline unsigned ChargeHiddenAge(ChargeMove move) {
    return move == ChargeMove::UT ? 22 : 19;
}

inline float RevealAlpha(uint64_t age, unsigned lastHidden, unsigned revealFrames) {
    if (age <= lastHidden) return 0;
    return std::min(1.0f, float(age - lastHidden) / float(revealFrames));
}

inline float ChargeBodyAlpha(ChargeMove move, uint64_t age, unsigned revealFrames,
    ChargeVisual visual = ChargeVisual::Full) {
    if (visual == ChargeVisual::Off) return 1;
    if (visual == ChargeVisual::AirTrail && age < ChargeTravelAge)
        return age + 1 == ChargeTravelAge ? 0.40f : 1;
    if (!age) return 0.40f;
    return RevealAlpha(age, ChargeHiddenAge(move), revealFrames);
}

struct ChargeTimeline {
    ChargeFrame current;
    ChargeFrame lastReading;
    unsigned firstFrame = 0;
    uint64_t chainRender = 0;
    unsigned chainAge = 0;
    ChargeMove chainMove = ChargeMove::None;
    ChargeVisual chainVisual = ChargeVisual::Full;

    bool Advance(ChargeFrame reading, bool followThrough, uint64_t rendered) {
        const bool began = reading.move != ChargeMove::None &&
            (reading.move != lastReading.move || reading.family != lastReading.family ||
                reading.visual != lastReading.visual) &&
            reading.frame <= 3;
        lastReading = reading;
        if (!followThrough) chainRender = 0;
        if (followThrough && current.move != ChargeMove::None && reading.move == ChargeMove::None) {
            chainRender = rendered;
            chainAge = current.frame - firstFrame + 1;
            chainMove = current.move;
            chainVisual = current.visual;
        }
        if (began) {
            firstFrame = reading.frame;
            current = reading;
        } else if (reading.move == current.move && reading.family == current.family &&
            reading.visual == current.visual && reading.frame >= current.frame) {
            current = reading;
        } else {
            // The motion clock can reset just before the old script exits.
            // Keep the script latched until a different action is observed.
            current = {};
        }
        return began;
    }

    unsigned Age() const {
        return current.move == ChargeMove::None ? 0 : current.frame - firstFrame;
    }

    bool CapturesOrigin() const {
        return current.move != ChargeMove::None && current.visual == ChargeVisual::Full;
    }

    bool SamplesTrail() const {
        const auto age = Age();
        return current.move != ChargeMove::None && current.visual != ChargeVisual::Off &&
            age >= ChargeTravelAge && age <= ChargeHiddenAge(current.move) + 2;
    }

    float BodyOpacity(uint64_t rendered, unsigned etReveal, unsigned utReveal) const {
        if (current.move != ChargeMove::None) {
            return ChargeBodyAlpha(current.move, Age(), current.move == ChargeMove::UT ? utReveal : etReveal,
                current.visual);
        }
        if (chainRender)
            return ChargeBodyAlpha(chainMove, chainAge + rendered - chainRender,
                chainMove == ChargeMove::UT ? utReveal : etReveal, chainVisual);
        return 1;
    }
};

inline float WindRunBodyAlpha(unsigned frame) {
    if (frame < WindRunStartFrame) return 1;
    if (frame == WindRunStartFrame) return 0.40f;
    return RevealAlpha(frame, WindRunLastHiddenFrame, WindRunRevealFrames);
}

