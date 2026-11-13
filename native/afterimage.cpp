#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "MinHook.h"
#include "game_compatibility.h"
#include "charge_effect.h"
#include "wall_dive.h"
#include "aerial_effect.h"
#include "air_combo.h"
#include "body_parts.h"
#include "costumes.h"
#include "render_hook_bridge.h"

namespace {
using Microsoft::WRL::ComPtr;
using Context = ID3D11DeviceContext;
constexpr UINT VertexSlots = 8;
constexpr UINT ConstantSlots = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
constexpr UINT ColorSlot = ConstantSlots - 1;
constexpr UINT ViewProjectionSlot = 2;
constexpr UINT MaskWidth = 160;
constexpr UINT MaskHeight = 90;
constexpr unsigned CaptureLimit = 320;
constexpr unsigned FadeSteps = 24;
constexpr unsigned PoseHistorySize = 12;
constexpr unsigned MaxGhostLayers = 12;
constexpr unsigned MaxSurfaceBrightness = 200;
constexpr unsigned MaxSigma2GlowStrength = 200;

enum Slot : unsigned {
    VsConstants = 7, PsShader = 9, VsShader = 11, Indexed = 12,
    PsConstants = 16, InputLayout = 17, VertexBuffers = 18, IndexBuffer = 19, IndexedInstanced = 20, Topology = 24,
    Targets = 33, TargetsAndUavs = 34, Blend = 35, Depth = 36,
    Viewports = 44, ClearDepthView = 53, Execute = 58, Clear = 110, Finish = 114
};

using SetBuffers = void (STDMETHODCALLTYPE*)(Context*, UINT, UINT, ID3D11Buffer* const*);
using SetVertex = void (STDMETHODCALLTYPE*)(Context*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
using SetIndex = void (STDMETHODCALLTYPE*)(Context*, ID3D11Buffer*, DXGI_FORMAT, UINT);
using SetLayout = void (STDMETHODCALLTYPE*)(Context*, ID3D11InputLayout*);
using SetTopology = void (STDMETHODCALLTYPE*)(Context*, D3D11_PRIMITIVE_TOPOLOGY);
using SetPS = void (STDMETHODCALLTYPE*)(Context*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
using SetVS = void (STDMETHODCALLTYPE*)(Context*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
using SetTargets = void (STDMETHODCALLTYPE*)(Context*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using SetTargetsUav = void (STDMETHODCALLTYPE*)(Context*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using SetBlend = void (STDMETHODCALLTYPE*)(Context*, ID3D11BlendState*, const FLOAT*, UINT);
using SetDepth = void (STDMETHODCALLTYPE*)(Context*, ID3D11DepthStencilState*, UINT);
using SetViewport = void (STDMETHODCALLTYPE*)(Context*, UINT, const D3D11_VIEWPORT*);
using ClearDepth = void (STDMETHODCALLTYPE*)(Context*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
using DrawIndexed = void (STDMETHODCALLTYPE*)(Context*, UINT, UINT, INT);
using DrawIndexedInstanced = void (STDMETHODCALLTYPE*)(Context*, UINT, UINT, UINT, INT, UINT);
using ExecuteList = void (STDMETHODCALLTYPE*)(Context*, ID3D11CommandList*, BOOL);
using FinishList = HRESULT (STDMETHODCALLTYPE*)(Context*, BOOL, ID3D11CommandList**);
using ClearContext = void (STDMETHODCALLTYPE*)(Context*);
using Present = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using GameFrame = void (*)(void*, void*);

struct VertexBinding {
    ID3D11Buffer* buffer = nullptr;
    UINT stride = 0;
    UINT offset = 0;
    D3D11_BUFFER_DESC description{};
};

struct Draw {
    UINT count;
    UINT first;
    INT base;
    UINT instance = 0;
    bool instanced = false;
};

struct Snapshot {
    uint64_t timeUs = 0;
    uint64_t renderFrame = 0;
    std::array<ComPtr<ID3D11Buffer>, ConstantSlots> constants;
    std::array<ComPtr<ID3D11Buffer>, VertexSlots> vertices;
};

struct OverlayDraw {
    Draw draw{};
    Snapshot pose;
    ComPtr<ID3D11VertexShader> shader;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> index;
    ComPtr<ID3D11Buffer> color;
    std::array<UINT, VertexSlots> strides{}, offsets{};
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
    UINT indexOffset = 0;
    float anchorOpacity = 0;
    float opacity = 0.48f;
    BodyPart bodyPart = BodyPart::Torso;
    GhostMaterial material = GhostMaterial::Fabric;
    uint64_t meshKey = 0;
    ID3D11Buffer* sourceVertex = nullptr;
};

struct PoseFrame {
    std::vector<OverlayDraw> parts;
    unsigned count = 0;
    uint64_t frame = 0;
    uint64_t timeUs = 0;
    unsigned fadeFrames = 0;
    uint64_t swallowClock = 0;

    void Begin(uint64_t renderFrame, uint64_t nowUs) {
        frame = renderFrame;
        timeUs = nowUs;
        count = 0;
        fadeFrames = 0;
        swallowClock = 0;
    }
};

struct Readback {
    unsigned id;
    ComPtr<ID3D11Texture2D> texture;
};

struct ContextState {
    std::array<VertexBinding, VertexSlots> vertices;
    std::array<ID3D11Buffer*, ConstantSlots> vsConstants{};
    ID3D11Buffer* index = nullptr;
    D3D11_BUFFER_DESC indexDesc{};
    DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
    UINT indexOffset = 0;
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    UINT psClasses = 0;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
    UINT targetCount = 0;
    ID3D11DepthStencilView* depthView = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    UINT stencil = 0;
    ID3D11BlendState* blendState = nullptr;
    std::array<FLOAT, 4> blendFactor{1, 1, 1, 1};
    UINT sampleMask = UINT_MAX;
    D3D11_VIEWPORT viewport{};
    UINT viewportCount = 0;
    bool uavsBound = false;
    bool unknownVertexSlots = false;
    std::vector<Readback> readbacks;
};

struct RefractionResources {
    ComPtr<ID3D11VertexShader> fullscreen;
    ComPtr<ID3D11PixelShader> composite, partShader, gloveShader, gripShader;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11BlendState> rgbBlend;
    ComPtr<ID3D11Texture2D> scene;
    ComPtr<ID3D11ShaderResourceView> sceneView, maskView;
    ComPtr<ID3D11DepthStencilState> partDepth;
    ComPtr<ID3D11Buffer> parameters;
    std::array<ComPtr<ID3D11Buffer>, 8 * 3> partLabels;
    std::vector<ComPtr<ID3D11RenderTargetView>> slices;
    D3D11_TEXTURE2D_DESC surface{};
    DXGI_FORMAT maskFormat = DXGI_FORMAT_UNKNOWN;
    bool unavailable = false;
};

struct SceneDepth {
    ComPtr<ID3D11Texture2D> source, snapshot;
    ComPtr<ID3D11DepthStencilView> sourceView;
    ComPtr<ID3D11ShaderResourceView> view;
    D3D11_TEXTURE2D_DESC desc{};
    D3D11_VIEWPORT viewport{};
    UINT subresource = 0;
    uint64_t frame = UINT64_MAX;
    bool reversed = false;
    bool frozen = false;
};

struct DeviceResources {
    ComPtr<ID3D11PixelShader> maskShader;
    ComPtr<ID3D11PixelShader> ghostShader;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11DepthStencilState> readDepth;
    ComPtr<ID3D11BlendState> ghostBlend;
    ComPtr<ID3D11BlendState> fadeBlend;
    ComPtr<ID3D11PixelShader> fadeUnderlay;
    ComPtr<ID3D11PixelShader> gripFadeUnderlay;
    ComPtr<ID3D11BlendState> fadeUnderlayBlend;
    ComPtr<ID3D11Buffer> ghostLighting;
    ComPtr<ID3D11Buffer> sceneDepthConstants;
    ComPtr<ID3D11Texture2D> mask;
    ComPtr<ID3D11RenderTargetView> maskTarget;
    ComPtr<Context> overlayContext;
    ComPtr<ID3D11RasterizerState> overlayRasterizer;
    ComPtr<ID3D11DepthStencilState> overlayStencil;
    ComPtr<ID3D11DepthStencilView> overlayCoverage;
    D3D11_TEXTURE2D_DESC coverageDesc{};
    RefractionResources refraction;
    SceneDepth sceneDepth;
    std::vector<OverlayDraw> overlayDraws;
    std::array<PoseFrame, PoseHistorySize> trail;
    unsigned nextTrail = 0;
    PoseFrame chargeOrigin;
    PoseFrame leadingPose;
    PoseFrame playerFront;
    uint64_t playerFrontUntilUs = 0;
    std::array<PoseFrame, 4> stompOrigins;
    std::array<PoseFrame, 3> airOrigins;
    std::array<PoseFrame, AirTrailCount * 2> airTrail;
    uint64_t chargeSerial = 0;
    uint64_t windSerial = 0;
    ComPtr<ID3D11Buffer> frameCamera;
    uint64_t cameraFrame = 0;
    std::array<ComPtr<ID3D11Buffer>, FadeSteps> colors;
    uint32_t colorKey = 0;
};

enum class Action { None, ET, UT, WindRun, WallDownslash, FlyingSwallow, WhiteFeather };

struct Appearance {
    unsigned alpha = 40;
    unsigned glow = 68;
    std::array<unsigned, 3> tint{144, 163, 253};
    unsigned muscleHighlight = 45;
    unsigned brightness = 100;
};

struct Settings {
    bool enabled = true;
    bool autoET = true;
    bool autoUT = true;
    bool autoWindRun = true;
    bool autoWallDownslash = true;
    bool autoSwallow = true;
    bool autoWhiteFeather = true;
    unsigned interval = 12;
    unsigned lifetime = 185;
    unsigned copies = 5;
    unsigned alpha = 40;
    unsigned previewMs = 4500;
    unsigned revealFrames = 9;
    unsigned utRevealFrames = 12;
    unsigned glowStrength = 68;
    unsigned muscleHighlight = 45;
    unsigned refractionPixels = 1;
    unsigned jointOpacity = 76;
    bool glassVisual = false;
    unsigned glassRefraction = 19;
    unsigned brightness = 100;
    std::array<unsigned, 3> tint{144, 163, 253};
};

std::recursive_mutex renderMutex;
std::mutex logMutex;
std::unordered_map<void*, void*> originals;
std::unordered_set<void*> sharedRenderTargets;
ControllerRenderHooks controllerHooks{};
std::unordered_map<Context*, ContextState> contexts;
std::unordered_map<ID3D11Device*, DeviceResources> resources;
std::unordered_map<ID3D11CommandList*, std::vector<Readback>> commandReadbacks;
std::unordered_set<uint64_t> captureKeys;
Settings settings;
std::array<Appearance, std::size(Costumes)> appearances;
Costume activeCostume = Costume::Unknown;
uint64_t costumeLastFrame = 0;
std::wstring directory;
std::wstring ini;
FILE* logFile = nullptr;
std::atomic<ULONGLONG> previewUntil{0};
std::atomic<ULONGLONG> captureUntil{0};
uintptr_t gameBase = 0;
Action activeAction = Action::None;
uint64_t lastActionFrame = 0;
ChargeTimeline chargeTimeline;
WallDiveTimeline wallDive;
SwallowTimeline swallow;
unsigned previousFeatherFrame = 0;
AirCombo airCombo;
uint64_t chargeSerial = 0;
uint64_t stompSerial = 0;
uint64_t stompRenderFrame = 0;
float previousStompFrame = 0;
bool stompLatched = false;
unsigned windRunBranch = 0;
float windRunFrame = 0;
uint64_t windRunSerial = 0;
struct EffectFrame {
    float body = 1;
    bool leading = false;
    unsigned trailFrames = 0;
    bool distinctPose = false;
    bool useSwallowClock = false;
};
EffectFrame effectFrame;
uint64_t samples = 0;
uint64_t intervalTotalUs = 0;
uint64_t intervalCount = 0;
unsigned captureNumber = 0;
uint64_t renderFrame = 1;
unsigned loggedOverlays = 0;
unsigned loggedPoses = 0;
unsigned loggedEyes = 0;
unsigned loggedStompPhases = 0;
Present originalPresent = nullptr;
GameFrame originalGameFrame = nullptr;
std::atomic<IDXGISwapChain*> gameSwap{nullptr};
unsigned captureSerial = 0;
std::atomic<bool> hooksReady{false};
thread_local bool drawingExtra = false;

uint64_t PoseClockUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

unsigned PlayerScript(uintptr_t base, float& motionFrame);

const char* ActionName(Action action) {
    switch (action) {
        case Action::ET: return "ET";
        case Action::UT: return "UT";
        case Action::WindRun: return "WindRun";
        case Action::WallDownslash: return "WallDownslash";
        case Action::FlyingSwallow: return "FlyingSwallow";
        case Action::WhiteFeather: return "WhiteFeather";
        default: return "None";
    }
}

unsigned WindRunBranch(unsigned script) {
    if (script >= 0x195b4c && script < 0x195c5a) return 41;
    if (script >= 0x195d10 && script < 0x195e0e) return 44;
    return 0;
}

void Log(const char* format, ...) {
    std::lock_guard<std::mutex> guard(logMutex);
    if (!logFile) return;
    fprintf(logFile, "%llu ", static_cast<unsigned long long>(GetTickCount64()));
    va_list args;
    va_start(args, format);
    vfprintf(logFile, format, args);
    va_end(args);
    fputc('\n', logFile);
    fflush(logFile);
}

void UpdateAction() {
    if (lastActionFrame == renderFrame) return;
    lastActionFrame = renderFrame;
    float frame = 0;
    const unsigned script = PlayerScript(gameBase, frame);
    const bool wasDescending = airCombo.descending;
    if (airCombo.Advance(script, frame, renderFrame)) {
        Log("AIR_STRIKE serial=%llu branch=%u script=%x frame=%.1f",
            static_cast<unsigned long long>(airCombo.serial), airCombo.branch, script, frame);
    }
    if (wasDescending != airCombo.descending)
        Log("AIR_DESCENT active=%u script=%x frame=%.1f", unsigned(airCombo.descending), script, frame);
    if (airCombo.trailCapture)
        Log("AIR_TRAIL serial=%llu frame=%.1f layer=%u", static_cast<unsigned long long>(airCombo.serial), frame, airCombo.samples);
    const bool stompScript = script >= 0x1b0f0e && script < 0x1b0fa4;
    if (!stompScript || (frame <= 2 && previousStompFrame >= 10)) stompLatched = false;
    if (!stompLatched && script >= 0x1b0f26 && script < 0x1b0f3e && frame >= 2 && frame < 10) {
        // The player contact cue has executed, and the rebound motion is still at its start.
        stompLatched = true;
        stompRenderFrame = renderFrame;
        ++stompSerial;
        Log("STOMP_CONTACT serial=%llu script=%x frame=%.1f", static_cast<unsigned long long>(stompSerial), script, frame);
    }
    previousStompFrame = stompScript ? frame : 0;
    const unsigned branch = settings.autoWindRun && std::isfinite(frame) ? WindRunBranch(script) : 0;
    if (branch && (branch != windRunBranch || (frame <= 6 && frame < windRunFrame))) {
        ++windRunSerial;
        Log("WINDRUN_START serial=%llu branch=%u script=%x frame=%.1f",
            static_cast<unsigned long long>(windRunSerial), branch, script, frame);
    } else if (branch && windRunFrame < WindRunApexFrame && frame >= WindRunApexFrame) {
        Log("WINDRUN_APEX serial=%llu branch=%u frame=%.1f",
            static_cast<unsigned long long>(windRunSerial), branch, frame);
    }
    windRunBranch = branch;
    windRunFrame = branch ? frame : 0;
    auto reading = ReadChargeFrame(script, frame);
    if ((reading.move == ChargeMove::ET && !settings.autoET) ||
        (reading.move == ChargeMove::UT && !settings.autoUT)) reading = {};
    const auto previous = chargeTimeline.current;
    const bool followThrough = ChargeFollowThrough(script);
    if (chargeTimeline.Advance(reading, followThrough, renderFrame)) {
        ++chargeSerial;
        Log("CHARGE_RELEASE level=%s family=%u script=%x motion=%.1f visual=%u",
            reading.move == ChargeMove::ET ? "ET" : "UT", unsigned(reading.family), script, frame,
            unsigned(reading.visual));
    }
    const auto current = chargeTimeline.current;
    wallDive.Advance(settings.autoWallDownslash ? script : 0, frame, renderFrame);
    swallow.Advance(settings.autoSwallow ? script : 0, frame, renderFrame);
    const auto feather = settings.autoUT && settings.autoWhiteFeather ? WhiteFeatherFrame(script, frame) : FeatherFrame{};
    const auto action = current.move == ChargeMove::ET ? Action::ET :
        current.move == ChargeMove::UT ? Action::UT :
        feather.frame ? Action::WhiteFeather :
        swallow.route || swallow.reveal ? Action::FlyingSwallow :
        wallDive.diving || wallDive.landing ? Action::WallDownslash :
        windRunBranch ? Action::WindRun : Action::None;
    const float previousBody = effectFrame.body;
    effectFrame = {};
    const unsigned age = chargeTimeline.Age();
    if (current.move != ChargeMove::None || (chargeTimeline.chainRender && action == Action::None)) {
        effectFrame.body = chargeTimeline.BodyOpacity(renderFrame, settings.revealFrames, settings.utRevealFrames);
        if (chargeTimeline.SamplesTrail()) {
            effectFrame.leading = effectFrame.body == 0;
            effectFrame.trailFrames = 12;
        }
    } else if (action == Action::FlyingSwallow) {
        effectFrame.body = swallow.BodyOpacity(renderFrame);
        effectFrame.leading = effectFrame.body == 0;
        effectFrame.useSwallowClock = true;
        effectFrame.distinctPose = swallow.sample;
        if (swallow.sample) effectFrame.trailFrames = SwallowTrailFrames;
    } else if (action == Action::WhiteFeather) {
        effectFrame.body = feather.BodyOpacity();
        effectFrame.leading = effectFrame.body == 0;
        if (feather.frame < feather.impact && feather.frame != previousFeatherFrame) {
            effectFrame.trailFrames = 12;
            effectFrame.distinctPose = true;
        }
        if (feather.frame == feather.impact && previousFeatherFrame != feather.impact)
            Log("WHITE_FEATHER_IMPACT script=%x frame=%.1f", script, frame);
    } else if (action == Action::WindRun) {
        // Both branches use the same clock. The roll cancel starts at frame 6.
        const auto phase = static_cast<unsigned>(std::clamp(windRunFrame, 0.0f, 60.0f));
        effectFrame.body = WindRunBodyAlpha(phase);
        effectFrame.leading = effectFrame.body == 0;
        if (phase >= WindRunStartFrame && phase <= WindRunLastTrailFrame) effectFrame.trailFrames = 9;
    } else if (action == Action::WallDownslash) {
        effectFrame.body = wallDive.BodyOpacity(renderFrame);
        effectFrame.leading = effectFrame.body == 0;
        if (wallDive.diving) effectFrame.trailFrames = 11;
        if (wallDive.start == renderFrame)
            Log("WALL_DIVE route=%x script=%x frame=%.1f", wallDive.route->begin, script, frame);
        if (wallDive.landing == renderFrame)
            Log("WALL_LANDING script=%x frame=%.1f airborne_frames=%llu", script, frame,
                static_cast<unsigned long long>(renderFrame - wallDive.start));
    }
    previousFeatherFrame = feather.frame;
    if (action != activeAction) {
        Log("ACTION from=%s to=%s script=%x frame=%.1f", ActionName(activeAction), ActionName(action), script, frame);
        activeAction = action;
    }
    if (effectFrame.body != previousBody &&
        (effectFrame.body == 0 || effectFrame.body == 1 || previousBody == 0 || previousBody == 1))
        Log("BODY_PHASE move=%s script=%x frame=%.1f alpha=%.3f leading=%u",
            ActionName(action), script, frame, effectFrame.body, unsigned(effectFrame.leading));
    if (chargeTimeline.SamplesTrail() && current.frame != previous.frame && age == ChargeTravelAge)
        Log("CHARGE_TRAVEL move=%s frame=%u age=%u", ActionName(action), current.frame, age);
}

template<class Fn>
Fn Original(Context* context, Slot slot) {
    void* address = (*reinterpret_cast<void***>(context))[slot];
    auto found = originals.find(address);
    return reinterpret_cast<Fn>(found == originals.end() ? address : found->second);
}

template<class T, size_t N>
void WriteRange(std::array<T, N>& destination, UINT first, UINT count, const T* values) {
    for (UINT i = 0; i < count && first + i < N; ++i)
        destination[first + i] = values ? values[i] : T{};
}

void STDMETHODCALLTYPE TrackVSConstants(Context* c, UINT first, UINT count, ID3D11Buffer* const* buffers) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    WriteRange(contexts[c].vsConstants, first, count, buffers);
    Original<SetBuffers>(c, VsConstants)(c, first, count, buffers);
}

void STDMETHODCALLTYPE TrackVertex(Context* c, UINT first, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    for (UINT i = 0; i < count; ++i) {
        if (first + i >= VertexSlots) {
            if (buffers && buffers[i]) state.unknownVertexSlots = true;
            continue;
        }
        auto& binding = state.vertices[first + i];
        ID3D11Buffer* next = buffers ? buffers[i] : nullptr;
        if (binding.buffer != next) {
            binding.description = {};
            if (next) next->GetDesc(&binding.description);
        }
        binding.buffer = next;
        binding.stride = strides ? strides[i] : 0;
        binding.offset = offsets ? offsets[i] : 0;
    }
    Original<SetVertex>(c, VertexBuffers)(c, first, count, buffers, strides, offsets);
}

void STDMETHODCALLTYPE TrackIndex(Context* c, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    if (state.index != buffer) {
        state.indexDesc = {};
        if (buffer) buffer->GetDesc(&state.indexDesc);
    }
    state.index = buffer;
    state.indexFormat = format;
    state.indexOffset = offset;
    Original<SetIndex>(c, IndexBuffer)(c, buffer, format, offset);
}

void STDMETHODCALLTYPE TrackLayout(Context* c, ID3D11InputLayout* layout) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].layout = layout;
    Original<SetLayout>(c, InputLayout)(c, layout);
}

void STDMETHODCALLTYPE TrackTopology(Context* c, D3D11_PRIMITIVE_TOPOLOGY topology) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].topology = topology;
    Original<SetTopology>(c, Topology)(c, topology);
}

void STDMETHODCALLTYPE TrackPS(Context* c, ID3D11PixelShader* shader, ID3D11ClassInstance* const* classes, UINT count) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].ps = shader;
    contexts[c].psClasses = count;
    Original<SetPS>(c, PsShader)(c, shader, classes, count);
}

void STDMETHODCALLTYPE TrackVS(Context* c, ID3D11VertexShader* shader, ID3D11ClassInstance* const* classes, UINT count) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].vs = shader;
    Original<SetVS>(c, VsShader)(c, shader, classes, count);
}

void RememberTargets(ContextState& state, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth) {
    state.targets.fill(nullptr);
    state.targetCount = std::min<UINT>(count, state.targets.size());
    WriteRange(state.targets, 0, state.targetCount, views);
    state.depthView = depth;
}

void STDMETHODCALLTYPE TrackTargets(Context* c, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    RememberTargets(state, count, views, depth);
    state.uavsBound = false;
    Original<SetTargets>(c, Targets)(c, count, views, depth);
}

void STDMETHODCALLTYPE TrackTargetsUav(Context* c, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth, UINT start, UINT uavCount, ID3D11UnorderedAccessView* const* uavs, const UINT* initialCounts) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    if (count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) RememberTargets(state, count, views, depth);
    if (uavCount != D3D11_KEEP_UNORDERED_ACCESS_VIEWS) state.uavsBound = uavCount != 0;
    Original<SetTargetsUav>(c, TargetsAndUavs)(c, count, views, depth, start, uavCount, uavs, initialCounts);
}

void STDMETHODCALLTYPE TrackBlend(Context* c, ID3D11BlendState* blend, const FLOAT* factors, UINT mask) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    state.blendState = blend;
    state.sampleMask = mask;
    state.blendFactor = {1, 1, 1, 1};
    if (factors) std::copy_n(factors, 4, state.blendFactor.begin());
    Original<SetBlend>(c, Blend)(c, blend, factors, mask);
}

void STDMETHODCALLTYPE TrackDepth(Context* c, ID3D11DepthStencilState* depth, UINT stencil) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].depthState = depth;
    contexts[c].stencil = stencil;
    Original<SetDepth>(c, Depth)(c, depth, stencil);
}

void STDMETHODCALLTYPE TrackViewport(Context* c, UINT count, const D3D11_VIEWPORT* viewport) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].viewportCount = count;
    if (count && viewport) contexts[c].viewport = viewport[0];
    Original<SetViewport>(c, Viewports)(c, count, viewport);
}

void CopySceneDepth(Context* c, const SceneDepth& depth) {
    c->CopySubresourceRegion(depth.snapshot.Get(), 0, 0, 0, 0, depth.source.Get(), depth.subresource, nullptr);
}

void STDMETHODCALLTYPE ObserveClearDepth(Context* c, ID3D11DepthStencilView* view, UINT flags, FLOAT value, UINT8 stencil) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    if (!drawingExtra && settings.enabled && (flags & D3D11_CLEAR_DEPTH)) {
        ComPtr<ID3D11Device> device;
        c->GetDevice(device.GetAddressOf());
        const auto found = resources.find(device.Get());
        if (found != resources.end()) {
            auto& depth = found->second.sceneDepth;
            if (depth.frame == renderFrame && !depth.frozen && view == depth.sourceView.Get() && depth.snapshot) {
                // Preserve geometry depth before a later UI/postprocess pass clears it.
                CopySceneDepth(c, depth);
                depth.frozen = true;
            }
        }
    }
    Original<ClearDepth>(c, ClearDepthView)(c, view, flags, value, stencil);
}

void STDMETHODCALLTYPE TrackClear(Context* c) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    auto readbacks = std::move(state.readbacks);
    state = ContextState{};
    state.readbacks = std::move(readbacks);
    Original<ClearContext>(c, Clear)(c);
}

bool CreateShader(ID3D11Device* device, const char* hlsl, ComPtr<ID3D11PixelShader>& shader) {
    ComPtr<ID3DBlob> code, error;
    HRESULT result = D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), error.GetAddressOf());
    if (FAILED(result)) {
        Log("SHADER_COMPILE_FAILED hr=%08lx", result);
        return false;
    }
    return SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf()));
}

D3D11_TEXTURE2D_DESC MaskDescription() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = MaskWidth;
    desc.Height = MaskHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    return desc;
}

bool PrepareMask(ID3D11Device* device, DeviceResources& res) {
    if (res.maskTarget) return true;
    if (!res.maskShader && !CreateShader(device, "float4 main():SV_Target{return 1;}", res.maskShader)) return false;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (!res.noDepth && FAILED(device->CreateDepthStencilState(&depth, res.noDepth.GetAddressOf()))) return false;
    auto desc = MaskDescription();
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (!res.mask && FAILED(device->CreateTexture2D(&desc, nullptr, res.mask.GetAddressOf()))) return false;
    return SUCCEEDED(device->CreateRenderTargetView(res.mask.Get(), nullptr, res.maskTarget.GetAddressOf()));
}

// Grip of Murder albedo 0 packs bare skin and the black glove into one atlas.
// The right arm's base skin and red curse pattern use albedos 1 and 25.
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

bool PrepareGhost(ID3D11Device* device, DeviceResources& res) {
    const std::string shader = std::string(SceneOcclusion) + R"(
        cbuffer GhostLighting : register(b12) { float anchorOpacity; float brightness; }
        cbuffer GhostColor : register(b13) { float4 color; }
        float4 main(float4 pos : SV_Position) : SV_Target {
            occlude(pos);
            float opacity = anchorOpacity > 0 ? anchorOpacity : color.a;
            return float4(color.rgb * (opacity * brightness), opacity);
        })";
    if (!res.ghostShader && !CreateShader(device, shader.c_str(), res.ghostShader)) return false;
    if (!res.ghostLighting) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&desc, nullptr, res.ghostLighting.GetAddressOf()))) return false;
    }
    if (!res.sceneDepthConstants) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&desc, nullptr, res.sceneDepthConstants.GetAddressOf()))) return false;
    }
    if (!res.readDepth) {
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(device->CreateDepthStencilState(&depth, res.readDepth.GetAddressOf()))) return false;
    }
    if (!res.ghostBlend) {
        D3D11_BLEND_DESC blend{};
        auto& color = blend.RenderTarget[0];
        color.BlendEnable = TRUE;
        color.SrcBlend = D3D11_BLEND_ONE;
        color.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        color.BlendOp = D3D11_BLEND_OP_ADD;
        color.SrcBlendAlpha = D3D11_BLEND_ONE;
        color.DestBlendAlpha = D3D11_BLEND_ZERO;
        color.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        color.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        if (FAILED(device->CreateBlendState(&blend, res.ghostBlend.GetAddressOf()))) return false;
        color.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
        color.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
        if (FAILED(device->CreateBlendState(&blend, res.fadeBlend.GetAddressOf()))) return false;
    }
    if (!res.fadeBlend) return false;
    if (!res.fadeUnderlay) {
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
        if (!CreateShader(device, gripUnderlay.c_str(), res.gripFadeUnderlay) ||
            !CreateShader(device, underlay.c_str(), res.fadeUnderlay)) return false;
    }
    if (!res.fadeUnderlayBlend) {
        D3D11_BLEND_DESC blend{};
        auto& color = blend.RenderTarget[0];
        color.BlendEnable = TRUE;
        color.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
        color.DestBlend = D3D11_BLEND_ONE;
        color.BlendOp = D3D11_BLEND_OP_ADD;
        color.SrcBlendAlpha = D3D11_BLEND_ONE;
        color.DestBlendAlpha = D3D11_BLEND_ZERO;
        color.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        color.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        if (FAILED(device->CreateBlendState(&blend, res.fadeUnderlayBlend.GetAddressOf()))) return false;
    }
    const uint32_t colorKey = settings.alpha | (settings.tint[0] << 8) |
        (settings.tint[1] << 16) | (settings.tint[2] << 24);
    if (res.colorKey != colorKey) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        for (unsigned i = 0; i < FadeSteps; ++i) {
            const float opacity = (settings.alpha / 100.0f) * (i + 1) / FadeSteps;
            const float tint[4]{settings.tint[0] / 255.0f, settings.tint[1] / 255.0f,
                settings.tint[2] / 255.0f, opacity};
            D3D11_SUBRESOURCE_DATA initial{tint, 0, 0};
            if (FAILED(device->CreateBuffer(&desc, &initial, res.colors[i].ReleaseAndGetAddressOf()))) return false;
        }
        res.colorKey = colorKey;
    }
    return true;
}

uint64_t MeshKey(const ContextState& state, const Draw& draw) {
    uint64_t hash = 14695981039346656037ull;
    auto add = [&](UINT n) { hash = (hash ^ n) * 1099511628211ull; };
    add(draw.count);
    add(draw.first);
    add(static_cast<UINT>(draw.base));
    add(state.indexDesc.ByteWidth);
    add(state.indexFormat);
    add(state.indexOffset);
    for (const auto& vertex : state.vertices) {
        add(vertex.description.ByteWidth);
        add(vertex.stride);
        add(vertex.offset);
    }
    return hash;
}

void CallDraw(Context* c, const Draw& draw) {
    if (draw.instanced) Original<DrawIndexedInstanced>(c, IndexedInstanced)(c, draw.count, 1, draw.first, draw.base, draw.instance);
    else Original<DrawIndexed>(c, Indexed)(c, draw.count, draw.first, draw.base);
}

void SaveReadback(Context* c, const Readback& readback) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(c->Map(readback.texture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
    wchar_t file[96];
    swprintf_s(file, L"\\capture-%04u.rgba", readback.id);
    FILE* output = _wfopen((directory + file).c_str(), L"wb");
    if (output) {
        for (UINT row = 0; row < MaskHeight; ++row)
            fwrite(static_cast<const BYTE*>(mapped.pData) + row * mapped.RowPitch, 1, MaskWidth * 4, output);
        fclose(output);
    }
    c->Unmap(readback.texture.Get(), 0);
}

void CaptureMask(Context* c, ContextState& state, ID3D11Device* device, DeviceResources& res, const Draw& draw, uint64_t key) {
    if (captureKeys.size() >= CaptureLimit || !captureKeys.insert(key).second || !PrepareMask(device, res)) return;
    Readback readback{++captureNumber, {}};
    auto desc = MaskDescription();
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, readback.texture.GetAddressOf()))) return;
    const FLOAT zero[4]{};
    c->ClearRenderTargetView(res.maskTarget.Get(), zero);
    auto target = res.maskTarget.Get();
    D3D11_VIEWPORT viewport{0, 0, float(MaskWidth), float(MaskHeight), state.viewport.MinDepth, state.viewport.MaxDepth};
    Original<SetTargets>(c, Targets)(c, 1, &target, nullptr);
    Original<SetViewport>(c, Viewports)(c, 1, &viewport);
    Original<SetBlend>(c, Blend)(c, nullptr, nullptr, UINT_MAX);
    Original<SetDepth>(c, Depth)(c, res.noDepth.Get(), 0);
    Original<SetPS>(c, PsShader)(c, res.maskShader.Get(), nullptr, 0);
    CallDraw(c, draw);
    Original<SetPS>(c, PsShader)(c, state.ps, nullptr, 0);
    Original<SetDepth>(c, Depth)(c, state.depthState, state.stencil);
    Original<SetBlend>(c, Blend)(c, state.blendState, state.blendFactor.data(), state.sampleMask);
    Original<SetViewport>(c, Viewports)(c, 1, &state.viewport);
    Original<SetTargets>(c, Targets)(c, state.targetCount, state.targets.data(), state.depthView);
    c->CopyResource(readback.texture.Get(), res.mask.Get());
    Log("MASK id=%u key=%016llx indices=%u first=%u base=%d vb=%u stride=%u ib=%u vp=%.0fx%.0f vs=%p ps=%p rt=%p", readback.id,
        static_cast<unsigned long long>(key), draw.count, draw.first, draw.base, state.vertices[0].description.ByteWidth,
        state.vertices[0].stride, state.indexDesc.ByteWidth, state.viewport.Width, state.viewport.Height, state.vs, state.ps, state.targets[0]);
    if (c->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) SaveReadback(c, readback);
    else state.readbacks.push_back(std::move(readback));
}

bool CopyBuffer(Context* c, ID3D11Device* device, ID3D11Buffer* buffer, ComPtr<ID3D11Buffer>& result) {
    if (!buffer) {
        result.Reset();
        return true;
    }
    D3D11_BUFFER_DESC desc{};
    buffer->GetDesc(&desc);
    if (desc.Usage == D3D11_USAGE_IMMUTABLE) {
        result = buffer;
        return true;
    }
    if (desc.ByteWidth > 2 * 1024 * 1024 || desc.MiscFlags != 0) return false;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    if (result) {
        D3D11_BUFFER_DESC previous{};
        result->GetDesc(&previous);
        if (result.Get() == buffer || previous.Usage != desc.Usage || previous.ByteWidth != desc.ByteWidth ||
            previous.BindFlags != desc.BindFlags || previous.MiscFlags != desc.MiscFlags) result.Reset();
    }
    if (!result && FAILED(device->CreateBuffer(&desc, nullptr, result.GetAddressOf()))) return false;
    c->CopyResource(result.Get(), buffer);
    return true;
}

bool SavePose(Context* c, const ContextState& state, ID3D11Device* device, Snapshot& pose, uint64_t nowUs) {
    pose.timeUs = 0;
    for (UINT i = 0; i < ConstantSlots; ++i) {
        // VUniform1 at cb2 contains the live view/projection. cb3 holds the pose.
        if (i == ViewProjectionSlot) continue;
        if (!CopyBuffer(c, device, state.vsConstants[i], pose.constants[i])) return false;
    }
    for (UINT i = 0; i < VertexSlots; ++i) {
        const auto& binding = state.vertices[i];
        if (binding.description.Usage == D3D11_USAGE_DYNAMIC) {
            if (!CopyBuffer(c, device, binding.buffer, pose.vertices[i])) return false;
        } else pose.vertices[i] = binding.buffer;
    }
    pose.timeUs = nowUs;
    pose.renderFrame = renderFrame;
    return true;
}

constexpr uint64_t EyeMesh = 0x8f9a9ac38c445b38ull;
constexpr uint64_t StompHoldFrames = 10;
constexpr uint64_t StompFadeFrames = 18;

float StompAlpha(uint64_t ageFrames) {
    if (ageFrames < StompHoldFrames) return 1;
    if (ageFrames >= StompHoldFrames + StompFadeFrames - 1) return 0;
    return float(StompHoldFrames + StompFadeFrames - ageFrames - 1) / float(StompFadeFrames);
}

constexpr std::array<uint64_t, 7> DragonGearMeshes{
    0xcdcee5d368c2225cull, 0x46f1b6e0e3ae7210ull, 0x839c67d5888b97e6ull,
    0x1caaf75ab2d117e8ull, 0xcdf7d0b016078448ull, 0xa31e7a41a8018c68ull, 0x399c3337538d8504ull
};

void CapturePart(Context* c, const ContextState& state, ID3D11Device* device, PoseFrame& frame,
    const Draw& draw, uint64_t key) {
    if (!state.layout || !state.vsConstants[ViewProjectionSlot] ||
        state.topology == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED || frame.count >= 192) return;
    for (unsigned i = 0; i < frame.count; ++i) {
        const auto& part = frame.parts[i];
        if (part.meshKey == key && part.index.Get() == state.index && part.sourceVertex == state.vertices[0].buffer)
            return;
    }
    if (frame.count == frame.parts.size()) frame.parts.emplace_back();
    auto& part = frame.parts[frame.count];
    if (!SavePose(c, state, device, part.pose, frame.timeUs)) return;
    part.meshKey = key;
    part.bodyPart = ClassifyBodyPart(key);
    part.material = ClassifyGhostMaterial(key);
    if (activeCostume != Costume::Black) {
        if (const auto* mesh = FindCostumeMesh(activeCostume, key)) {
            part.bodyPart = mesh->part;
            part.material = mesh->material;
        }
    }
    part.sourceVertex = state.vertices[0].buffer;
    part.draw = draw;
    part.shader = state.vs;
    part.layout = state.layout;
    part.index = state.index;
    part.topology = state.topology;
    part.indexFormat = state.indexFormat;
    part.indexOffset = state.indexOffset;
    for (UINT i = 0; i < VertexSlots; ++i) {
        part.strides[i] = state.vertices[i].stride;
        part.offsets[i] = state.vertices[i].offset;
    }
    ++frame.count;
}

void CapturePlayer(Context* c, const ContextState& state, ID3D11Device* device, DeviceResources& res,
    const Draw& draw, uint64_t key, uint64_t nowUs, bool recording, bool stomp) {
    const bool charge = chargeTimeline.current.move != ChargeMove::None;
    if (res.cameraFrame != renderFrame && state.vsConstants[ViewProjectionSlot] &&
        CopyBuffer(c, device, state.vsConstants[ViewProjectionSlot], res.frameCamera)) {
        res.cameraFrame = renderFrame;
    }
    const bool releasePoseAlive = res.chargeOrigin.count && renderFrame - res.chargeOrigin.frame < AnchorEndAge;
    if (recording || (charge && chargeTimeline.current.visual != ChargeVisual::Off) ||
        releasePoseAlive || effectFrame.leading || stomp || airCombo.armed)
        res.playerFrontUntilUs = nowUs + 1000ull * std::max(650u, settings.lifetime);
    if (effectFrame.body > 0 && nowUs < res.playerFrontUntilUs) {
        if (res.playerFront.frame != renderFrame) res.playerFront.Begin(renderFrame, nowUs);
        CapturePart(c, state, device, res.playerFront, draw, key);
    }
    if (charge && res.chargeSerial != chargeSerial) {
        res.chargeSerial = chargeSerial;
        if (chargeTimeline.CapturesOrigin()) res.chargeOrigin.Begin(renderFrame, nowUs);
        else res.chargeOrigin.Begin(0, 0);
        for (auto& frame : res.trail) frame.Begin(0, 0);
    }
    if (chargeTimeline.CapturesOrigin() && res.chargeOrigin.frame == renderFrame)
        CapturePart(c, state, device, res.chargeOrigin, draw, key);
    if (stomp) {
        auto& origin = res.stompOrigins[stompSerial % res.stompOrigins.size()];
        if (origin.frame != renderFrame) origin.Begin(renderFrame, nowUs);
        CapturePart(c, state, device, origin, draw, key);
    }
    if (airCombo.branch && airCombo.firstRenderFrame == renderFrame) {
        auto& origin = res.airOrigins[airCombo.serial % res.airOrigins.size()];
        if (origin.frame != renderFrame) origin.Begin(renderFrame, nowUs);
        CapturePart(c, state, device, origin, draw, key);
    }
    if (airCombo.trailCapture) {
        auto& pose = res.airTrail[airCombo.trailSerial % res.airTrail.size()];
        if (pose.frame != renderFrame) pose.Begin(renderFrame, nowUs);
        CapturePart(c, state, device, pose, draw, key);
    }
    auto* trail = &res.trail[(res.nextTrail + PoseHistorySize - 1) % PoseHistorySize];
    if (recording && trail->frame != renderFrame) {
        const bool newWindRun = activeAction == Action::WindRun && res.windSerial != windRunSerial;
        const auto gap = nowUs - trail->timeUs;
        if (newWindRun || effectFrame.distinctPose || airCombo.descentCapture || !trail->timeUs ||
            gap >= 1000ull * settings.interval) {
            if (trail->timeUs && gap < 100000) {
                intervalTotalUs += gap;
                ++intervalCount;
            }
            trail = &res.trail[res.nextTrail];
            res.nextTrail = (res.nextTrail + 1) % PoseHistorySize;
            trail->Begin(renderFrame, nowUs);
            trail->fadeFrames = airCombo.descentCapture ? 0 : effectFrame.trailFrames;
            if (effectFrame.useSwallowClock) trail->swallowClock = renderFrame - swallow.pauseFrames;
            res.windSerial = windRunSerial;
            ++samples;
        }
    }
    if (trail->frame == renderFrame) {
        CapturePart(c, state, device, *trail, draw, key);
    } else if (effectFrame.leading) {
        if (res.leadingPose.frame != renderFrame) res.leadingPose.Begin(renderFrame, nowUs);
        CapturePart(c, state, device, res.leadingPose, draw, key);
    }
}

void QueueFrame(DeviceResources& res, const PoseFrame& pose, ID3D11Buffer* color, float opacity, bool anchor = false) {
    for (unsigned i = 0; i < pose.count; ++i) {
        auto& packet = res.overlayDraws.emplace_back(pose.parts[i]);
        packet.pose.constants[ViewProjectionSlot] = res.frameCamera;
        packet.color = color;
        packet.anchorOpacity = anchor ? opacity : 0;
        packet.opacity = opacity;
    }
}

void BuildOverlayDraws(DeviceResources& res, uint64_t nowUs) {
    if (res.cameraFrame != renderFrame || !res.colors.back()) return;
    const float opacity = settings.alpha / 100.0f;
    const float chargeAlpha = res.chargeOrigin.count ? AnchorAlpha(renderFrame - res.chargeOrigin.frame) : 0;
    if (chargeAlpha > 0 && res.chargeOrigin.count)
        QueueFrame(res, res.chargeOrigin, res.colors.back().Get(), chargeAlpha * opacity, true);
    for (const auto& stomp : res.stompOrigins) {
        if (!stomp.count || stomp.frame > renderFrame) continue;
        const auto age = renderFrame - stomp.frame;
        const float alpha = StompAlpha(age);
        if (loggedStompPhases < 12 && (age == 0 || age == StompHoldFrames - 1 ||
            age == StompHoldFrames || age == StompHoldFrames + StompFadeFrames - 1)) {
            ++loggedStompPhases;
            Log("STOMP_ALPHA age_frames=%llu alpha=%.3f", static_cast<unsigned long long>(age), alpha);
        }
        if (alpha <= 0) continue;
        QueueFrame(res, stomp, res.colors.back().Get(), alpha * opacity, true);
        if (stomp.frame == renderFrame) Log("STOMP_POSE parts=%u", stomp.count);
    }
    for (const auto& origin : res.airOrigins) {
        if (!origin.count) continue;
        const float alpha = AirOriginFade(renderFrame - origin.frame);
        if (alpha > 0) QueueFrame(res, origin, res.colors.back().Get(), opacity * alpha, true);
    }
    for (const auto& pose : res.airTrail) {
        if (!pose.count || pose.frame == renderFrame) continue;
        const float alpha = AirFade(renderFrame - pose.frame);
        if (alpha > 0) QueueFrame(res, pose, res.colors.back().Get(), opacity * alpha);
    }
    if (airCombo.branch) return;

    const uint64_t lifetimeUs = 1000ull * settings.lifetime;
    std::array<const PoseFrame*, PoseHistorySize> visible{};
    std::array<float, PoseHistorySize> strengths{};
    const auto& latest = res.trail[(res.nextTrail + PoseHistorySize - 1) % PoseHistorySize];
    const auto& leading = latest.frame == renderFrame ? latest : res.leadingPose;
    const bool drawLeading = effectFrame.leading && leading.frame == renderFrame && leading.count;
    unsigned count = 0;
    for (unsigned i = 0; i < PoseHistorySize; ++i) {
        const auto& pose = res.trail[(res.nextTrail + i) % PoseHistorySize];
        if (!pose.count || pose.frame >= renderFrame || pose.timeUs > nowUs) continue;
        const uint64_t age = pose.swallowClock ? renderFrame - swallow.pauseFrames - pose.swallowClock :
            pose.fadeFrames ? renderFrame - pose.frame : nowUs - pose.timeUs;
        const uint64_t duration = pose.fadeFrames ? pose.fadeFrames : lifetimeUs;
        if (age < duration) {
            strengths[count] = float(duration - age) / float(duration);
            visible[count++] = &pose;
        }
    }
    const unsigned drawn = std::min(settings.copies - unsigned(drawLeading), count);
    for (unsigned i = 0; i < drawn; ++i) {
        const unsigned index = drawn == 1 ? count - 1 : i * (count - 1) / (drawn - 1);
        const float strength = strengths[index];
        const auto step = std::min<unsigned>(FadeSteps - 1, static_cast<unsigned>(strength * FadeSteps));
        QueueFrame(res, *visible[index], res.colors[step].Get(), opacity * strength);
    }
    if (drawLeading) QueueFrame(res, leading, res.colors.back().Get(), opacity);
    if (drawn + unsigned(drawLeading) == settings.copies && loggedPoses < 10) {
        ++loggedPoses;
        const auto& newest = drawLeading ? leading : *visible[count - 1];
        const auto eyes = std::count_if(newest.parts.begin(), newest.parts.begin() + newest.count,
            [](const OverlayDraw& part) { return part.meshKey == EyeMesh; });
        Log("TRAIL_POSES count=%u parts=%u eyes=%u", drawn + unsigned(drawLeading), newest.count, static_cast<unsigned>(eyes));
    }
}

void ResetPoses() {
    for (auto& [device, res] : resources) {
        for (auto& pose : res.trail) pose.Begin(0, 0);
        for (auto& pose : res.stompOrigins) pose.Begin(0, 0);
        for (auto& pose : res.airOrigins) pose.Begin(0, 0);
        for (auto& pose : res.airTrail) pose.Begin(0, 0);
        res.chargeOrigin.Begin(0, 0);
        res.leadingPose.Begin(0, 0);
        res.playerFront.Begin(0, 0);
        res.playerFrontUntilUs = 0;
        res.overlayDraws.clear();
    }
}

void ApplyAppearance() {
    const auto index = static_cast<unsigned>(activeCostume);
    const auto& appearance = appearances[index < appearances.size() ? index : 0];
    settings.alpha = appearance.alpha;
    settings.glowStrength = appearance.glow;
    settings.tint = appearance.tint;
    settings.muscleHighlight = appearance.muscleHighlight;
    settings.brightness = appearance.brightness;
}

const CostumeMesh* ObserveCostume(uint64_t key) {
    if (const auto* part = FindCostumeMesh(activeCostume, key)) {
        if (part->torsoAnchor) costumeLastFrame = renderFrame;
        return part;
    }
    // Keep the current model while it is being drawn. Shared eyes and gloves
    // cannot select a preset on their own.
    if (activeCostume != Costume::Unknown && renderFrame - costumeLastFrame <= 2) return nullptr;
    for (unsigned i = 0; i < std::size(Costumes); ++i) {
        const auto candidate = static_cast<Costume>(i);
        const auto* part = FindCostumeMesh(candidate, key);
        if (!part || !part->torsoAnchor) continue;
        ResetPoses();
        activeCostume = candidate;
        costumeLastFrame = renderFrame;
        ApplyAppearance();
        Log("COSTUME id=%u alpha=%u glow=%u rgb=%u,%u,%u", i, settings.alpha, settings.glowStrength,
            settings.tint[0], settings.tint[1], settings.tint[2]);
        return part;
    }
    return nullptr;
}

void RememberSceneDepth(ID3D11Device* device, SceneDepth& depth, ID3D11DepthStencilView* view,
    ID3D11DepthStencilState* state, const D3D11_VIEWPORT& viewport) {
    if (!view || depth.frame == renderFrame) return;
    D3D11_DEPTH_STENCIL_DESC mode{};
    if (state) {
        state->GetDesc(&mode);
        if (!mode.DepthEnable || (mode.DepthFunc != D3D11_COMPARISON_LESS &&
            mode.DepthFunc != D3D11_COMPARISON_LESS_EQUAL && mode.DepthFunc != D3D11_COMPARISON_EQUAL &&
            mode.DepthFunc != D3D11_COMPARISON_GREATER && mode.DepthFunc != D3D11_COMPARISON_GREATER_EQUAL)) return;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC binding{};
    view->GetDesc(&binding);
    if (binding.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D) return;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    view->GetResource(resource.GetAddressOf());
    if (FAILED(resource.As(&texture))) return;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const UINT mip = binding.Texture2D.MipSlice;
    desc.Width = std::max(1u, desc.Width >> mip);
    desc.Height = std::max(1u, desc.Height >> mip);
    if (desc.SampleDesc.Count != 1 || viewport.Width != desc.Width || viewport.Height != desc.Height ||
        viewport.TopLeftX != 0 || viewport.TopLeftY != 0) return;
    DXGI_FORMAT readFormat;
    switch (binding.Format) {
    case DXGI_FORMAT_D16_UNORM:
        desc.Format = DXGI_FORMAT_R16_TYPELESS; readFormat = DXGI_FORMAT_R16_UNORM; break;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        desc.Format = DXGI_FORMAT_R24G8_TYPELESS; readFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
    case DXGI_FORMAT_D32_FLOAT:
        desc.Format = DXGI_FORMAT_R32_TYPELESS; readFormat = DXGI_FORMAT_R32_FLOAT; break;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        desc.Format = DXGI_FORMAT_R32G8X24_TYPELESS; readFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
    default: return;
    }
    if (!depth.snapshot || desc.Width != depth.desc.Width || desc.Height != depth.desc.Height ||
        desc.Format != depth.desc.Format) {
        desc.MipLevels = desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = desc.MiscFlags = 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = readFormat;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        ComPtr<ID3D11Texture2D> snapshot;
        ComPtr<ID3D11ShaderResourceView> reader;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, snapshot.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(snapshot.Get(), &srv, reader.GetAddressOf()))) return;
        depth.snapshot = std::move(snapshot);
        depth.view = std::move(reader);
        depth.desc = desc;
    }
    depth.source = std::move(texture);
    depth.sourceView = view;
    depth.viewport = viewport;
    depth.subresource = mip;
    depth.reversed = mode.DepthFunc == D3D11_COMPARISON_GREATER || mode.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL;
    depth.frozen = false;
    depth.frame = renderFrame;
}

bool InspectDraw(Context* c, const Draw& draw) {
    if (!hooksReady || drawingExtra) return false;
    const auto now = GetTickCount64();
    const bool capture = now < captureUntil;
    if (!capture && !settings.enabled) return false;
    const auto found = contexts.find(c);
    if (found == contexts.end()) return false;
    auto& state = found->second;
    if (!state.index || !state.vertices[0].buffer || !state.vs || state.uavsBound || state.unknownVertexSlots ||
        draw.count < 3 || draw.count > 100000) return false;
    const auto key = MeshKey(state, draw);
    const auto* mesh = ObserveCostume(key);
    const bool selected = mesh != nullptr;
    const bool gear = activeCostume != Costume::Unknown &&
        std::find(DragonGearMeshes.begin(), DragonGearMeshes.end(), key) != DragonGearMeshes.end();
    if (!capture && !selected && !gear) return false;
    const auto nowUs = PoseClockUs();
    const bool active = settings.enabled;
    if (active && (selected || gear)) UpdateAction();
    const float body = active && (selected || gear) ? effectFrame.body : 1;
    const bool color = state.ps && state.targets[0];
    if (!color) return state.depthView && body < 1;
    if (key == EyeMesh && body < 1 && loggedEyes < 8) {
        ++loggedEyes;
        Log("EYE_OPACITY move=%s alpha=%.3f index=%p", ActionName(activeAction), body, state.index);
    }
    const bool sceneColor = state.viewportCount == 1 && state.viewport.Width >= 640 &&
        state.viewport.Height >= 360 && !state.psClasses;
    if ((!active || (!selected && !gear) || !sceneColor) && !capture && body >= 1) return false;

    ComPtr<ID3D11Device> device;
    c->GetDevice(device.GetAddressOf());
    if (!device) return false;
    auto& res = resources[device.Get()];
    struct ExtraDrawScope {
        ExtraDrawScope() { drawingExtra = true; }
        ~ExtraDrawScope() { drawingExtra = false; }
    } extra;
    if (capture && sceneColor) CaptureMask(c, state, device.Get(), res, draw, key);
    if (!PrepareGhost(device.Get(), res)) return false;
    if (active && selected && sceneColor) {
        RememberSceneDepth(device.Get(), res.sceneDepth, state.depthView, state.depthState, state.viewport);
        const bool recording = now < previewUntil || effectFrame.trailFrames || airCombo.descentCapture;
        CapturePlayer(c, state, device.Get(), res, draw, key, nowUs, recording, stompRenderFrame == renderFrame);
    } else if (active && gear && sceneColor && body > 0 && res.playerFront.frame == renderFrame) {
        CapturePart(c, state, device.Get(), res.playerFront, draw, key);
    }
    if (body <= 0) return true;
    if (body >= 1) return false;

    // Every selected material uses the same opacity, including both eye draws.
    const FLOAT factors[]{body, body, body, body};
    auto setBlend = Original<SetBlend>(c, Blend);
    auto setDepth = Original<SetDepth>(c, Depth);
    if (state.depthView) setDepth(c, res.readDepth.Get(), 0);
    if (selected && sceneColor && state.depthView) {
        // The blue material is drawn under the dissolving body. The later
        // screen-space trails still yield to its foreground mask.
        const auto surface = activeCostume == Costume::Black ? ClassifyGhostMaterial(key) : mesh->material;
        const bool gripAtlas = surface == GhostMaterial::GripAtlas;
        const float skinBoost = 0.46f * settings.muscleHighlight / 100.0f;
        const float glow = settings.glowStrength / 100.0f;
        const float lighting = settings.glassVisual ? (settings.brightness / 100.0f + glow) * 0.48f : 1.0f + glow;
        const float intensity = settings.alpha * lighting * (1 - body) / 25500.0f;
        auto setPS = Original<SetPS>(c, PsShader);
        auto drawUnderlay = [&](ID3D11PixelShader* shader, float level) {
            const FLOAT blue[]{settings.tint[0] * intensity * level * 0.70f,
                settings.tint[1] * intensity * level * 0.83f, settings.tint[2] * intensity * level, 0};
            setPS(c, shader, nullptr, 0);
            setBlend(c, res.fadeUnderlayBlend.Get(), blue, state.sampleMask);
            CallDraw(c, draw);
        };
        drawUnderlay(res.fadeUnderlay.Get(), 0.54f + (surface == GhostMaterial::Skin ? skinBoost : 0));
        if (gripAtlas && skinBoost > 0) drawUnderlay(res.gripFadeUnderlay.Get(), skinBoost);
        setPS(c, state.ps, nullptr, 0);
    }
    setBlend(c, res.fadeBlend.Get(), factors, state.sampleMask);
    CallDraw(c, draw);
    if (state.depthView) setDepth(c, state.depthState, state.stencil);
    setBlend(c, state.blendState, state.blendFactor.data(), state.sampleMask);
    return true;
}

void STDMETHODCALLTYPE ObserveIndexed(Context* c, UINT count, UINT first, INT base) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    try { if (InspectDraw(c, Draw{count, first, base})) return; }
    catch (...) { Log("DRAW_INSPECTION_FAILED"); }
    Original<DrawIndexed>(c, Indexed)(c, count, first, base);
}

void STDMETHODCALLTYPE ObserveIndexedInstanced(Context* c, UINT count, UINT instances, UINT first, INT base, UINT instance) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    if (instances == 1) {
        try { if (InspectDraw(c, Draw{count, first, base, instance, true})) return; }
        catch (...) { Log("DRAW_INSPECTION_FAILED"); }
    }
    Original<DrawIndexedInstanced>(c, IndexedInstanced)(c, count, instances, first, base, instance);
}

HRESULT STDMETHODCALLTYPE ObserveFinish(Context* c, BOOL restore, ID3D11CommandList** list) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    HRESULT result = Original<FinishList>(c, Finish)(c, restore, list);
    auto found = contexts.find(c);
    if (found != contexts.end() && SUCCEEDED(result) && list && *list && !found->second.readbacks.empty()) {
        commandReadbacks[*list] = std::move(found->second.readbacks);
        found->second.readbacks.clear();
    }
    return result;
}

void STDMETHODCALLTYPE ObserveExecute(Context* c, ID3D11CommandList* list, BOOL restore) {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    Original<ExecuteList>(c, Execute)(c, list, restore);
    auto found = commandReadbacks.find(list);
    if (found != commandReadbacks.end() && c->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        for (const auto& readback : found->second) SaveReadback(c, readback);
        commandReadbacks.erase(found);
    }
}

bool PrepareOverlay(ID3D11Device* device, DeviceResources& res, const D3D11_TEXTURE2D_DESC& backBuffer) {
    if (!res.overlayContext && FAILED(device->CreateDeferredContext(0, res.overlayContext.GetAddressOf()))) return false;
    if (!res.overlayRasterizer) {
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizer, res.overlayRasterizer.GetAddressOf()))) return false;
    }
    if (!res.overlayStencil) {
        D3D11_DEPTH_STENCIL_DESC coverage{};
        coverage.DepthFunc = D3D11_COMPARISON_ALWAYS;
        coverage.StencilEnable = TRUE;
        coverage.StencilReadMask = coverage.StencilWriteMask = 0xff;
        coverage.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR_SAT, D3D11_COMPARISON_EQUAL};
        coverage.BackFace = coverage.FrontFace;
        if (FAILED(device->CreateDepthStencilState(&coverage, res.overlayStencil.GetAddressOf()))) return false;
    }
    auto desc = MaskDescription();
    desc.Width = backBuffer.Width;
    desc.Height = backBuffer.Height;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc = backBuffer.SampleDesc;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    const auto& old = res.coverageDesc;
    if (res.overlayCoverage && desc.Width == old.Width && desc.Height == old.Height &&
        desc.SampleDesc.Count == old.SampleDesc.Count && desc.SampleDesc.Quality == old.SampleDesc.Quality) return true;
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
        FAILED(device->CreateDepthStencilView(texture.Get(), nullptr, res.overlayCoverage.ReleaseAndGetAddressOf()))) return false;
    res.coverageDesc = desc;
    return true;
}

bool PrepareRefraction(ID3D11Device* device, RefractionResources& res, const D3D11_TEXTURE2D_DESC& backBuffer, unsigned layers) {
    if (!res.fullscreen) {
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
        ComPtr<ID3DBlob> code;
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
        if (FAILED(D3DCompile(fullscreen, sizeof(fullscreen) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), nullptr)) ||
            !CreateShader(device, composite, res.composite) || !CreateShader(device, part.c_str(), res.partShader) ||
            !CreateShader(device, glove.c_str(), res.gloveShader) ||
            !CreateShader(device, grip.c_str(), res.gripShader)) return false;
        D3D11_BUFFER_DESC labels{};
        labels.ByteWidth = 16;
        labels.Usage = D3D11_USAGE_IMMUTABLE;
        labels.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constexpr float MaterialLevels[][2]{{0.16f, 0.44f}, {0.12f, 0.34f}, {0.72f, 0.28f}};
        for (unsigned i = 0; i < res.partLabels.size(); ++i) {
            const auto& level = MaterialLevels[i / 8];
            const FLOAT id[4]{float(i % 8 + 1 + 16 * (i / 8)), level[0], level[1]};
            D3D11_SUBRESOURCE_DATA initial{id, 0, 0};
            if (FAILED(device->CreateBuffer(&labels, &initial, res.partLabels[i].GetAddressOf()))) return false;
        }
        D3D11_BUFFER_DESC parameters{};
        parameters.ByteWidth = 16 + MaxGhostLayers * 4 + 32;
        parameters.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED |
            D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        if (FAILED(device->CreateDepthStencilState(&depth, res.partDepth.GetAddressOf())) ||
            FAILED(device->CreateBuffer(&parameters, nullptr, res.parameters.GetAddressOf())) ||
            FAILED(device->CreateSamplerState(&sampler, res.linearSampler.GetAddressOf())) ||
            FAILED(device->CreateBlendState(&blend, res.rgbBlend.GetAddressOf())) ||
            FAILED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                res.fullscreen.GetAddressOf()))) return false;
    }
    const bool sameSize = res.scene && res.surface.Width == backBuffer.Width &&
        res.surface.Height == backBuffer.Height && res.surface.Format == backBuffer.Format;
    const DXGI_FORMAT maskFormat = settings.glassVisual ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8_UNORM;
    if (sameSize && res.maskFormat == maskFormat && res.slices.size() >= layers) return true;
    auto desc = backBuffer;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> scene, mask;
    ComPtr<ID3D11ShaderResourceView> sceneView, maskView;
    if (!sameSize && (FAILED(device->CreateTexture2D(&desc, nullptr, scene.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, sceneView.GetAddressOf())))) return false;
    desc.Format = maskFormat;
    desc.ArraySize = layers;
    desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.ArraySize = layers;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, mask.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(mask.Get(), &srv, maskView.GetAddressOf()))) return false;
    std::vector<ComPtr<ID3D11RenderTargetView>> slices(layers);
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = desc.Format;
    rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv.Texture2DArray.ArraySize = 1;
    for (unsigned i = 0; i < layers; ++i) {
        rtv.Texture2DArray.FirstArraySlice = i;
        if (FAILED(device->CreateRenderTargetView(mask.Get(), &rtv, slices[i].GetAddressOf()))) return false;
    }
    res.surface = backBuffer;
    if (!sameSize) {
        res.scene = std::move(scene);
        res.sceneView = std::move(sceneView);
    }
    res.slices = std::move(slices);
    res.maskView = std::move(maskView);
    res.maskFormat = maskFormat;
    return true;
}

void CompositeRefraction(Context* c, DeviceResources& res, ID3D11RenderTargetView* target,
    unsigned layers, const std::array<float, MaxGhostLayers>& opacities, bool playerFront) {
    auto& refraction = res.refraction;
    Original<SetTargets>(c, Targets)(c, 1, &target, nullptr);
    Original<SetLayout>(c, InputLayout)(c, nullptr);
    Original<SetTopology>(c, Topology)(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Original<SetVS>(c, VsShader)(c, refraction.fullscreen.Get(), nullptr, 0);
    Original<SetPS>(c, PsShader)(c, refraction.composite.Get(), nullptr, 0);
    Original<SetBlend>(c, Blend)(c, refraction.rgbBlend.Get(), nullptr, UINT_MAX);
    auto* sampler = refraction.linearSampler.Get();
    c->PSSetSamplers(0, 1, &sampler);
    ID3D11ShaderResourceView* views[]{refraction.sceneView.Get(), refraction.maskView.Get()};
    c->PSSetShaderResources(0, 2, views);
    struct Parameters {
        float pixels, time, emission, jointOpacity;
        std::array<float, MaxGhostLayers> opacities;
        UINT layers, playerLayer;
        float muscleHighlight, glassRefraction;
        float surfaceBrightness;
        float padding[3];
    };
    const Parameters constants{float(settings.refractionPixels), float(GetTickCount64() % 8000) * 0.0007853982f,
        settings.glowStrength / 100.0f, settings.jointOpacity / 100.0f, opacities, layers,
        playerFront ? layers : UINT_MAX, settings.muscleHighlight / 100.0f,
        settings.glassVisual ? float(settings.glassRefraction) : -1.0f,
        settings.brightness / 100.0f, {}};
    c->UpdateSubresource(refraction.parameters.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffers[]{refraction.parameters.Get(), res.colors.back().Get()};
    Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 1, 2, buffers);
    c->Draw(3, 0);
    ID3D11ShaderResourceView* empty[2]{};
    c->PSSetShaderResources(0, 2, empty);
}

void DrawOverlayPart(Context* c, const OverlayDraw& packet, ID3D11Buffer* camera) {
    Original<SetLayout>(c, InputLayout)(c, packet.layout.Get());
    Original<SetTopology>(c, Topology)(c, packet.topology);
    Original<SetIndex>(c, IndexBuffer)(c, packet.index.Get(), packet.indexFormat, packet.indexOffset);
    Original<SetVS>(c, VsShader)(c, packet.shader.Get(), nullptr, 0);
    std::array<ID3D11Buffer*, ConstantSlots> constants{};
    std::array<ID3D11Buffer*, VertexSlots> vertices{};
    for (UINT i = 0; i < ConstantSlots; ++i) constants[i] = packet.pose.constants[i].Get();
    if (camera) constants[ViewProjectionSlot] = camera;
    for (UINT i = 0; i < VertexSlots; ++i) vertices[i] = packet.pose.vertices[i].Get();
    Original<SetBuffers>(c, VsConstants)(c, 0, ConstantSlots, constants.data());
    Original<SetVertex>(c, VertexBuffers)(c, 0, VertexSlots, vertices.data(), packet.strides.data(), packet.offsets.data());
    CallDraw(c, packet.draw);
}

bool RenderOverlay(IDXGISwapChain* swap, ID3D11Device* device, DeviceResources& res) {
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))) ||
        FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, target.GetAddressOf()))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    if (!PrepareOverlay(device, res, desc)) return false;
    ComPtr<Context> immediate;
    device->GetImmediateContext(immediate.GetAddressOf());
    if (!immediate) return false;

    auto& draws = res.overlayDraws;
    // Preserve the anchor under the moving trail and layer the newest pose last.
    std::stable_sort(draws.begin(), draws.end(), [](const OverlayDraw& a, const OverlayDraw& b) {
        const bool anchorA = a.anchorOpacity > 0;
        const bool anchorB = b.anchorOpacity > 0;
        return anchorA != anchorB ? anchorA : a.pose.renderFrame < b.pose.renderFrame;
    });
    const auto samePose = [](const OverlayDraw& a, const OverlayDraw& b) {
        return a.pose.renderFrame == b.pose.renderFrame && (a.anchorOpacity > 0) == (b.anchorOpacity > 0);
    };
    unsigned layerCount = 1;
    for (size_t i = 1; i < draws.size(); ++i)
        if (!samePose(draws[i - 1], draws[i])) ++layerCount;
    struct OverlayScope {
        OverlayScope() { drawingExtra = true; }
        ~OverlayScope() { drawingExtra = false; }
    } extra;
    auto* c = res.overlayContext.Get();
    auto& refraction = res.refraction;
    const auto& sceneDepth = res.sceneDepth;
    const bool hasSceneDepth = sceneDepth.frame == renderFrame && sceneDepth.view;
    const bool playerFront = res.playerFront.frame == renderFrame && res.playerFront.count;
    bool useRefraction = desc.SampleDesc.Count == 1 && !refraction.unavailable && layerCount <= MaxGhostLayers;
    if (useRefraction && !PrepareRefraction(device, refraction, desc, layerCount + unsigned(playerFront))) {
        refraction = {};
        refraction.unavailable = true;
        useRefraction = false;
        Log("REFRACTION_UNAVAILABLE");
    }
    // Keep a visible character untouched even if this backbuffer cannot build the front mask.
    if (playerFront && !useRefraction) return false;
    if (hasSceneDepth && !sceneDepth.frozen) CopySceneDepth(c, sceneDepth);
    auto* rtv = target.Get();
    if (useRefraction) c->CopyResource(refraction.scene.Get(), backBuffer.Get());
    auto* stencil = res.overlayCoverage.Get();
    const D3D11_VIEWPORT viewport{0, 0, float(desc.Width), float(desc.Height),
        hasSceneDepth ? sceneDepth.viewport.MinDepth : 0, hasSceneDepth ? sceneDepth.viewport.MaxDepth : 1};
    if (!useRefraction) Original<SetTargets>(c, Targets)(c, 1, &rtv, stencil);
    Original<SetDepth>(c, Depth)(c, useRefraction ? refraction.partDepth.Get() : res.overlayStencil.Get(), 0);
    Original<SetViewport>(c, Viewports)(c, 1, &viewport);
    Original<SetPS>(c, PsShader)(c, useRefraction ? refraction.partShader.Get() : res.ghostShader.Get(), nullptr, 0);
    Original<SetBlend>(c, Blend)(c, useRefraction ? nullptr : res.ghostBlend.Get(), nullptr, UINT_MAX);
    auto* lighting = res.ghostLighting.Get();
    Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 1, 1, &lighting);
    const FLOAT depthParameters[]{float(sceneDepth.desc.Width) / desc.Width, float(sceneDepth.desc.Height) / desc.Height,
        hasSceneDepth ? (sceneDepth.reversed ? -1.0f : 1.0f) : 0.0f, 0.00005f};
    auto* depthConstants = res.sceneDepthConstants.Get();
    c->UpdateSubresource(depthConstants, 0, nullptr, depthParameters, 0, 0);
    Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 3, 1, &depthConstants);
    auto* depthTexture = hasSceneDepth ? sceneDepth.view.Get() : nullptr;
    c->PSSetShaderResources(2, 1, &depthTexture);
    if (useRefraction) {
        const FLOAT projection[4]{1.0f / desc.Height};
        c->UpdateSubresource(lighting, 0, nullptr, projection, 0, 0);
    }
    c->RSSetState(res.overlayRasterizer.Get());
    const OverlayDraw* previous = nullptr;
    std::array<float, MaxGhostLayers> opacities{};
    float lastOpacity = -1;
    unsigned lastPartLabel = UINT_MAX;
    auto* partShader = refraction.partShader.Get();
    unsigned poses = 0;
    for (const auto& packet : draws) {
        if (!previous || !samePose(*previous, packet)) {
            if (useRefraction) {
                // The nearest surface determines the anatomical label within this pose.
                auto* layer = refraction.slices[poses].Get();
                const FLOAT clear[4]{};
                c->ClearRenderTargetView(layer, clear);
                c->ClearDepthStencilView(stencil, D3D11_CLEAR_DEPTH, 1, 0);
                Original<SetTargets>(c, Targets)(c, 1, &layer, stencil);
                opacities[poses] = packet.opacity;
            } else {
                c->ClearDepthStencilView(stencil, D3D11_CLEAR_STENCIL, 1, 0);
            }
            ++poses;
        }
        previous = &packet;
        const bool mixed = packet.material == GhostMaterial::Glove || packet.material == GhostMaterial::GripAtlas;
        const auto material = mixed ? GhostMaterial::Armor : packet.material;
        const unsigned labelIndex = unsigned(material) * 8 + unsigned(packet.bodyPart) - 1;
        auto* nextShader = packet.material == GhostMaterial::GripAtlas ? refraction.gripShader.Get() :
            packet.material == GhostMaterial::Glove ? refraction.gloveShader.Get() : refraction.partShader.Get();
        if (useRefraction && nextShader != partShader) {
            Original<SetPS>(c, PsShader)(c, nextShader, nullptr, 0);
            partShader = nextShader;
        }
        if (useRefraction && labelIndex != lastPartLabel) {
            auto* label = refraction.partLabels[labelIndex].Get();
            Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 2, 1, &label);
            lastPartLabel = labelIndex;
        }
        if (!useRefraction && packet.opacity != lastOpacity) {
            const float glow = settings.glowStrength / 100.0f;
            const FLOAT constants[4]{packet.opacity,
                settings.glassVisual ? settings.brightness / 100.0f + glow : 1.0f + glow};
            c->UpdateSubresource(lighting, 0, nullptr, constants, 0, 0);
            lastOpacity = packet.opacity;
        }
        if (!useRefraction) {
            auto* color = packet.color.Get();
            Original<SetBuffers>(c, PsConstants)(c, ColorSlot, 1, &color);
        }
        DrawOverlayPart(c, packet, nullptr);
    }
    if (playerFront) {
        if (partShader != refraction.partShader.Get())
            Original<SetPS>(c, PsShader)(c, refraction.partShader.Get(), nullptr, 0);
        auto* layer = refraction.slices[poses].Get();
        const FLOAT clear[4]{};
        c->ClearRenderTargetView(layer, clear);
        c->ClearDepthStencilView(stencil, D3D11_CLEAR_DEPTH, 1, 0);
        Original<SetTargets>(c, Targets)(c, 1, &layer, stencil);
        auto* label = refraction.partLabels[0].Get();
        Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 2, 1, &label);
        for (unsigned i = 0; i < res.playerFront.count; ++i)
            DrawOverlayPart(c, res.playerFront.parts[i], res.frameCamera.Get());
    }
    if (useRefraction) CompositeRefraction(c, res, target.Get(), poses, opacities, playerFront);
    ComPtr<ID3D11CommandList> list;
    if (FAILED(Original<FinishList>(c, Finish)(c, FALSE, list.GetAddressOf())) || !list) return false;
    Original<ExecuteList>(immediate.Get(), Execute)(immediate.Get(), list.Get(), TRUE);
    if (loggedOverlays++ < 16)
        Log("OVERLAY_DRAW frame=%llu draws=%u poses=%u backbuffer=%ux%u brightness=%u emission=%u refraction=%u segmented=%u player_front=%u style=%s scene_depth=%ux%u frozen=%u",
            static_cast<unsigned long long>(renderFrame), static_cast<unsigned>(draws.size()), poses, desc.Width,
            desc.Height, settings.brightness, settings.glowStrength,
            useRefraction ? (settings.glassVisual ? settings.glassRefraction : settings.refractionPixels) : 0,
            unsigned(useRefraction), playerFront ? res.playerFront.count : 0,
            settings.glassVisual ? "Sigma2" : "Classic", hasSceneDepth ? sceneDepth.desc.Width : 0,
            hasSceneDepth ? sceneDepth.desc.Height : 0, hasSceneDepth && sceneDepth.frozen);
    return true;
}

HRESULT STDMETHODCALLTYPE ObservePresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
    // A windowed swap chain can keep presenting after another window gains focus.
    if (hooksReady && swap == gameSwap && !(flags & DXGI_PRESENT_TEST)) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        for (auto& entry : resources) {
            auto& res = entry.second;
            try {
                if (settings.enabled) BuildOverlayDraws(res, PoseClockUs());
                if (res.overlayDraws.empty()) continue;
                if (settings.enabled && !RenderOverlay(swap, entry.first, res) && loggedOverlays++ < 16)
                    Log("OVERLAY_DRAW_FAILED");
            } catch (...) {
                Log("OVERLAY_DRAW_FAILED exception");
            }
            res.overlayDraws.clear();
        }
        ++renderFrame;
    }
    return originalPresent(swap, syncInterval, flags);
}

struct Hook { Slot slot; void* handler; };
const Hook hooks[]{
    {VsConstants, reinterpret_cast<void*>(TrackVSConstants)},
    {VertexBuffers, reinterpret_cast<void*>(TrackVertex)}, {IndexBuffer, reinterpret_cast<void*>(TrackIndex)},
    {InputLayout, reinterpret_cast<void*>(TrackLayout)}, {Topology, reinterpret_cast<void*>(TrackTopology)},
    {PsShader, reinterpret_cast<void*>(TrackPS)}, {VsShader, reinterpret_cast<void*>(TrackVS)},
    {Targets, reinterpret_cast<void*>(TrackTargets)}, {TargetsAndUavs, reinterpret_cast<void*>(TrackTargetsUav)},
    {Blend, reinterpret_cast<void*>(TrackBlend)}, {Depth, reinterpret_cast<void*>(TrackDepth)},
    {ClearDepthView, reinterpret_cast<void*>(ObserveClearDepth)},
    {Viewports, reinterpret_cast<void*>(TrackViewport)}, {Clear, reinterpret_cast<void*>(TrackClear)},
    {Indexed, reinterpret_cast<void*>(ObserveIndexed)}, {IndexedInstanced, reinterpret_cast<void*>(ObserveIndexedInstanced)},
    {Finish, reinterpret_cast<void*>(ObserveFinish)}, {Execute, reinterpret_cast<void*>(ObserveExecute)}
};

bool HasSharedContextHook(Context* context, Slot slot) {
    return hooksReady && sharedRenderTargets.count((*reinterpret_cast<void***>(context))[slot]);
}

const AfterimageDrawHandlers sharedDrawHandlers{
    [](Context* c, UINT count, UINT first, INT base) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        if (!HasSharedContextHook(c, Indexed)) return false;
        ObserveIndexed(c, count, first, base);
        return true;
    },
    [](Context* c, UINT count, UINT instances, UINT first, INT base, UINT instance) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        if (!HasSharedContextHook(c, IndexedInstanced)) return false;
        ObserveIndexedInstanced(c, count, instances, first, base, instance);
        return true;
    },
    [](Context* c, ID3D11CommandList* list, BOOL restore) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        if (!HasSharedContextHook(c, Execute)) return false;
        ObserveExecute(c, list, restore);
        return true;
    }
};

bool BindRenderHooks(ID3D11Device* device, Context* immediate, IDXGISwapChain* swap) {
    ComPtr<Context> deferred;
    if (FAILED(device->CreateDeferredContext(0, deferred.GetAddressOf()))) return false;
    auto* present = (*reinterpret_cast<void***>(swap))[8];
    void* nextPresent = nullptr;
    if (MH_CreateHook(present, reinterpret_cast<void*>(ObservePresent), &nextPresent) != MH_OK) {
        Log("PRESENT_HOOK_FAILED");
        return false;
    }
    originalPresent = reinterpret_cast<Present>(nextPresent);
    std::vector<void*> ownedTargets;
    auto rollback = [&] {
        MH_RemoveHook(present);
        for (auto target : ownedTargets) MH_RemoveHook(target);
        originals.clear();
        sharedRenderTargets.clear();
        return false;
    };
    for (auto c : {immediate, deferred.Get()}) {
        auto table = *reinterpret_cast<void***>(c);
        for (const auto& hook : hooks) {
            void* target = table[hook.slot];
            if (originals.count(target)) continue;
            if (controllerHooks.original) {
                if (auto* next = controllerHooks.original(target, hook.slot)) {
                    originals.emplace(target, next);
                    sharedRenderTargets.insert(target);
                    continue;
                }
            }
            void* next = nullptr;
            auto status = MH_CreateHook(target, hook.handler, &next);
            if (status != MH_OK) {
                Log("HOOK_FAILED slot=%u status=%s", unsigned(hook.slot), MH_StatusToString(status));
                return rollback();
            }
            originals.emplace(target, next);
            ownedTargets.push_back(target);
        }
    }
    if (MH_QueueEnableHook(present) != MH_OK) return rollback();
    for (auto target : ownedTargets)
        if (MH_QueueEnableHook(target) != MH_OK) return rollback();
    if (MH_ApplyQueued() != MH_OK) return rollback();
    gameSwap = swap;
    hooksReady = true;
    if (!sharedRenderTargets.empty() && controllerHooks.connect) controllerHooks.connect(&sharedDrawHandlers);
    Log("RENDERER_READY hooks=%u shared=%u present=%p", unsigned(originals.size()),
        unsigned(sharedRenderTargets.size()), present);
    return true;
}

void ObserveGameFrame(void* renderer, void* options) {
    // The verified game's frame routine owns the actual device and swap chain.
    // Creating a second swap chain through a proxy can replace its game window.
    const auto* data = static_cast<const BYTE*>(renderer);
    auto* swap = *reinterpret_cast<IDXGISwapChain* const*>(data + 0x3080);
    if (swap != gameSwap) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        static bool attempted = false;
        if (!attempted && swap) {
            auto* device = *reinterpret_cast<ID3D11Device* const*>(data + 0x3078);
            auto* immediate = *reinterpret_cast<Context* const*>(data + 0x10);
            if (device && immediate) {
                attempted = true;
                if (!BindRenderHooks(device, immediate, swap)) Log("INITIALIZATION_FAILED");
            }
        } else if (hooksReady) {
            gameSwap = swap;
        }
    }
    originalGameFrame(renderer, options);
}

bool InstallHooks(GameCompatibility::Kind kind) {
    constexpr uintptr_t FrameRoutine = 0x35780;
    constexpr size_t FrameRoutineSize = 0x218;
    const char* frameHash = kind == GameCompatibility::Kind::Original104 ?
        "4AE68FD9E758F585BDCD6A05E4F037F538A0EBA33BEA0B47A7A6E3D7BA495D01" :
        "96EDFA9CE9E1E2342AC3015BC206892B8EF86D27B7FA53EF6AD7781D511DFDA0";
    auto* frame = reinterpret_cast<BYTE*>(gameBase + FrameRoutine);
    char hash[65]{};
    if (!GameCompatibility::Sha256(frame, FrameRoutineSize, hash) || strcmp(hash, frameHash) != 0) return false;
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    void* next = nullptr;
    if (MH_CreateHook(frame, reinterpret_cast<void*>(ObserveGameFrame), &next) != MH_OK) {
        return false;
    }
    originalGameFrame = reinterpret_cast<GameFrame>(next);
    if (MH_EnableHook(frame) != MH_OK) {
        MH_RemoveHook(frame);
        return false;
    }
    return true;
}

unsigned ReadNumber(const wchar_t* name, unsigned fallback, unsigned low, unsigned high) {
    return std::clamp(GetPrivateProfileIntW(L"Afterimage", name, fallback, ini.c_str()), low, high);
}

void ReadSettings() {
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    const bool previouslyEnabled = settings.enabled;
    const bool previousMaterial = settings.glassVisual;
    const Appearance previous{settings.alpha, settings.glowStrength, settings.tint, settings.muscleHighlight,
        settings.brightness};
    wchar_t material[24]{};
    GetPrivateProfileStringW(L"Afterimage", L"Material", L"Classic", material, std::size(material), ini.c_str());
    settings.glassVisual = _wcsicmp(material, L"Sigma2") == 0;
    settings.enabled = ReadNumber(L"Enabled", 1, 0, 1) != 0;
    settings.autoET = ReadNumber(L"AutomaticDragonET", 1, 0, 1) != 0;
    settings.autoUT = ReadNumber(L"AutomaticDragonUT", 1, 0, 1) != 0;
    settings.autoWindRun = ReadNumber(L"AutomaticWindRun", 1, 0, 1) != 0;
    settings.autoWallDownslash = ReadNumber(L"AutomaticWallDownslash", 1, 0, 1) != 0;
    settings.autoSwallow = ReadNumber(L"AutomaticFlyingSwallow", 1, 0, 1) != 0;
    settings.autoWhiteFeather = ReadNumber(L"AutomaticWhiteFeather", 1, 0, 1) != 0;
    settings.interval = ReadNumber(L"IntervalMs", 12, 6, 150);
    settings.lifetime = ReadNumber(L"LifetimeMs", 185, 65, 650);
    settings.copies = ReadNumber(L"Copies", 5, 1, 5);
    settings.previewMs = ReadNumber(L"PreviewMs", 8000, 500, 10000);
    settings.revealFrames = ReadNumber(L"RevealFrames", 9, 1, 45);
    settings.utRevealFrames = ReadNumber(L"UTRevealFrames", 12, 1, 45);
    settings.refractionPixels = ReadNumber(L"RefractionPixels", 1, 0, 8);
    settings.glassRefraction = ReadNumber(L"GlassRefractionPixels", 19, 0, 36);
    settings.jointOpacity = ReadNumber(L"JointOpacityPercent", 76, 0, 100);
    for (unsigned i = 0; i < appearances.size(); ++i) {
        const auto costume = static_cast<Costume>(i);
        const bool exposedSkin = costume == Costume::Black || costume == Costume::Blue || costume == Costume::Grip;
        auto load = [&](const wchar_t* section) -> Appearance {
            auto read = [&](const wchar_t* key, unsigned fallback, unsigned maximum) {
                const auto value = static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.c_str()));
                return static_cast<unsigned>(std::clamp(value, 0, static_cast<int>(maximum)));
            };
            return {read(L"OpacityPercent", 40, 100),
                read(L"GlowStrengthPercent", 68, settings.glassVisual ? MaxSigma2GlowStrength : 100),
                {read(L"TintR", 144, 255), read(L"TintG", 163, 255), read(L"TintB", 253, 255)},
                exposedSkin ? read(L"MuscleHighlightPercent", 45, 100) : 0,
                read(L"SurfaceBrightnessPercent", 100, MaxSurfaceBrightness)};
        };
        if (settings.glassVisual) {
            const auto section = std::wstring(L"Afterimage.Sigma2") + wcschr(Costumes[i].section, L'.');
            appearances[i] = load(section.c_str());
        } else {
            appearances[i] = load(Costumes[i].section);
        }
    }
    ApplyAppearance();
    if (previousMaterial != settings.glassVisual)
        Log("MATERIAL_RELOAD %s", settings.glassVisual ? "Sigma2" : "Classic");
    if (activeCostume != Costume::Unknown && (previous.alpha != settings.alpha ||
        previous.glow != settings.glowStrength || previous.tint != settings.tint ||
        previous.muscleHighlight != settings.muscleHighlight || previous.brightness != settings.brightness)) {
        Log("APPEARANCE_RELOAD costume=%u alpha=%u glow=%u rgb=%u,%u,%u muscle=%u brightness=%u",
            unsigned(activeCostume), settings.alpha, settings.glowStrength, settings.tint[0], settings.tint[1],
            settings.tint[2], settings.muscleHighlight, settings.brightness);
    }
    if (previouslyEnabled != settings.enabled) {
        ResetPoses();
        chargeTimeline = {};
        airCombo = {};
        wallDive = {};
        swallow = {};
        previousFeatherFrame = 0;
        effectFrame = {};
        windRunBranch = 0;
        Log("CONFIG enabled=%u", unsigned(settings.enabled));
    }
    const unsigned serial = GetPrivateProfileIntW(L"Diagnostics", L"Capture", 0, ini.c_str());
    if (serial && serial != captureSerial) {
        captureSerial = serial;
        captureKeys.clear();
        captureUntil = GetTickCount64() + 900;
        Log("CAPTURE_BEGIN serial=%u", serial);
    }
}

unsigned PlayerScript(uintptr_t base, float& motionFrame) {
    uintptr_t bank = 0, pc = 0;
    BYTE header[16]{};
    SIZE_T bytes = 0;
    auto read = [&](uintptr_t at, void* dst, size_t size) {
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), dst, size, &bytes) && bytes == size;
    };
    if (!read(base + 0xcc0ab0 + 0x26d0, &bank, sizeof(bank)) ||
        !read(base + 0x3757570 + 0x25d0, &pc, sizeof(pc)) || !bank || pc < bank || pc - bank >= 1889840 ||
        !read(bank, header, sizeof(header)) || !read(base + 0x3757570 + 0x10, &motionFrame, sizeof(motionFrame))) return 0;
    unsigned length;
    memcpy(&length, header + 12, sizeof(length));
    return memcmp(header, "chr_dat", 8) == 0 && length == 1889840 ? static_cast<unsigned>(pc - bank) : 0;
}

DWORD WINAPI PollAfterimage(void*) {
    unsigned lastRead = 0;
    unsigned lastReport = 0;
    bool wasPreviewKey = false;
    bool wasStopKey = false;
    for (;;) {
        DWORD owner = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &owner);
        const bool focus = owner == GetCurrentProcessId();
        bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool previewKey = focus && control && (GetAsyncKeyState(VK_F11) & 0x8000);
        bool stopKey = focus && control && (GetAsyncKeyState(VK_F12) & 0x8000);
        if (previewKey && !wasPreviewKey) {
            std::lock_guard<std::recursive_mutex> guard(renderMutex);
            previewUntil = GetTickCount64() + settings.previewMs;
            Log("PREVIEW_STARTED");
        }
        if (stopKey && !wasStopKey) {
            std::lock_guard<std::recursive_mutex> guard(renderMutex);
            previewUntil = 0;
            ResetPoses();
            Log("PREVIEW_STOPPED");
        }
        wasPreviewKey = previewKey;
        wasStopKey = stopKey;
        auto now = GetTickCount();
        if (now - lastRead >= 350) {
            lastRead = now;
            ReadSettings();
        }
        if (now - lastReport >= 2000) {
            lastReport = now;
            std::lock_guard<std::recursive_mutex> guard(renderMutex);
            if (samples) {
                Log("POSE_SAMPLES count=%llu average_gap_us=%llu", static_cast<unsigned long long>(samples),
                    static_cast<unsigned long long>(intervalCount ? intervalTotalUs / intervalCount : 0));
                samples = intervalTotalUs = intervalCount = 0;
            }
        }
        Sleep(8);
    }
}
}

bool StartAfterimage(const wchar_t* root, GameCompatibility::Kind kind, ControllerRenderHooks shared) {
    directory = std::wstring(root) + L"\\NGS1Rumble";
    ini = std::wstring(root) + L"\\NGS1Rumble.ini";
    logFile = _wfsopen((directory + L"\\NGS1Afterimage.log").c_str(), L"wb", _SH_DENYNO);
    gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    controllerHooks = shared;
    Log("GAME pid=%lu build=%s afterimage=0.6.9", GetCurrentProcessId(), GameCompatibility::Name(kind));
    if (GetModuleHandleW(L"NGS1Afterimage.dll")) {
        Log("LEGACY_AFTERIMAGE_ALREADY_LOADED");
        return false;
    }
    ReadSettings();
    Log("VISUAL_STYLE %s", settings.glassVisual ? "Sigma2" : "Classic");
    if (kind == GameCompatibility::Kind::Unsupported || !InstallHooks(kind)) {
        Log("INITIALIZATION_FAILED");
        return false;
    }
    HANDLE worker = CreateThread(nullptr, 0, PollAfterimage, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
    Log("FRAME_HOOK_READY hot_reload=%u", worker != nullptr);
    return true;
}
