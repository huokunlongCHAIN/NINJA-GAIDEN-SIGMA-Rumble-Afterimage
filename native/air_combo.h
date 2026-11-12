#pragma once

#include <cmath>
#include <cstdint>

constexpr unsigned AirOriginFrames = 36;
constexpr unsigned AirFadeFrames = 16;
// In the reference the first moving silhouette follows the impact flash by about two frames.
constexpr unsigned AirFirstTrailFrame = 5;
constexpr unsigned AirTrailCount = 3;
constexpr unsigned AirLastTrailFrame = 13;
constexpr unsigned DescentStartFrame = 25;
constexpr unsigned DescentLandingFrame = 38;

inline float AirFade(uint64_t age) {
    return age < AirFadeFrames ? float(AirFadeFrames - age) / AirFadeFrames : 0;
}

inline float AirOriginFade(uint64_t age) {
    if (age <= 4) return 1;
    if (age < 17) return 1.0f - 0.64f * float(age - 4) / 13;
    return age < AirOriginFrames ? 0.36f * float(AirOriginFrames - age) / (AirOriginFrames - 17) : 0;
}

struct AirCombo {
    bool armed = false;
    unsigned branch = 0;
    unsigned animationFrame = 0;
    unsigned lastSampleFrame = 0;
    unsigned samples = 0;
    uint64_t serial = 0;
    uint64_t firstRenderFrame = 0;
    uint64_t trailSerial = 0;
    bool trailCapture = false;
    bool descending = false;
    bool descentCapture = false;

    bool Advance(unsigned script, float reading, uint64_t rendered) {
        trailCapture = false;
        descentCapture = false;
        const bool wasDescending = descending;
        descending = false;
        if (!std::isfinite(reading) || reading < 1 || reading > 100) return false;
        unsigned frame = static_cast<unsigned>(reading);
        if (script >= 0x1aea4c && script < 0x1aeba0) {
            armed = true;
            branch = 0;
            return false;
        }
        if (!armed) return false;
        unsigned next = 0;
        if (script >= 0x1aeba0 && script < 0x1aec9e) next = 1351;
        else if (script >= 0x1aec9e && script < 0x1aed80) next = 1352;
        else if (script >= 0x1aed80 && script < 0x1aee66) next = 1353;
        else if (script >= 0x1aee66 && script < 0x1aef78) {
            branch = 0;
            descending = frame >= DescentStartFrame && frame < DescentLandingFrame;
            descentCapture = descending && (!wasDescending || frame != animationFrame);
            animationFrame = frame;
            return false;
        } else {
            armed = false;
            branch = 0;
            return false;
        }
        // A script PC can already point to the next header while its last frame is displayed.
        const bool start = frame <= 3 && (next != branch || frame < animationFrame);
        if (start) {
            ++serial;
            branch = next;
            lastSampleFrame = 0;
            samples = 0;
            firstRenderFrame = rendered;
        }
        animationFrame = frame;
        // Fill the three poses from distinct frames that actually reached the renderer.
        if (next == branch && frame >= AirFirstTrailFrame && frame <= AirLastTrailFrame &&
            frame > lastSampleFrame && samples < AirTrailCount) {
            trailCapture = true;
            ++trailSerial;
            ++samples;
            lastSampleFrame = frame;
        }
        return start;
    }
};
