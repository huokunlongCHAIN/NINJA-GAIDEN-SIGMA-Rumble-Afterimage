#pragma once
#include <string>

namespace AfterimageHlsl {
constexpr char GripSkinMask[] = R"(
    bool gripSkin(float2 uv) {
        float2 p = frac(uv) * 512.0;
        float left = p.y < 182 ? lerp(384, 363, (p.y - 68) / 114) :
            p.y < 250 ? lerp(363, 267, (p.y - 182) / 68) : lerp(267, 296, (p.y - 250) / 162);
        float right = p.y < 328 ? lerp(520, 487, (p.y - 68) / 260) :
            p.y < 350 ? lerp(487, 427, (p.y - 328) / 22) : lerp(427, 416, (p.y - 350) / 62);
        return p.y < 68 || p.y > 413 || p.x < left || p.x > right;
    }
)";

constexpr char SceneOcclusion[] = R"(
    Texture2D<float> sceneDepth : register(t2);
    cbuffer SceneOcclusion : register(b10) {
        float2 depthScale;
        float depthDirection;
        float depthBias;
    }
    void occlude(float4 pos) {
        if (depthDirection != 0) {
            float nearest = sceneDepth.Load(int3(int2(pos.xy * depthScale), 0));
            clip(depthBias + (nearest - pos.z) * depthDirection);
        }
    })";

inline const std::string Ghost = std::string(SceneOcclusion) + R"(
    cbuffer GhostLighting : register(b12) { float anchorOpacity; float brightness; }
    cbuffer GhostColor : register(b13) { float4 color; }
    float4 main(float4 pos : SV_Position) : SV_Target {
        occlude(pos);
        float opacity = anchorOpacity > 0 ? anchorOpacity : color.a;
        return float4(color.rgb * (opacity * brightness), opacity);
    })";
constexpr char underlayLighting[] = R"(
    float underlayLight(float4 pos) {
        float z = rcp(max(abs(pos.w), 0.0001));
        float3 normal = normalize(float3(-ddx(z), ddy(z), z * 0.0015));
        return 0.36 + 0.64 * saturate(dot(normal, float3(-0.42, 0.44, 0.794)));
    })";
const std::string underlay = std::string(underlayLighting) + R"(
    float4 main(float4 pos : SV_Position) : SV_Target { return float4(underlayLight(pos).xxx, 1); }
)";
const std::string gripUnderlay = std::string(underlayLighting) + GripSkinMask + R"(
    float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
        float light = underlayLight(pos) * (gripSkin(uv) ? 1.0 : 0.0);
        return float4(light.xxx, 1);
    }
)";
constexpr char fullscreen[] = R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
        float2 uv = float2((id << 1) & 2, id & 2);
        return float4(uv * float2(2,-2) + float2(-1,1), 0, 1);
    })";
constexpr char composite[] = R"(
    Texture2D<float4> scene : register(t0);
    Texture2DArray<float4> parts : register(t1);
    SamplerState linearSampler : register(s0);
    cbuffer Refraction : register(b12) {
        float pixels; float time; float emission; float jointOpacity;
        float4 layerOpacity[3];
        uint layerCount; uint playerLayer;
        float muscleHighlight; float glassRefraction;
        float surfaceBrightness;
    }
    cbuffer GhostColor : register(b13) { float4 color; }
    float4 surfaceAt(int2 p, uint layer) {
        float4 raw = parts.Load(int4(p, layer, 0));
        return float4(round(raw.x * 255.0), raw.y, raw.zw * 2.0 - 1.0);
    }
    int partOf(float label) { return (int)label & 15; }
    float filled(float2 surface) { return partOf(surface.x) != 0 ? 1.0 : 0.0; }
    bool playerAt(int2 p) {
        return playerLayer != 0xffffffff && partOf(surfaceAt(p, playerLayer).x) != 0;
    }
    bool connects(int a, int b) {
        return b != 0 && ((abs(a - b) == 1 && min(a, b) != 5) ||
            (min(a, b) == 2 && max(a, b) == 6));
    }
    float4 main(float4 pos : SV_Position) : SV_Target {
        uint w, h;
        scene.GetDimensions(w,h);
        int2 pixel = int2(pos.xy);
        if (playerAt(pixel)) discard;
        int width = max(1, (int)round(h / 430.0));
        int edgeRadius = clamp((int)round(h / 650.0), 1, 3);
        float alpha = 0;
        float3 light = 0;
        float3 blue = color.rgb * float3(0.70, 0.83, 1.0);
        float2 size = float2(w,h);
        float3 original = scene.Load(int3(pixel, 0)).rgb;
        float3 glassColor = original;
        [loop] for (uint i = 0; i < layerCount; ++i) {
            float4 surface = surfaceAt(pixel, i);
            int part = partOf(surface.x);
            if (part == 0) continue;
            float4 left = surfaceAt(pixel - int2(width,0), i);
            float4 right = surfaceAt(pixel + int2(width,0), i);
            float4 top = surfaceAt(pixel - int2(0,width), i);
            float4 bottom = surfaceAt(pixel + int2(0,width), i);
            float opacity = layerOpacity[i / 4][i % 4];
            if (jointOpacity < 1 &&
                (connects(part, partOf(left.x)) || connects(part, partOf(right.x)) ||
                 connects(part, partOf(top.x)) || connects(part, partOf(bottom.x)))) opacity *= jointOpacity;
            // Smooth shading only within the same anatomical part and material.
            float4 neighbors = float4(left.y, right.y, top.y, bottom.y);
            float4 labels = float4(left.x, right.x, top.x, bottom.x);
            float4 weights = 1 - saturate(abs(labels - surface.x));
            float brightness = (3 * surface.y + dot(neighbors, weights)) /
                (3 + dot(weights, float4(1, 1, 1, 1)));
            // Reduce coverage at the silhouette, especially at convex
            // corners. Internal material and anatomical seams stay solid.
            int2 x = int2(edgeRadius,0), y = int2(0,edgeRadius);
            float sides = edgeRadius == width ? filled(left.xy) + filled(right.xy) + filled(top.xy) + filled(bottom.xy) :
                filled(surfaceAt(pixel-x,i).xy) + filled(surfaceAt(pixel+x,i).xy) +
                filled(surfaceAt(pixel-y,i).xy) + filled(surfaceAt(pixel+y,i).xy);
            if (sides < 4) {
                float corners = filled(surfaceAt(pixel-x-y,i).xy) + filled(surfaceAt(pixel+x-y,i).xy) +
                    filled(surfaceAt(pixel-x+y,i).xy) + filled(surfaceAt(pixel+x+y,i).xy);
                float edge = (4.0 + 2.0 * sides + corners) / 16.0;
                opacity *= lerp(0.35, 1.0, smoothstep(0.20, 0.98, edge));
            }
            if (glassRefraction >= 0) {
                // The shape bends the scene behind it. A weak lavender
                // tint remains where the angle gives little refraction.
                float4 normalWeights = float4(filled(left.xy), filled(right.xy),
                    filled(top.xy), filled(bottom.xy));
                float2 direction = (surface.zw * 3 + left.zw * normalWeights.x + right.zw * normalWeights.y +
                    top.zw * normalWeights.z + bottom.zw * normalWeights.w) /
                    (3 + dot(normalWeights, float4(1,1,1,1)));
                float bend = saturate(length(direction));
                float rim = saturate((4 - sides) * 0.25);
                float2 shifted = clamp(pos.xy - direction * glassRefraction * (h / 720.0),
                    float2(0.5,0.5), size - 0.5);
                int2 source = int2(floor(shifted - 0.5));
                bool touchesPlayer = playerAt(source) || playerAt(source + int2(1,0)) ||
                    playerAt(source + int2(0,1)) || playerAt(source + int2(1,1));
                float3 bent = touchesPlayer ? original : scene.SampleLevel(linearSampler, shifted / size, 0).rgb;
                float transmission = saturate(opacity * (1.35 + 1.18 * bend));
                glassColor = lerp(glassColor, bent, transmission);
                float3 lavender = lerp(color.rgb, float3(0.64, 0.59, 0.84), 0.47);
                float frost = opacity * (0.16 + 0.30 * (1 - bend) + 0.22 * rim);
                float skin = ((int)surface.x >> 4) == 2 ? muscleHighlight : 0.25;
                float glint = opacity * emission * (0.11 + 0.32 * rim + 0.24 * bend * bend + 0.14 * brightness * skin);
                glassColor = glassColor * (1 - frost) + lavender * frost * 0.70 * surfaceBrightness +
                    lerp(lavender, float3(0.91,0.90,1.0), 0.37 * bend) * glint;
                alpha += opacity;
                continue;
            }
            float coverage = opacity * (0.44 + 0.48 * brightness);
            float highlight = ((int)surface.x >> 4) == 2 ? muscleHighlight : 1.0;
            float3 tone = lerp(blue, float3(0.97, 0.96, 1.0), smoothstep(0.64, 0.95, brightness) * highlight);
            float energy = opacity * emission * (0.11 + 1.72 * brightness * brightness) * highlight;
            light = tone * (coverage + energy) + light * (1 - coverage);
            alpha = coverage + alpha * (1 - coverage);
        }
        if (alpha <= 0) discard;
        if (glassRefraction >= 0) return float4(glassColor, 1);
        if (pixels <= 0) return float4(original * (1 - alpha) + light, 1);
        float2 p = pos.xy * (720.0 / h);
        float2 wave = float2(sin(p.y * 0.055 + time), cos(p.x * 0.05 - time));
        float2 shift = wave * (pixels * max(0.5, h / 720.0));
        float2 shifted = pos.xy + shift;
        int2 source = int2(floor(shifted - 0.5));
        bool touchesPlayer = playerAt(source) || playerAt(source + int2(1,0)) ||
            playerAt(source + int2(0,1)) || playerAt(source + int2(1,1));
        float3 bent = touchesPlayer ? original : scene.SampleLevel(linearSampler, shifted / size, 0).rgb;
        float3 glass = lerp(original, bent, saturate(alpha * 1.5));
        return float4(glass * (1 - alpha) + light, 1);
    })";
const std::string partLighting = std::string(SceneOcclusion) + R"(
    cbuffer Part : register(b11) { float id; float minimum; float range; }
    cbuffer Viewport : register(b12) { float invHeight; }
    float4 surface(float4 pos, float label, float low, float span) {
        occlude(pos);
        float z = rcp(max(abs(pos.w), 0.0001));
        float3 normal = normalize(float3(-ddx(z), ddy(z), z * invHeight * 1.1));
        float diffuse = pow(saturate(dot(normal, float3(-0.42, 0.44, 0.794))), 1.35);
        float grazing = 1.0 - normal.z;
        float level = saturate(low + span * (diffuse * 0.76 + grazing * 0.24));
        return float4(label / 255.0, level, normal.xy * 0.5 + 0.5);
    })";
const std::string part = std::string(partLighting) + R"(
    float4 main(float4 pos : SV_Position) : SV_Target { return surface(pos, id, minimum, range); }
)";
// Only the finger and palm islands of albedo 21 contain skin.
const std::string glove = std::string(partLighting) + R"(
    float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
        uv = frac(uv);
        bool fingers = uv.x < 0.5 && uv.y < 0.5 && (uv.x >= 0.25 || uv.y >= 0.25);
        bool thumb = uv.x >= 0.875 && uv.y < 0.125;
        bool skin = fingers || thumb;
        return surface(pos, skin ? id + 16 : id, skin ? 0.72 : minimum, skin ? 0.28 : range);
    }
)";
const std::string grip = std::string(partLighting) + GripSkinMask + R"(
    float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
        bool skin = gripSkin(uv);
        return surface(pos, skin ? id + 16 : id, skin ? 0.72 : minimum, skin ? 0.28 : range);
    }
)";
}
