#pragma once

#include "charge_effect.h"

constexpr unsigned WallDiveStartFrame = 26;
constexpr unsigned WallLandingHoldFrames = 7;
constexpr unsigned WallLandingRevealFrames = 8;

enum class WallFamily { Sword, Chain, Heavy, Lunar, Dual, DualAlternate };

struct WallDiveRoute {
    unsigned begin, end;
    WallFamily family;
};

// The Ryu script bank contains three launch variants for each of these moves.
// Each carries the wall-downslash cue (0x21000020) and its own recovery.
inline constexpr WallDiveRoute WallDiveRoutes[]{
    {0x1a16e0, 0x1a17f0, WallFamily::Sword},
    {0x1a17f0, 0x1a1900, WallFamily::Sword},
    {0x1a190c, 0x1a1a1c, WallFamily::Sword},
    {0x1b5506, 0x1b55c8, WallFamily::Chain},
    {0x1b55c8, 0x1b568a, WallFamily::Chain},
    {0x1b5696, 0x1b5760, WallFamily::Chain},
    {0x1b970c, 0x1b97d8, WallFamily::Heavy},
    {0x1b97d8, 0x1b98a4, WallFamily::Heavy},
    {0x1b98a4, 0x1b9970, WallFamily::Heavy},
    {0x1bf7e6, 0x1bf8d4, WallFamily::Lunar},
    {0x1bf8d4, 0x1bf9c2, WallFamily::Lunar},
    {0x1bf9ce, 0x1bfabc, WallFamily::Lunar},
    {0x1c7ff2, 0x1c80d0, WallFamily::Dual},
    {0x1c80d0, 0x1c81ae, WallFamily::Dual},
    {0x1c81ba, 0x1c8298, WallFamily::Dual},
    {0x1c9016, 0x1c90ee, WallFamily::Dual},
    {0x1c90ee, 0x1c91c6, WallFamily::Dual},
    {0x1c91c6, 0x1c929e, WallFamily::Dual},
    {0x1cb7ee, 0x1cb8cc, WallFamily::DualAlternate},
    {0x1cb8cc, 0x1cb9ae, WallFamily::DualAlternate},
    {0x1cb9ae, 0x1cba90, WallFamily::DualAlternate},
    {0x1cba90, 0x1cbb68, WallFamily::DualAlternate},
    {0x1cbb68, 0x1cbc44, WallFamily::DualAlternate},
    {0x1cbc44, 0x1cbd20, WallFamily::DualAlternate},
};

enum class WallRecovery { None, Falling, Landing };

inline WallRecovery ReadWallRecovery(WallFamily family, unsigned script) {
    struct Recovery {
        WallFamily family;
        unsigned falling, landing, end;
    };
    constexpr Recovery recoveries[]{
        {WallFamily::Sword, 0x1a1a28, 0x1a1a28, 0x1a1d1e},
        {WallFamily::Sword, 0x1a1e1c, 0x1a1e1c, 0x1a1f60},
        {WallFamily::Chain, 0x1b5760, 0x1b5794, 0x1b588e},
        {WallFamily::Heavy, 0x1b9970, 0x1b9998, 0x1b9b64},
        {WallFamily::Lunar, 0x1bfabc, 0x1bfaec, 0x1bfdba},
        {WallFamily::Dual, 0x1c8298, 0x1c82c4, 0x1c846a},
        {WallFamily::Dual, 0x1c929e, 0x1c92ca, 0x1c941a},
        {WallFamily::DualAlternate, 0x1cbd20, 0x1cbdf0, 0x1cbf4a},
    };
    for (const auto& phase : recoveries) {
        if (phase.family == family && script >= phase.falling && script < phase.end)
            return script >= phase.landing ? WallRecovery::Landing : WallRecovery::Falling;
    }
    return WallRecovery::None;
}

struct WallDiveTimeline {
    const WallDiveRoute* route = nullptr;
    bool diving = false;
    uint64_t start = 0;
    uint64_t landing = 0;

    void Advance(unsigned script, float motion, uint64_t rendered) {
        if (motion >= WallDiveStartFrame && motion < 80) {
            // The VM may park at the next header while the current motion is still airborne.
            if (diving && script == route->end) return;
            for (const auto& candidate : WallDiveRoutes) {
                if (script < candidate.begin + 10 || script >= candidate.end) continue;
                if (!diving || route != &candidate) start = rendered;
                route = &candidate;
                diving = true;
                landing = 0;
                return;
            }
        }
        if (route && std::isfinite(motion) && motion >= 1) {
            const auto recovery = ReadWallRecovery(route->family, script);
            if (diving && recovery == WallRecovery::Falling) return;
            if (recovery == WallRecovery::Landing) {
                if (diving) landing = rendered;
                diving = false;
                return;
            }
        }
        *this = {};
    }

    float BodyOpacity(uint64_t rendered) const {
        if (diving) return rendered == start ? 0.40f : 0;
        if (landing) return RevealAlpha(rendered - landing, WallLandingHoldFrames, WallLandingRevealFrames);
        return 1;
    }
};
