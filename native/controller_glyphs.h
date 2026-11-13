#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ControllerGlyphs {

enum class Symbol { Square, Triangle, Circle, Cross, L1, R1, L2, R2 };

inline bool IsXboxAtlas(unsigned width, unsigned height, uint64_t hash) {
    if (width != 512) return false;
    if (height == 32) return hash == 0x02248876ea9adceaull || hash == 0x4ba8889a287e6c4eull;
    if (height == 512) return hash == 0xfe7a5c69e24c78ceull || hash == 0xc4973fe4f885a6b6ull;
    return false;
}

inline bool Stroke(float x, float y, float ax, float ay, float bx, float by, float radius) {
    const float dx = bx - ax, dy = by - ay;
    const float t = std::clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    const float distanceX = x - ax - t * dx, distanceY = y - ay - t * dy;
    return distanceX * distanceX + distanceY * distanceY <= radius * radius;
}

inline bool Mark(Symbol symbol, float x, float y, float radius) {
    auto line = [&](float ax, float ay, float bx, float by) {
        return Stroke(x, y, ax, ay, bx, by, radius);
    };
    switch (symbol) {
        case Symbol::Square: {
            const float edge = std::max(std::abs(x), std::abs(y));
            return std::abs(edge - 0.53f) <= radius;
        }
        case Symbol::Triangle:
            return line(0, -0.64f, -0.62f, 0.48f) ||
                line(-0.62f, 0.48f, 0.62f, 0.48f) || line(0.62f, 0.48f, 0, -0.64f);
        case Symbol::Circle:
            return std::abs(std::sqrt(x*x + y*y) - 0.55f) <= radius;
        case Symbol::Cross:
            return line(-0.51f, -0.51f, 0.51f, 0.51f) || line(-0.51f, 0.51f, 0.51f, -0.51f);
        default: {
            const bool right = symbol == Symbol::R1 || symbol == Symbol::R2;
            const bool two = symbol == Symbol::L2 || symbol == Symbol::R2;
            const bool stem = line(-0.80f, -0.53f, -0.80f, 0.53f);
            const bool letter = right ?
                line(-0.80f, -0.53f, -0.36f, -0.53f) || line(-0.36f, -0.53f, -0.25f, -0.27f) ||
                line(-0.25f, -0.27f, -0.36f, 0) || line(-0.36f, 0, -0.80f, 0) ||
                line(-0.62f, 0, -0.25f, 0.53f) :
                line(-0.80f, 0.53f, -0.24f, 0.53f);
            const bool digit = two ?
                line(0.18f, -0.41f, 0.34f, -0.53f) || line(0.34f, -0.53f, 0.73f, -0.53f) ||
                line(0.73f, -0.53f, 0.81f, -0.22f) || line(0.81f, -0.22f, 0.19f, 0.53f) ||
                line(0.19f, 0.53f, 0.82f, 0.53f) :
                line(0.28f, -0.29f, 0.55f, -0.53f) || line(0.55f, -0.53f, 0.55f, 0.53f) ||
                line(0.28f, 0.53f, 0.83f, 0.53f);
            return stem || letter || digit;
        }
    }
}

struct Badge {
    float halfWidth, halfHeight, corner, scale, stroke;
};

inline void DrawBadge(std::vector<uint8_t>& pixels, unsigned width, unsigned height,
    float centerX, float centerY, Symbol symbol, Badge badge, bool replaceTile) {
    constexpr int samples = 4;
    for (int py = int(centerY - badge.halfHeight - 1); py < centerY + badge.halfHeight + 1; ++py) {
        for (int px = int(centerX - badge.halfWidth - 1); px < centerX + badge.halfWidth + 1; ++px) {
            if (px < 0 || py < 0 || unsigned(px) >= width || unsigned(py) >= height) continue;
            unsigned gray = 0, coverage = 0;
            for (int sy = 0; sy < samples; ++sy) for (int sx = 0; sx < samples; ++sx) {
                const float x = px + (sx + 0.5f) / samples - centerX;
                const float y = py + (sy + 0.5f) / samples - centerY;
                const float dx = std::max(0.0f, std::abs(x) - (badge.halfWidth - badge.corner));
                const float dy = std::max(0.0f, std::abs(y) - (badge.halfHeight - badge.corner));
                const float distance = dx * dx + dy * dy;
                if (distance > badge.corner * badge.corner) continue;
                const float inner = std::max(0.0f, badge.corner - 0.95f);
                gray += Mark(symbol, x / badge.scale, y / badge.scale, badge.stroke) ? 248u :
                    distance >= inner * inner ? 174u : 62u;
                ++coverage;
            }
            auto* pixel = &pixels[(unsigned(py) * width + unsigned(px)) * 4];
            if (!coverage) continue;
            if (replaceTile) {
                // The game filters the atlas with straight alpha.
                pixel[0] = pixel[1] = pixel[2] = uint8_t(gray / coverage);
                pixel[3] = uint8_t((coverage * 255 + 8) / 16);
            } else {
                // Preserve the controller diagram outside the badge.
                for (int channel = 0; channel < 3; ++channel)
                    pixel[channel] = uint8_t((gray + pixel[channel] * (16 - coverage) + 8) / 16);
            }
        }
    }
}

inline bool Render(std::vector<uint8_t>& pixels, unsigned width, unsigned height) {
    if (width != 512 || (height != 32 && height != 512) || pixels.size() != width * height * 4u) return false;
    if (height == 32) {
        auto drawTile = [&](unsigned tile, Symbol symbol, Badge badge) {
            for (unsigned y = 0; y < height; ++y)
                std::fill_n(&pixels[(y * width + tile * 32) * 4], 32 * 4, 0);
            DrawBadge(pixels, width, height, tile * 32 + 16.0f, 16.0f, symbol, badge, true);
        };
        const Badge face{13, 13, 13, 11, 0.10f};
        const Badge shoulder{14, 10, 3, 12, 0.093f};
        const Badge trigger{11, 13, 3, 10, 0.115f};
        drawTile(0, Symbol::Square, face);   // Xbox X maps to DualSense Square.
        drawTile(1, Symbol::Triangle, face);
        drawTile(2, Symbol::Circle, face);
        drawTile(3, Symbol::Cross, face);
        drawTile(4, Symbol::L1, shoulder);
        drawTile(5, Symbol::R1, shoulder);
        drawTile(6, Symbol::L2, trigger);
        drawTile(7, Symbol::R2, trigger);
    } else {
        const Badge face{8.5f, 8.5f, 8.5f, 7.2f, 0.14f};
        DrawBadge(pixels, width, height, 231.5f, 194.5f, Symbol::Square, face, false);
        DrawBadge(pixels, width, height, 251.5f, 174.5f, Symbol::Triangle, face, false);
        DrawBadge(pixels, width, height, 271.5f, 194.5f, Symbol::Circle, face, false);
        DrawBadge(pixels, width, height, 251.5f, 214.5f, Symbol::Cross, face, false);
    }
    return true;
}
}
