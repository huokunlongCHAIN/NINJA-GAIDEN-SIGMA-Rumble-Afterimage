#pragma once

#include "charge_effect.h"

constexpr unsigned SwallowFlightFrames = 14;
constexpr unsigned SwallowRevealFrames = 6;
constexpr unsigned SwallowTrailFrames = 13;

inline unsigned SwallowRoute(unsigned script) {
    // The 0x51 launch cue and the rest of the sword dash script. The wall
    // and free-jump inputs can each choose either facing variant.
    if (script >= 0x19f614 && script <= 0x19f6f2) return 490;
    if (script >= 0x19f85c && script <= 0x19f936) return 494;
    if (script >= 0x1c7b1a && script <= 0x1c7ba8) return 2720;
    return 0;
}

struct SwallowTimeline {
    unsigned route = 0;
    unsigned frame = 0;
    uint64_t reveal = 0;
    uint64_t pauseFrames = 0;
    bool sample = false;

    void Advance(unsigned script, float motion, uint64_t rendered) {
        const unsigned next = SwallowRoute(script);
        const unsigned phase = std::isfinite(motion) && motion >= 1 && motion <= SwallowFlightFrames ?
            static_cast<unsigned>(motion) : 0;
        const bool continuing = route && route == next && phase >= frame;
        const bool starting = next && phase && phase <= 3 && route != next;
        sample = starting || (continuing && phase > frame);
        if (starting || continuing) {
            if (!sample) ++pauseFrames;
            route = next;
            frame = phase;
            reveal = 0;
        } else {
            if (route) reveal = rendered;
            route = frame = 0;
            if (reveal && rendered - reveal + 1 >= SwallowRevealFrames) reveal = 0;
        }
    }

    float BodyOpacity(uint64_t rendered) const {
        if (route) return frame == 1 ? 0.40f : 0;
        return reveal ? RevealAlpha(rendered - reveal + 1, 0, SwallowRevealFrames) : 1;
    }
};

constexpr unsigned FeatherRevealFrames = 8;

struct FeatherFrame {
    unsigned frame = 0;
    unsigned first = 0;
    unsigned impact = 0;

    float BodyOpacity() const {
        if (!frame) return 1;
        if (frame == first) return 0.40f;
        return RevealAlpha(frame, impact, FeatherRevealFrames);
    }
};

inline FeatherFrame WhiteFeatherFrame(unsigned script, float motion) {
    if (!std::isfinite(motion)) return {};
    // Script 427 reaches its apex at about f113 in the sword root-height track.
    // Script 1347 uses another landing branch, with its ground cue at f127.
    if (script >= 0x19c99a && script < 0x19cb9c && motion >= 113 && motion <= 125 + FeatherRevealFrames)
        return {static_cast<unsigned>(motion), 113, 125};
    if (script >= 0x1ae6be && script < 0x1ae844 && motion >= 114 && motion <= 127 + FeatherRevealFrames)
        return {static_cast<unsigned>(motion), 114, 127};
    return {};
}
