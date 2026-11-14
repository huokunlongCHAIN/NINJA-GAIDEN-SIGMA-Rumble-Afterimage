#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
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
#include "render_vtable.h"
#include "graphics_adapter.h"
#include "afterimage_shaders.generated.h"

namespace {
using Microsoft::WRL::ComPtr;
using Context = ID3D11DeviceContext;
using PrepareGameDraw = bool (*)(void*, int);
constexpr size_t GameContextOffset = 0x10;
constexpr size_t RendererWorkerOffset = 0x2b28;
constexpr size_t GameDeviceOffset = 0x3078;
constexpr size_t GameSwapOffset = 0x3080;
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
constexpr unsigned IntegratedGhostLayers = 2;
constexpr unsigned MaxSurfaceBrightness = 200;
constexpr unsigned MaxGlowStrength = 200;

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
using ExecuteList = ExecuteDrawList;
using FinishList = HRESULT (STDMETHODCALLTYPE*)(Context*, BOOL, ID3D11CommandList**);
using ClearContext = void (STDMETHODCALLTYPE*)(Context*);
using CreateDeferred = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, UINT, Context**);
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
    std::array<UINT, VertexSlots> strides{}, offsets{};
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
    UINT indexOffset = 0;
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

struct QueuedPose {
    const PoseFrame* pose;
    ID3D11Buffer* color;
    float opacity;
    bool anchor;
};

struct Readback {
    unsigned id;
    ComPtr<ID3D11Texture2D> texture;
};

struct ContextState {
    ID3D11Device* device = nullptr; // The context owns a reference to its device.
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
    ComPtr<ID3D11PixelShader> composite, partShader, gloveShader, gripShader, playerCoverage;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11BlendState> rgbBlend;
    ComPtr<ID3D11Texture2D> scene;
    ComPtr<ID3D11ShaderResourceView> sceneView, maskView;
    ComPtr<ID3D11DepthStencilState> partDepth;
    ComPtr<ID3D11Buffer> parameters;
    std::array<ComPtr<ID3D11Buffer>, 8 * 3> partLabels;
    std::vector<ComPtr<ID3D11RenderTargetView>> slices;
    D3D11_TEXTURE2D_DESC surface{};
    UINT maskWidth = 0, maskHeight = 0;
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

struct OverlayCoverageProbe {
    ComPtr<ID3D11Query> geometry, composite;
    ComPtr<ID3D11Query> frequency, start, end;
    uint64_t frame = 0;
    unsigned samples = 0;
    unsigned draws = 0;
    bool pending = false;
    bool timed = false;
    bool sceneDepth = false;
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
    OverlayCoverageProbe coverageProbe;
    std::vector<QueuedPose> overlayPoses;
    std::array<PoseFrame, PoseHistorySize> trail;
    unsigned nextTrail = 0;
    PoseFrame chargeOrigin;
    PoseFrame leadingPose;
    PoseFrame playerFront;
    uint64_t frontDecisionFrame = UINT64_MAX;
    bool captureFront = false;
    bool capturePose = false;
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
enum class RenderProfile { Auto, Standard, Integrated };

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
    unsigned sceneDepthMode = 0;
    RenderProfile renderProfile = RenderProfile::Auto;
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
std::unordered_map<void**, const RenderVtable::Slot*> originals;
std::unordered_set<void**> sharedRenderSlots;
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
struct RenderActivity {
    unsigned long long indexed = 0, stateReady = 0, costume = 0, sceneColor = 0, presented = 0;
    unsigned long long prepared = 0, rebound = 0;
    unsigned long long fastRebind = 0;
    unsigned long long rebindChecks = 0;
    unsigned long long overlayFrames = 0, overlayCpuUs = 0, peakOverlayCpuUs = 0;
    unsigned long long peakGhostDraws = 0, peakFrontDraws = 0, skippedGhostLayers = 0;
    struct Cadence {
        unsigned long long frames = 0, over18Ms = 0, over25Ms = 0, peakUs = 0;

        void Record(unsigned long long elapsedUs) {
            ++frames;
            over18Ms += elapsedUs > 18000;
            over25Ms += elapsedUs > 25000;
            peakUs = std::max(peakUs, elapsedUs);
        }
    } cadenceWithOverlay, cadenceWithoutOverlay;
    D3D11_VIEWPORT viewport{};
};
RenderActivity activity;
uint64_t lastPresentUs = 0;
bool lastPresentHadOverlay = false;
GameFrame originalGameFrame = nullptr;
PrepareGameDraw originalPrepareGameDraw = nullptr;
bool gameWorkerLayoutVerified = false;
std::atomic<IDXGISwapChain*> gameSwap{nullptr};
unsigned captureSerial = 0;
std::atomic<bool> hooksReady{false};
thread_local bool drawingExtra = false;
thread_local bool forwardingIndexed = false;
bool intelAdapter = false;
GraphicsAdapter graphicsAdapter = GraphicsAdapter::Unknown;
bool intelDepthHook = false;
bool loggedCameraCopy = false;
bool loggedDepthSetup = false;
bool loggedDepthCopy = false;

uint64_t PoseClockUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool LimitGhostLayers() {
    if (settings.renderProfile != RenderProfile::Auto)
        return settings.renderProfile == RenderProfile::Integrated;
    return graphicsAdapter == GraphicsAdapter::Integrated || graphicsAdapter == GraphicsAdapter::Software;
}

const char* ProfileName(RenderProfile profile) {
    switch (profile) {
        case RenderProfile::Standard: return "standard";
        case RenderProfile::Integrated: return "integrated";
        default: return "auto";
    }
}

void Log(const char* format, ...);
bool SceneDepthEnabled();

void LogRenderProfile() {
    Log("RENDER_PROFILE configured=%s effective=%s adapter=%s mask_scale=%s layer_limit=%u scene_depth=%u",
        ProfileName(settings.renderProfile), LimitGhostLayers() ? "integrated" : "standard",
        GraphicsAdapterDetection::Name(graphicsAdapter), LimitGhostLayers() ? "half" : "full",
        LimitGhostLayers() ? IntegratedGhostLayers : 0, unsigned(SceneDepthEnabled()));
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
    auto* table = RenderVtable::Table(context);
    if (controllerHooks.original)
        if (auto* next = controllerHooks.original(table, slot)) return reinterpret_cast<Fn>(next);
    if (auto found = originals.find(table + slot); found != originals.end() && table[slot] == found->second->observer)
        return reinterpret_cast<Fn>(found->second->Next());
    return reinterpret_cast<Fn>(table[slot]);
}

template<class T, size_t N>
void WriteRange(std::array<T, N>& destination, UINT first, UINT count, const T* values) {
    for (UINT i = 0; i < count && first + i < N; ++i)
        destination[first + i] = values ? values[i] : T{};
}

void TrackVSConstants(SetBuffers next, Context* c, UINT first, UINT count, ID3D11Buffer* const* buffers) {
    if (drawingExtra) return next(c, first, count, buffers);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    WriteRange(contexts[c].vsConstants, first, count, buffers);
    next(c, first, count, buffers);
}

void TrackVertex(SetVertex next, Context* c, UINT first, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) {
    if (drawingExtra) return next(c, first, count, buffers, strides, offsets);
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
    next(c, first, count, buffers, strides, offsets);
}

void TrackIndex(SetIndex next, Context* c, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) {
    if (drawingExtra) return next(c, buffer, format, offset);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    if (state.index != buffer) {
        state.indexDesc = {};
        if (buffer) buffer->GetDesc(&state.indexDesc);
    }
    state.index = buffer;
    state.indexFormat = format;
    state.indexOffset = offset;
    next(c, buffer, format, offset);
}

void TrackLayout(SetLayout next, Context* c, ID3D11InputLayout* layout) {
    if (drawingExtra) return next(c, layout);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].layout = layout;
    next(c, layout);
}

void TrackTopology(SetTopology next, Context* c, D3D11_PRIMITIVE_TOPOLOGY topology) {
    if (drawingExtra) return next(c, topology);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].topology = topology;
    next(c, topology);
}

void TrackPS(SetPS next, Context* c, ID3D11PixelShader* shader, ID3D11ClassInstance* const* classes, UINT count) {
    if (drawingExtra) return next(c, shader, classes, count);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].ps = shader;
    contexts[c].psClasses = count;
    next(c, shader, classes, count);
}

void TrackVS(SetVS next, Context* c, ID3D11VertexShader* shader, ID3D11ClassInstance* const* classes, UINT count) {
    if (drawingExtra) return next(c, shader, classes, count);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].vs = shader;
    next(c, shader, classes, count);
}

void RememberTargets(ContextState& state, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth) {
    state.targets.fill(nullptr);
    state.targetCount = std::min<UINT>(count, state.targets.size());
    WriteRange(state.targets, 0, state.targetCount, views);
    state.depthView = depth;
}

void TrackTargets(SetTargets next, Context* c, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth) {
    if (drawingExtra) return next(c, count, views, depth);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    RememberTargets(state, count, views, depth);
    state.uavsBound = false;
    next(c, count, views, depth);
}

void TrackTargetsUav(SetTargetsUav next, Context* c, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth, UINT start, UINT uavCount, ID3D11UnorderedAccessView* const* uavs, const UINT* initialCounts) {
    if (drawingExtra) return next(c, count, views, depth, start, uavCount, uavs, initialCounts);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    if (count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) RememberTargets(state, count, views, depth);
    if (uavCount != D3D11_KEEP_UNORDERED_ACCESS_VIEWS) state.uavsBound = uavCount != 0;
    next(c, count, views, depth, start, uavCount, uavs, initialCounts);
}

void TrackBlend(SetBlend next, Context* c, ID3D11BlendState* blend, const FLOAT* factors, UINT mask) {
    if (drawingExtra) return next(c, blend, factors, mask);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    auto& state = contexts[c];
    state.blendState = blend;
    state.sampleMask = mask;
    state.blendFactor = {1, 1, 1, 1};
    if (factors) std::copy_n(factors, 4, state.blendFactor.begin());
    next(c, blend, factors, mask);
}

void TrackDepth(SetDepth next, Context* c, ID3D11DepthStencilState* depth, UINT stencil) {
    if (drawingExtra) return next(c, depth, stencil);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].depthState = depth;
    contexts[c].stencil = stencil;
    next(c, depth, stencil);
}

void TrackViewport(SetViewport next, Context* c, UINT count, const D3D11_VIEWPORT* viewport) {
    if (drawingExtra) return next(c, count, viewport);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    contexts[c].viewportCount = count;
    if (count && viewport) contexts[c].viewport = viewport[0];
    next(c, count, viewport);
}

void CopySceneDepth(Context* c, const SceneDepth& depth) {
    const bool first = !loggedDepthCopy;
    if (first) Log("SCENE_DEPTH_COPY_BEGIN");
    c->CopySubresourceRegion(depth.snapshot.Get(), 0, 0, 0, 0, depth.source.Get(), depth.subresource, nullptr);
    if (first) {
        loggedDepthCopy = true;
        Log("SCENE_DEPTH_COPY_READY");
    }
}

bool SceneDepthEnabled() {
    if (intelAdapter && !intelDepthHook) return false;
    return settings.sceneDepthMode == 2 || (settings.sceneDepthMode == 0 && !LimitGhostLayers());
}

void ObserveClearDepth(ClearDepth next, Context* c, ID3D11DepthStencilView* view, UINT flags, FLOAT value, UINT8 stencil) {
    if (drawingExtra) return next(c, view, flags, value, stencil);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    if (!drawingExtra && settings.enabled && SceneDepthEnabled() && (flags & D3D11_CLEAR_DEPTH)) {
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
    next(c, view, flags, value, stencil);
}

void ResetContextState(ContextState& state) {
    auto* device = state.device;
    auto readbacks = std::move(state.readbacks);
    state = ContextState{};
    state.device = device;
    state.readbacks = std::move(readbacks);
}

void TrackClear(ClearContext next, Context* c) {
    if (drawingExtra) return next(c);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    ResetContextState(contexts[c]);
    next(c);
}

template<size_t Size>
bool CreatePixelShader(ID3D11Device* device, const unsigned char (&bytecode)[Size], ComPtr<ID3D11PixelShader>& shader) {
    const HRESULT result = device->CreatePixelShader(bytecode, Size, nullptr, shader.ReleaseAndGetAddressOf());
    if (FAILED(result)) Log("CREATE_PIXEL_SHADER_FAILED hr=%08lx", result);
    return SUCCEEDED(result);
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
    if (!res.maskShader && !CreatePixelShader(device, AfterimageShaders::Mask, res.maskShader)) return false;
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
bool PrepareGhost(ID3D11Device* device, DeviceResources& res) {
    const bool firstUse = !res.fadeUnderlayBlend;
    if (!res.ghostShader && !CreatePixelShader(device, AfterimageShaders::Ghost, res.ghostShader)) return false;
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
    if (!res.fadeUnderlay || !res.gripFadeUnderlay) {
        if (!CreatePixelShader(device, AfterimageShaders::GripUnderlay, res.gripFadeUnderlay) ||
            !CreatePixelShader(device, AfterimageShaders::Underlay, res.fadeUnderlay)) return false;
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
    if (firstUse) Log("GHOST_PIPELINE_READY");
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
    if (draw.instanced) Original<InstancedIndexedDraw>(c, IndexedInstanced)(c, draw.count, 1, draw.first, draw.base, draw.instance);
    else Original<IndexedDraw>(c, Indexed)(c, draw.count, draw.first, draw.base);
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
    if (res.cameraFrame != renderFrame && state.vsConstants[ViewProjectionSlot]) {
        const bool first = !loggedCameraCopy;
        if (first) Log("CAMERA_BUFFER_COPY_BEGIN");
        const bool copied = CopyBuffer(c, device, state.vsConstants[ViewProjectionSlot], res.frameCamera);
        if (copied) res.cameraFrame = renderFrame;
        if (first) {
            loggedCameraCopy = true;
            Log("CAMERA_BUFFER_COPY_END ok=%u", unsigned(copied));
        }
    }
    if (res.captureFront) {
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
    // Present consumes the queue while holding renderMutex. Captured buffers stay with their poses.
    res.overlayPoses.push_back({&pose, color, opacity, anchor});
}

unsigned BudgetOverlayPoses(std::vector<QueuedPose>& poses) {
    std::stable_sort(poses.begin(), poses.end(), [](const QueuedPose& a, const QueuedPose& b) {
        return a.anchor != b.anchor ? a.anchor : a.pose->frame < b.pose->frame;
    });
    if (!LimitGhostLayers() || poses.size() <= IntegratedGhostLayers) return 0;

    // Keep the newest stationary imprint and the newest moving silhouette.
    // A trail without an imprint keeps its oldest and newest silhouettes.
    const auto firstMoving = std::partition_point(poses.begin(), poses.end(),
        [](const QueuedPose& pose) { return pose.anchor; });
    const auto older = firstMoving == poses.end() ? poses.end() - 2 :
        firstMoving == poses.begin() ? poses.begin() : firstMoving - 1;
    const auto skipped = static_cast<unsigned>(poses.size()) - IntegratedGhostLayers;
    poses.front() = *older;
    poses[1] = poses.back();
    poses.resize(IntegratedGhostLayers);
    return skipped;
}

float TrailStrength(const PoseFrame& pose, uint64_t nowUs) {
    if (!pose.count || pose.frame >= renderFrame || pose.timeUs > nowUs) return 0;
    const uint64_t age = pose.swallowClock ? renderFrame - swallow.pauseFrames - pose.swallowClock :
        pose.fadeFrames ? renderFrame - pose.frame : nowUs - pose.timeUs;
    const uint64_t duration = pose.fadeFrames ? pose.fadeFrames : 1000ull * settings.lifetime;
    return age < duration ? float(duration - age) / float(duration) : 0;
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

    std::array<const PoseFrame*, PoseHistorySize> visible{};
    std::array<float, PoseHistorySize> strengths{};
    const auto& latest = res.trail[(res.nextTrail + PoseHistorySize - 1) % PoseHistorySize];
    const auto& leading = latest.frame == renderFrame ? latest : res.leadingPose;
    const bool drawLeading = effectFrame.leading && leading.frame == renderFrame && leading.count;
    unsigned count = 0;
    for (unsigned i = 0; i < PoseHistorySize; ++i) {
        const auto& pose = res.trail[(res.nextTrail + i) % PoseHistorySize];
        const float strength = TrailStrength(pose, nowUs);
        if (strength > 0) {
            strengths[count] = strength;
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
        res.frontDecisionFrame = UINT64_MAX;
        res.overlayPoses.clear();
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
    const bool first = !loggedDepthSetup;
    if (first) {
        loggedDepthSetup = true;
        Log("SCENE_DEPTH_SETUP_BEGIN");
    }
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
    if (first) Log("SCENE_DEPTH_SETUP_READY size=%ux%u format=%u", desc.Width, desc.Height, unsigned(readFormat));
}

void UpdatePoseCapture(DeviceResources& res, uint64_t nowUs, bool recording, bool stomp) {
    if (res.frontDecisionFrame == renderFrame) return;
    res.frontDecisionFrame = renderFrame;
    const bool charge = chargeTimeline.current.move != ChargeMove::None && chargeTimeline.current.visual != ChargeVisual::Off;
    const auto hasPose = [](const auto& poses, uint64_t lifetime) {
        return std::any_of(poses.begin(), poses.end(), [lifetime](const PoseFrame& pose) {
            return pose.count && renderFrame - pose.frame < lifetime;
        });
    };
    const bool previousPoses = (res.chargeOrigin.count && renderFrame - res.chargeOrigin.frame < AnchorEndAge) ||
        hasPose(res.stompOrigins, StompHoldFrames + StompFadeFrames) ||
        hasPose(res.airOrigins, AirOriginFrames) || hasPose(res.airTrail, AirFadeFrames) ||
        std::any_of(res.trail.begin(), res.trail.end(),
            [nowUs](const PoseFrame& pose) { return TrailStrength(pose, nowUs) > 0; });
    res.capturePose = recording || stomp || effectFrame.leading || charge || airCombo.branch || previousPoses;
    res.captureFront = res.capturePose && effectFrame.body > 0;
}

bool InspectDraw(Context* c, const Draw& draw) {
    if (!hooksReady || drawingExtra) return false;
    const auto now = GetTickCount64();
    const bool capture = now < captureUntil;
    if (!capture && !settings.enabled) return false;
    ++activity.indexed;
    const auto found = contexts.find(c);
    if (found == contexts.end()) return false;
    auto& state = found->second;
    if (!state.index || !state.vertices[0].buffer || !state.vs || state.uavsBound || state.unknownVertexSlots ||
        draw.count < 3 || draw.count > 100000) return false;
    ++activity.stateReady;
    const auto key = MeshKey(state, draw);
    const auto* mesh = ObserveCostume(key);
    const bool selected = mesh != nullptr;
    if (selected) {
        ++activity.costume;
        if (state.viewportCount == 1) activity.viewport = state.viewport;
    }
    const bool gear = activeCostume != Costume::Unknown &&
        std::find(DragonGearMeshes.begin(), DragonGearMeshes.end(), key) != DragonGearMeshes.end();
    if (!capture && !selected && !gear) return false;
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
    if (selected && sceneColor) ++activity.sceneColor;
    if ((!active || (!selected && !gear) || !sceneColor) && !capture && body >= 1) return false;

    ComPtr<ID3D11Device> deviceRef;
    if (!state.device) {
        c->GetDevice(deviceRef.GetAddressOf());
        state.device = deviceRef.Get();
    }
    auto* device = state.device;
    if (!device) return false;
    auto& res = resources[device];
    struct ExtraDrawScope {
        ExtraDrawScope() { drawingExtra = true; }
        ~ExtraDrawScope() { drawingExtra = false; }
    } extra;
    if (capture && sceneColor) CaptureMask(c, state, device, res, draw, key);
    if (active && selected && sceneColor) {
        const bool recording = now < previewUntil || effectFrame.trailFrames || airCombo.descentCapture;
        const bool stomp = stompRenderFrame == renderFrame;
        if (res.frontDecisionFrame != renderFrame) UpdatePoseCapture(res, PoseClockUs(), recording, stomp);
        if (!res.capturePose && body >= 1) return false;
        if (res.capturePose) {
            if (!PrepareGhost(device, res)) return false;
            if (SceneDepthEnabled())
                RememberSceneDepth(device, res.sceneDepth, state.depthView, state.depthState, state.viewport);
            CapturePlayer(c, state, device, res, draw, key, PoseClockUs(), recording, stomp);
        }
    } else if (active && gear && sceneColor && body > 0 && res.playerFront.frame == renderFrame) {
        CapturePart(c, state, device, res.playerFront, draw, key);
    }
    if (body <= 0) return true;
    if (body >= 1) return false;
    if (!PrepareGhost(device, res)) return false;

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

template<class Next, class... Args>
void ForwardIndexed(Next next, Context* c, const Draw& draw, bool inspect, Args... args) {
    // D3D11 can forward to a second hooked entry with the original context pointer.
    // Dispatch by the entry we intercepted, since the context vtable can still name the first.
    if (forwardingIndexed || drawingExtra) {
        next(c, args...);
        return;
    }
    struct ForwardScope {
        ForwardScope() { forwardingIndexed = true; }
        ~ForwardScope() { forwardingIndexed = false; }
    } scope;
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    if (inspect) {
        try { if (InspectDraw(c, draw)) return; }
        catch (...) { Log("DRAW_INSPECTION_FAILED"); }
    }
    next(c, args...);
}

void ObserveIndexed(IndexedDraw next, Context* c, UINT count, UINT first, INT base) {
    ForwardIndexed(next, c, Draw{count, first, base}, true, count, first, base);
}

void ObserveIndexedInstanced(InstancedIndexedDraw next, Context* c, UINT count, UINT instances, UINT first, INT base, UINT instance) {
    ForwardIndexed(next, c, Draw{count, first, base, instance, true}, instances == 1,
        count, instances, first, base, instance);
}

HRESULT ObserveFinish(FinishList next, Context* c, BOOL restore, ID3D11CommandList** list) {
    if (drawingExtra) return next(c, restore, list);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    HRESULT result = next(c, restore, list);
    auto found = contexts.find(c);
    if (found != contexts.end() && SUCCEEDED(result) && list && *list) {
        if (!found->second.readbacks.empty()) {
            commandReadbacks[*list] = std::move(found->second.readbacks);
            found->second.readbacks.clear();
        }
        if (!restore) ResetContextState(found->second);
    }
    return result;
}

void ObserveExecute(ExecuteList next, Context* c, ID3D11CommandList* list, BOOL restore) {
    if (drawingExtra) return next(c, list, restore);
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    next(c, list, restore);
    if (!restore) {
        auto state = contexts.find(c);
        if (state != contexts.end()) ResetContextState(state->second);
    }
    auto found = commandReadbacks.find(list);
    if (found != commandReadbacks.end() && c->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        for (const auto& readback : found->second) SaveReadback(c, readback);
        commandReadbacks.erase(found);
    }
}

bool PrepareOverlay(ID3D11Device* device, DeviceResources& res, const D3D11_TEXTURE2D_DESC& coverageSurface) {
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
    desc.Width = coverageSurface.Width;
    desc.Height = coverageSurface.Height;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc = coverageSurface.SampleDesc;
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

bool PrepareRefraction(ID3D11Device* device, RefractionResources& res, const D3D11_TEXTURE2D_DESC& backBuffer,
    UINT maskWidth, UINT maskHeight, unsigned layers) {
    if (!res.fullscreen) {
        // Device object creation can fail partway through. Commit only when complete.
        RefractionResources prepared;
        if (!CreatePixelShader(device, AfterimageShaders::Composite, prepared.composite) ||
            !CreatePixelShader(device, AfterimageShaders::Part, prepared.partShader) ||
            !CreatePixelShader(device, AfterimageShaders::Glove, prepared.gloveShader) ||
            !CreatePixelShader(device, AfterimageShaders::Grip, prepared.gripShader) ||
            !CreatePixelShader(device, AfterimageShaders::PlayerCoverage, prepared.playerCoverage)) return false;
        D3D11_BUFFER_DESC labels{};
        labels.ByteWidth = 16;
        labels.Usage = D3D11_USAGE_IMMUTABLE;
        labels.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constexpr float MaterialLevels[][2]{{0.16f, 0.44f}, {0.12f, 0.34f}, {0.72f, 0.28f}};
        for (unsigned i = 0; i < prepared.partLabels.size(); ++i) {
            const auto& level = MaterialLevels[i / 8];
            const FLOAT id[4]{float(i % 8 + 1 + 16 * (i / 8)), level[0], level[1]};
            D3D11_SUBRESOURCE_DATA initial{id, 0, 0};
            if (FAILED(device->CreateBuffer(&labels, &initial, prepared.partLabels[i].GetAddressOf()))) return false;
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
        if (FAILED(device->CreateDepthStencilState(&depth, prepared.partDepth.GetAddressOf())) ||
            FAILED(device->CreateBuffer(&parameters, nullptr, prepared.parameters.GetAddressOf())) ||
            FAILED(device->CreateSamplerState(&sampler, prepared.linearSampler.GetAddressOf())) ||
            FAILED(device->CreateBlendState(&blend, prepared.rgbBlend.GetAddressOf())) ||
            FAILED(device->CreateVertexShader(AfterimageShaders::Fullscreen, sizeof(AfterimageShaders::Fullscreen), nullptr,
                prepared.fullscreen.GetAddressOf()))) return false;
        res = std::move(prepared);
        Log("REFRACTION_PIPELINE_READY");
    }
    const bool sameSize = res.scene && res.surface.Width == backBuffer.Width &&
        res.surface.Height == backBuffer.Height && res.surface.Format == backBuffer.Format;
    const DXGI_FORMAT maskFormat = settings.glassVisual ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8_UNORM;
    const bool sameMask = res.maskWidth == maskWidth && res.maskHeight == maskHeight && res.maskFormat == maskFormat;
    if (sameSize && sameMask && res.slices.size() >= layers) return true;
    auto desc = backBuffer;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> scene, mask;
    ComPtr<ID3D11ShaderResourceView> sceneView, maskView;
    if (!sameSize && (FAILED(device->CreateTexture2D(&desc, nullptr, scene.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, sceneView.GetAddressOf())))) return false;
    desc.Format = maskFormat;
    desc.Width = maskWidth;
    desc.Height = maskHeight;
    // Leave room for the normal five poses and the live player. Allocate once as the trail fills.
    desc.ArraySize = sameMask && res.slices.size() >= layers ? static_cast<UINT>(res.slices.size()) :
        std::max(layers, std::min(MaxGhostLayers + 1, settings.copies + 1));
    desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.ArraySize = desc.ArraySize;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, mask.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(mask.Get(), &srv, maskView.GetAddressOf()))) return false;
    std::vector<ComPtr<ID3D11RenderTargetView>> slices(desc.ArraySize);
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = desc.Format;
    rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv.Texture2DArray.ArraySize = 1;
    for (UINT i = 0; i < desc.ArraySize; ++i) {
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
    res.maskWidth = maskWidth;
    res.maskHeight = maskHeight;
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

struct OverlayBindings {
    const OverlayDraw* previous = nullptr;
    std::array<ID3D11Buffer*, ConstantSlots> constants{};
    std::array<ID3D11Buffer*, VertexSlots> vertices{};
    std::array<UINT, VertexSlots> strides{}, offsets{};
    unsigned changes = 0;

    void DrawPart(Context* c, const OverlayDraw& packet, ID3D11Buffer* camera) {
        if (!previous || previous->layout.Get() != packet.layout.Get()) {
            Original<SetLayout>(c, InputLayout)(c, packet.layout.Get());
            ++changes;
        }
        if (!previous || previous->topology != packet.topology) {
            Original<SetTopology>(c, Topology)(c, packet.topology);
            ++changes;
        }
        if (!previous || previous->index.Get() != packet.index.Get() || previous->indexFormat != packet.indexFormat ||
            previous->indexOffset != packet.indexOffset) {
            Original<SetIndex>(c, IndexBuffer)(c, packet.index.Get(), packet.indexFormat, packet.indexOffset);
            ++changes;
        }
        if (!previous || previous->shader.Get() != packet.shader.Get()) {
            Original<SetVS>(c, VsShader)(c, packet.shader.Get(), nullptr, 0);
            ++changes;
        }
        UINT first = ConstantSlots, last = 0;
        for (UINT i = 0; i < ConstantSlots; ++i) {
            auto* value = i == ViewProjectionSlot && camera ? camera : packet.pose.constants[i].Get();
            if (constants[i] != value) {
                constants[i] = value;
                first = std::min(first, i);
                last = i + 1;
            }
        }
        if (first < last) {
            Original<SetBuffers>(c, VsConstants)(c, first, last - first, constants.data() + first);
            ++changes;
        }
        first = VertexSlots;
        last = 0;
        for (UINT i = 0; i < VertexSlots; ++i) {
            auto* value = packet.pose.vertices[i].Get();
            if (vertices[i] == value && strides[i] == packet.strides[i] && offsets[i] == packet.offsets[i]) continue;
            vertices[i] = value;
            strides[i] = packet.strides[i];
            offsets[i] = packet.offsets[i];
            first = std::min(first, i);
            last = i + 1;
        }
        if (first < last) {
            Original<SetVertex>(c, VertexBuffers)(c, first, last - first, vertices.data() + first,
                strides.data() + first, offsets.data() + first);
            ++changes;
        }
        previous = &packet;
        CallDraw(c, packet.draw);
    }
};


void ReadOverlayCoverage(Context* immediate, OverlayCoverageProbe& probe) {
    if (!probe.pending) return;
    UINT64 geometry = 0, composite = 0;
    const auto mask = immediate->GetData(probe.geometry.Get(), &geometry, sizeof(geometry), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    const auto output = immediate->GetData(probe.composite.Get(), &composite, sizeof(composite), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (mask == S_FALSE || output == S_FALSE) return;
    double gpuMs = -1;
    if (probe.timed) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
        UINT64 start = 0, end = 0;
        const auto frequency = immediate->GetData(probe.frequency.Get(), &timing, sizeof(timing), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const auto begin = immediate->GetData(probe.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const auto finish = immediate->GetData(probe.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (frequency == S_FALSE || begin == S_FALSE || finish == S_FALSE) return;
        if (frequency == S_OK && begin == S_OK && finish == S_OK && !timing.Disjoint && timing.Frequency && end >= start)
            gpuMs = 1000.0 * (end - start) / timing.Frequency;
    }
    probe.pending = false;
    if (mask == S_OK && output == S_OK)
        Log("OVERLAY_COVERAGE frame=%llu draws=%u depth=%u geometry=%llu composite=%llu gpu_ms=%.3f",
            static_cast<unsigned long long>(probe.frame), probe.draws, unsigned(probe.sceneDepth),
            static_cast<unsigned long long>(geometry), static_cast<unsigned long long>(composite), gpuMs);
    else
        Log("OVERLAY_COVERAGE_UNAVAILABLE mask=%08lx output=%08lx", mask, output);
}

bool RenderOverlay(IDXGISwapChain* swap, ID3D11Device* device, DeviceResources& res) {
    const auto cpuStart = PoseClockUs();
    auto& poses = res.overlayPoses;
    activity.skippedGhostLayers += BudgetOverlayPoses(poses);
    const auto layerCount = static_cast<unsigned>(poses.size());
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))) ||
        FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, target.GetAddressOf()))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    ComPtr<Context> immediate;
    device->GetImmediateContext(immediate.GetAddressOf());
    if (!immediate) return false;

    struct OverlayScope {
        OverlayScope() { drawingExtra = true; }
        ~OverlayScope() { drawingExtra = false; }
    } extra;
    auto& refraction = res.refraction;
    const auto& sceneDepth = res.sceneDepth;
    const bool hasSceneDepth = SceneDepthEnabled() && sceneDepth.frame == renderFrame && sceneDepth.view;
    const bool playerFront = res.playerFront.frame == renderFrame && res.playerFront.count;
    bool useRefraction = desc.SampleDesc.Count == 1 && !refraction.unavailable && layerCount <= MaxGhostLayers;
    auto coverageSurface = desc;
    if (useRefraction && LimitGhostLayers()) {
        coverageSurface.Width = (desc.Width + 1) / 2;
        coverageSurface.Height = (desc.Height + 1) / 2;
    }
    if (useRefraction && !PrepareRefraction(device, refraction, desc, coverageSurface.Width,
        coverageSurface.Height, layerCount + unsigned(playerFront))) {
        refraction = {};
        refraction.unavailable = true;
        useRefraction = false;
        Log("REFRACTION_UNAVAILABLE");
    }
    // Keep a visible character untouched even if this backbuffer cannot build the front mask.
    if (playerFront && !useRefraction) return false;
    if (!useRefraction) coverageSurface = desc;
    if (!PrepareOverlay(device, res, coverageSurface)) return false;
    auto* c = res.overlayContext.Get();
    auto& probe = res.coverageProbe;
    const bool probeThisFrame = useRefraction && !probe.pending && probe.samples < 6 &&
        (!probe.frame || renderFrame - probe.frame >= 45);
    if (probeThisFrame) {
        ++probe.samples;
        probe.frame = renderFrame;
        if (!probe.geometry || !probe.composite) {
            const D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION, 0};
            if (FAILED(device->CreateQuery(&query, probe.geometry.ReleaseAndGetAddressOf())) ||
                FAILED(device->CreateQuery(&query, probe.composite.ReleaseAndGetAddressOf()))) probe.samples = 6;
            const D3D11_QUERY_DESC frequency{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            const D3D11_QUERY_DESC stamp{D3D11_QUERY_TIMESTAMP, 0};
            probe.timed = SUCCEEDED(device->CreateQuery(&frequency, probe.frequency.GetAddressOf())) &&
                SUCCEEDED(device->CreateQuery(&stamp, probe.start.GetAddressOf())) &&
                SUCCEEDED(device->CreateQuery(&stamp, probe.end.GetAddressOf()));
        }
    }
    const bool measure = probeThisFrame && probe.geometry && probe.composite;
    if (measure && probe.timed) {
        c->Begin(probe.frequency.Get());
        c->End(probe.start.Get());
    }
    if (hasSceneDepth && !sceneDepth.frozen) CopySceneDepth(c, sceneDepth);
    auto* rtv = target.Get();
    if (useRefraction) c->CopyResource(refraction.scene.Get(), backBuffer.Get());
    auto* stencil = res.overlayCoverage.Get();
    const D3D11_VIEWPORT viewport{0, 0, float(coverageSurface.Width), float(coverageSurface.Height),
        hasSceneDepth ? sceneDepth.viewport.MinDepth : 0, hasSceneDepth ? sceneDepth.viewport.MaxDepth : 1};
    if (!useRefraction) Original<SetTargets>(c, Targets)(c, 1, &rtv, stencil);
    Original<SetDepth>(c, Depth)(c, useRefraction ? refraction.partDepth.Get() : res.overlayStencil.Get(), 0);
    Original<SetViewport>(c, Viewports)(c, 1, &viewport);
    Original<SetPS>(c, PsShader)(c, useRefraction ? refraction.partShader.Get() : res.ghostShader.Get(), nullptr, 0);
    Original<SetBlend>(c, Blend)(c, useRefraction ? nullptr : res.ghostBlend.Get(), nullptr, UINT_MAX);
    auto* lighting = res.ghostLighting.Get();
    Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 1, 1, &lighting);
    const FLOAT depthParameters[]{float(sceneDepth.desc.Width) / coverageSurface.Width, float(sceneDepth.desc.Height) / coverageSurface.Height,
        hasSceneDepth ? (sceneDepth.reversed ? -1.0f : 1.0f) : 0.0f, 0.00005f};
    auto* depthConstants = res.sceneDepthConstants.Get();
    c->UpdateSubresource(depthConstants, 0, nullptr, depthParameters, 0, 0);
    Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 3, 1, &depthConstants);
    auto* depthTexture = hasSceneDepth ? sceneDepth.view.Get() : nullptr;
    c->PSSetShaderResources(2, 1, &depthTexture);
    if (useRefraction) {
        const FLOAT projection[4]{1.0f / coverageSurface.Height};
        c->UpdateSubresource(lighting, 0, nullptr, projection, 0, 0);
    }
    c->RSSetState(res.overlayRasterizer.Get());
    if (measure) c->Begin(probe.geometry.Get());
    std::array<float, MaxGhostLayers> opacities{};
    float lastOpacity = -1;
    ID3D11Buffer* lastColor = nullptr;
    unsigned lastPartLabel = UINT_MAX;
    auto* partShader = refraction.partShader.Get();
    unsigned layerIndex = 0, ghostDraws = 0;
    OverlayBindings bindings;
    for (const auto& queued : poses) {
        if (useRefraction) {
            // The nearest surface determines the anatomical label within this pose.
            auto* layer = refraction.slices[layerIndex].Get();
            const FLOAT clear[4]{};
            c->ClearRenderTargetView(layer, clear);
            c->ClearDepthStencilView(stencil, D3D11_CLEAR_DEPTH, 1, 0);
            Original<SetTargets>(c, Targets)(c, 1, &layer, stencil);
            opacities[layerIndex] = queued.opacity;
        } else {
            c->ClearDepthStencilView(stencil, D3D11_CLEAR_STENCIL, 1, 0);
        }
        ++layerIndex;
        if (!useRefraction && queued.opacity != lastOpacity) {
            const float glow = settings.glowStrength / 100.0f;
            const FLOAT constants[4]{queued.opacity,
                settings.glassVisual ? settings.brightness / 100.0f + glow : 1.0f + glow};
            c->UpdateSubresource(lighting, 0, nullptr, constants, 0, 0);
            lastOpacity = queued.opacity;
        }
        if (!useRefraction && lastColor != queued.color) {
            auto* color = lastColor = queued.color;
            Original<SetBuffers>(c, PsConstants)(c, ColorSlot, 1, &color);
        }
        for (unsigned i = 0; i < queued.pose->count; ++i) {
            const auto& packet = queued.pose->parts[i];
            if (useRefraction) {
                const bool mixed = packet.material == GhostMaterial::Glove || packet.material == GhostMaterial::GripAtlas;
                const auto material = mixed ? GhostMaterial::Armor : packet.material;
                const unsigned labelIndex = unsigned(material) * 8 + unsigned(packet.bodyPart) - 1;
                auto* nextShader = packet.material == GhostMaterial::GripAtlas ? refraction.gripShader.Get() :
                    packet.material == GhostMaterial::Glove ? refraction.gloveShader.Get() : refraction.partShader.Get();
                if (nextShader != partShader) {
                    Original<SetPS>(c, PsShader)(c, nextShader, nullptr, 0);
                    partShader = nextShader;
                }
                if (labelIndex != lastPartLabel) {
                    auto* label = refraction.partLabels[labelIndex].Get();
                    Original<SetBuffers>(c, PsConstants)(c, ColorSlot - 2, 1, &label);
                    lastPartLabel = labelIndex;
                }
            }
            bindings.DrawPart(c, packet, res.frameCamera.Get());
        }
        ghostDraws += queued.pose->count;
    }
    if (measure) c->End(probe.geometry.Get());
    if (playerFront) {
        Original<SetPS>(c, PsShader)(c, refraction.playerCoverage.Get(), nullptr, 0);
        auto* layer = refraction.slices[layerCount].Get();
        const FLOAT clear[4]{};
        c->ClearRenderTargetView(layer, clear);
        c->ClearDepthStencilView(stencil, D3D11_CLEAR_DEPTH, 1, 0);
        Original<SetTargets>(c, Targets)(c, 1, &layer, stencil);
        for (unsigned i = 0; i < res.playerFront.count; ++i)
            bindings.DrawPart(c, res.playerFront.parts[i], res.frameCamera.Get());
    }
    if (measure) c->Begin(probe.composite.Get());
    if (useRefraction) {
        const D3D11_VIEWPORT output{0, 0, float(desc.Width), float(desc.Height), 0, 1};
        Original<SetViewport>(c, Viewports)(c, 1, &output);
        CompositeRefraction(c, res, target.Get(), layerCount, opacities, playerFront);
    }
    if (measure) c->End(probe.composite.Get());
    if (measure && probe.timed) {
        c->End(probe.end.Get());
        c->End(probe.frequency.Get());
    }
    ComPtr<ID3D11CommandList> list;
    if (FAILED(Original<FinishList>(c, Finish)(c, FALSE, list.GetAddressOf())) || !list) return false;
    Original<ExecuteList>(immediate.Get(), Execute)(immediate.Get(), list.Get(), TRUE);
    const auto cpuUs = static_cast<unsigned long long>(PoseClockUs() - cpuStart);
    ++activity.overlayFrames;
    activity.overlayCpuUs += cpuUs;
    activity.peakOverlayCpuUs = std::max(activity.peakOverlayCpuUs, cpuUs);
    activity.peakGhostDraws = std::max<unsigned long long>(activity.peakGhostDraws, ghostDraws);
    const unsigned frontDraws = playerFront ? res.playerFront.count : 0;
    activity.peakFrontDraws = std::max<unsigned long long>(activity.peakFrontDraws, frontDraws);
    if (measure) {
        probe.draws = ghostDraws;
        probe.sceneDepth = hasSceneDepth;
        probe.pending = true;
    }
    if (loggedOverlays++ < 16)
        Log("OVERLAY_DRAW frame=%llu draws=%u poses=%u backbuffer=%ux%u mask=%ux%u state_binds=%u brightness=%u emission=%u refraction=%u segmented=%u player_front=%u style=%s scene_depth=%ux%u frozen=%u",
            static_cast<unsigned long long>(renderFrame), ghostDraws, layerCount, desc.Width,
            desc.Height, coverageSurface.Width, coverageSurface.Height, bindings.changes, settings.brightness, settings.glowStrength,
            useRefraction ? (settings.glassVisual ? settings.glassRefraction : settings.refractionPixels) : 0,
            unsigned(useRefraction), frontDraws,
            settings.glassVisual ? "Sigma2" : "Classic", hasSceneDepth ? sceneDepth.desc.Width : 0,
            hasSceneDepth ? sceneDepth.desc.Height : 0, hasSceneDepth && sceneDepth.frozen);
    return true;
}

HRESULT ObservePresent(Present next, IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
    // A windowed swap chain can keep presenting after another window gains focus.
    if (hooksReady && swap == gameSwap && !(flags & DXGI_PRESENT_TEST)) {
        const auto nowUs = PoseClockUs();
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        ++activity.presented;
        if (lastPresentUs) {
            auto& cadence = lastPresentHadOverlay ? activity.cadenceWithOverlay : activity.cadenceWithoutOverlay;
            cadence.Record(nowUs - lastPresentUs);
        }
        lastPresentUs = nowUs;
        lastPresentHadOverlay = false;
        for (auto& entry : resources) {
            auto& res = entry.second;
            try {
                if (res.coverageProbe.pending) {
                    ComPtr<Context> immediate;
                    entry.first->GetImmediateContext(immediate.GetAddressOf());
                    if (immediate) ReadOverlayCoverage(immediate.Get(), res.coverageProbe);
                }
                if (settings.enabled) BuildOverlayDraws(res, nowUs);
                if (res.overlayPoses.empty()) continue;
                if (RenderOverlay(swap, entry.first, res)) lastPresentHadOverlay = true;
                else if (loggedOverlays++ < 16) Log("OVERLAY_DRAW_FAILED");
            } catch (...) {
                Log("OVERLAY_DRAW_FAILED exception");
            }
            res.overlayPoses.clear();
        }
        ++renderFrame;
    }
    return next(swap, syncInterval, flags);
}

constexpr size_t ContextHookCount = 18;
constexpr size_t MaxContextTables = 16;
struct ContextHook {
    Slot index;
    std::array<void*, MaxContextTables> handlers;
};
std::array<RenderVtable::Slot, MaxContextTables * ContextHookCount> contextEntries{};
std::array<std::atomic<bool>, MaxContextTables * ContextHookCount> retiredContextEntries{};
std::array<std::atomic<void*>, MaxContextTables * ContextHookCount> contextOriginals{};
std::array<std::array<void*, 2>, MaxContextTables * ContextHookCount> verifiedContextTargets{};
std::array<RenderVtable::Slot, 1> presentEntries{};
std::array<RenderVtable::Slot, 1> deviceEntries{};
std::array<void**, MaxContextTables> contextTables{};
std::array<ComPtr<Context>, MaxContextTables> contextTableOwners;

template<size_t position, class Fn, auto Observe, size_t... rows>
auto ContextObservers(std::index_sequence<rows...>) {
    using Observer = RenderVtable::Observer<Fn, Observe>;
    return std::array<void*, MaxContextTables>{
        reinterpret_cast<void*>(Observer::template Call<contextEntries, rows * ContextHookCount + position>)...
    };
}

template<size_t position, class Fn, auto Observe>
ContextHook MakeContextHook(Slot index) {
    return {index, ContextObservers<position, Fn, Observe>(std::make_index_sequence<MaxContextTables>{})};
}

const ContextHook contextHooks[]{
    MakeContextHook<0, SetBuffers, TrackVSConstants>(VsConstants),
    MakeContextHook<1, SetVertex, TrackVertex>(VertexBuffers),
    MakeContextHook<2, SetIndex, TrackIndex>(IndexBuffer),
    MakeContextHook<3, SetLayout, TrackLayout>(InputLayout),
    MakeContextHook<4, SetTopology, TrackTopology>(Topology),
    MakeContextHook<5, SetPS, TrackPS>(PsShader),
    MakeContextHook<6, SetVS, TrackVS>(VsShader),
    MakeContextHook<7, SetTargets, TrackTargets>(Targets),
    MakeContextHook<8, SetTargetsUav, TrackTargetsUav>(TargetsAndUavs),
    MakeContextHook<9, SetBlend, TrackBlend>(Blend),
    MakeContextHook<10, SetDepth, TrackDepth>(Depth),
    MakeContextHook<11, SetViewport, TrackViewport>(Viewports),
    MakeContextHook<12, ClearDepth, ObserveClearDepth>(ClearDepthView),
    MakeContextHook<13, ClearContext, TrackClear>(Clear),
    MakeContextHook<14, FinishList, ObserveFinish>(Finish),
    MakeContextHook<15, ExecuteList, ObserveExecute>(Execute),
    MakeContextHook<16, IndexedDraw, ObserveIndexed>(Indexed),
    MakeContextHook<17, InstancedIndexedDraw, ObserveIndexedInstanced>(IndexedInstanced)
};
static_assert(std::size(contextHooks) == ContextHookCount);

bool HasSharedContextHook(Context* context, Slot slot) {
    return hooksReady && sharedRenderSlots.count(RenderVtable::Table(context) + slot);
}

const AfterimageDrawHandlers sharedDrawHandlers{
    [](IndexedDraw next, Context* c, UINT count, UINT first, INT base) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        if (!HasSharedContextHook(c, Indexed)) return false;
        ObserveIndexed(next, c, count, first, base);
        return true;
    },
    [](InstancedIndexedDraw next, Context* c, UINT count, UINT instances, UINT first, INT base, UINT instance) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        if (!HasSharedContextHook(c, IndexedInstanced)) return false;
        ObserveIndexedInstanced(next, c, count, instances, first, base, instance);
        return true;
    },
    [](ExecuteDrawList next, Context* c, ID3D11CommandList* list, BOOL restore) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        if (!HasSharedContextHook(c, Execute)) return false;
        ObserveExecute(next, c, list, restore);
        return true;
    }
};

bool RebindContextSlot(const RenderVtable::Slot& entry, std::array<void*, 2>* verified = nullptr) {
    if (!entry.address || entry.retired->load()) return false;
    void* current = *entry.address;
    if (!current || current == entry.observer) return false;
    const auto previous = entry.Next();
    if (current != previous && (!verified || std::find(verified->begin(), verified->end(), current) == verified->end())) {
        ++activity.rebindChecks;
        MEMORY_BASIC_INFORMATION oldCode{}, newCode{};
        if (!VirtualQuery(entry.original, &oldCode, sizeof(oldCode)) ||
            !VirtualQuery(current, &newCode, sizeof(newCode)) || newCode.Type != MEM_IMAGE ||
            oldCode.AllocationBase != newCode.AllocationBase ||
            !(newCode.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        if (verified) {
            (*verified)[1] = (*verified)[0];
            (*verified)[0] = current;
        }
    }
    if (current != previous) entry.currentOriginal->store(current);
    if (entry.Exchange(current, entry.observer)) return true;
    entry.currentOriginal->store(previous);
    return false;
}

bool AttachContext(Context* context, const char* source) {
    if (!context) return false;
    auto** table = RenderVtable::Table(context);
    const auto known = std::find(contextTables.begin(), contextTables.end(), table);
    if (known != contextTables.end()) {
        const size_t row = known - contextTables.begin();
        for (size_t i = 0; i < ContextHookCount; ++i) {
            const auto position = row * ContextHookCount + i;
            const auto& entry = contextEntries[position];
            if (RebindContextSlot(entry, &verifiedContextTargets[position])) {
                ++activity.rebound;
                activity.fastRebind += entry.writable;
            }
        }
        return true;
    }
    const auto free = std::find(contextTables.begin(), contextTables.end(), nullptr);
    if (free == contextTables.end()) return false;
    const size_t row = free - contextTables.begin();
    std::vector<const RenderVtable::Slot*> pending;
    for (size_t i = 0; i < ContextHookCount; ++i) {
        const auto& hook = contextHooks[i];
        void** address = table + hook.index;
        // Automatic Intel mode never intercepts the clear implicated by the crash.
        if (intelAdapter && !intelDepthHook && hook.index == ClearDepthView) continue;
        if (controllerHooks.original && controllerHooks.original(table, hook.index)) {
            if (hook.index == Indexed || hook.index == IndexedInstanced || hook.index == Execute)
                sharedRenderSlots.insert(address);
            continue;
        }
        const size_t position = row * ContextHookCount + i;
        verifiedContextTargets[position] = {};
        auto& entry = contextEntries[position];
        MEMORY_BASIC_INFORMATION page{};
        const bool writableTable = VirtualQuery(address, &page, sizeof(page)) &&
            page.Type == MEM_PRIVATE && page.Protect == PAGE_READWRITE;
        entry = {address, *address, hook.handlers[row], hook.index, Log, &retiredContextEntries[position],
            &contextOriginals[position], writableTable};
        entry.retired->store(false);
        entry.currentOriginal->store(entry.original);
        originals.emplace(address, &entry);
        pending.push_back(&entry);
    }
    size_t installed = 0;
    for (const auto* entry : pending) {
        if (!entry->Exchange(entry->original, entry->observer)) {
            Log("RENDER_VTABLE_INSTALL_FAILED slot=%u context=%zu", entry->index, row);
            while (installed) pending[--installed]->Restore();
            for (const auto& candidate : contextHooks) {
                originals.erase(table + candidate.index);
                sharedRenderSlots.erase(table + candidate.index);
            }
            return false;
        }
        ++installed;
    }
    contextTables[row] = table;
    contextTableOwners[row] = context;
    Log("CONTEXT_VTABLE_READY source=%s type=%u table=%p context=%zu", source, unsigned(context->GetType()),
        static_cast<void*>(table), row);
    return true;
}

bool ObservePrepareGameDraw(void* renderer, int topology) {
    // The game applies its dirty pipeline state here, immediately before submitting a draw.
    if (hooksReady) {
        auto* context = *reinterpret_cast<Context* const*>(static_cast<const BYTE*>(renderer) + GameContextOffset);
        {
            std::lock_guard<std::recursive_mutex> guard(renderMutex);
            ++activity.prepared;
            AttachContext(context, "game-draw");
        }
    }
    return originalPrepareGameDraw(renderer, topology);
}

// Some runtime implementations allocate a different dispatch table for each deferred context.
// A context created by the patch would therefore be the wrong table to observe.
HRESULT ObserveCreateDeferred(CreateDeferred next, ID3D11Device* device, UINT flags, Context** context) {
    const HRESULT result = next(device, flags, context);
    if (SUCCEEDED(result) && context && *context && hooksReady && !drawingExtra) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        AttachContext(*context, "device-create");
    }
    return result;
}

bool BindRenderHooks(ID3D11Device* device, Context* immediate, IDXGISwapChain* swap) {
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf()))) &&
        SUCCEEDED(dxgi->GetAdapter(adapter.GetAddressOf())) && SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
        intelAdapter = adapterDesc.VendorId == 0x8086;
        intelDepthHook = intelAdapter && settings.sceneDepthMode == 2;
        graphicsAdapter = GraphicsAdapterDetection::QueryDxCore(adapterDesc.AdapterLuid);
        const bool dxcore = graphicsAdapter != GraphicsAdapter::Unknown;
        if (!dxcore) graphicsAdapter = GraphicsAdapterDetection::ClassifyLegacy(adapterDesc.VendorId,
            adapterDesc.DedicatedVideoMemory, adapterDesc.SharedSystemMemory);
        Log("RENDER_ADAPTER vendor=%04x device=%04x revision=%u feature_level=%x class=%s detection=%s dedicated_mib=%llu name=%ls",
            adapterDesc.VendorId, adapterDesc.DeviceId, adapterDesc.Revision,
            unsigned(device->GetFeatureLevel()), GraphicsAdapterDetection::Name(graphicsAdapter), dxcore ? "DXCore" : "DXGI",
            static_cast<unsigned long long>(adapterDesc.DedicatedVideoMemory / (1024 * 1024)), adapterDesc.Description);
    } else {
        Log("RENDER_ADAPTER_UNKNOWN");
    }
    LogRenderProfile();
    auto* present = RenderVtable::Table(swap) + 8;
    presentEntries[0] = {present, *present,
        reinterpret_cast<void*>(RenderVtable::Observer<Present, ObservePresent>::Call<presentEntries, 0>), 8, Log};
    auto* create = RenderVtable::Table(device) + 27;
    deviceEntries[0] = {create, *create,
        reinterpret_cast<void*>(RenderVtable::Observer<CreateDeferred, ObserveCreateDeferred>::Call<deviceEntries, 0>), 27, Log};

    if (!AttachContext(immediate, "game-frame")) return false;
    if (!presentEntries[0].Exchange(presentEntries[0].original, presentEntries[0].observer) ||
        !deviceEntries[0].Exchange(deviceEntries[0].original, deviceEntries[0].observer)) {
        presentEntries[0].Restore();
        for (const auto& entry : contextEntries)
            if (entry.address) entry.Restore();
        return false;
    }
    gameSwap = swap;
    hooksReady = true;
    if (!sharedRenderSlots.empty() && controllerHooks.connect) controllerHooks.connect(&sharedDrawHandlers);
    Log("RENDERER_READY mode=vtable slots=%u shared=%u depth_clear=%u present=%p", unsigned(originals.size()),
        unsigned(sharedRenderSlots.size()), unsigned(SceneDepthEnabled()), presentEntries[0].original);
    return true;
}

void ObserveGameFrame(void* renderer, void* options) {
    // The verified game's frame routine owns the actual device and swap chain.
    // Creating a second swap chain through a proxy can replace its game window.
    const auto* data = static_cast<const BYTE*>(renderer);
    auto* swap = *reinterpret_cast<IDXGISwapChain* const*>(data + GameSwapOffset);
    auto* immediate = *reinterpret_cast<Context* const*>(data + GameContextOffset);
    if (swap && immediate) {
        std::lock_guard<std::recursive_mutex> guard(renderMutex);
        static bool attempted = false;
        if (!attempted) {
            auto* device = *reinterpret_cast<ID3D11Device* const*>(data + GameDeviceOffset);
            if (device) {
                attempted = true;
                if (!BindRenderHooks(device, immediate, swap)) Log("INITIALIZATION_FAILED");
            }
        }
        if (hooksReady) {
            // The runtime can replace an immediate context's dispatch table after creation.
            AttachContext(immediate, "game-frame");
            // The game's worker records the scene on a context created before auto-injection.
            if (gameWorkerLayoutVerified) {
                const auto* worker = *reinterpret_cast<const BYTE* const*>(data + RendererWorkerOffset);
                if (worker)
                    AttachContext(*reinterpret_cast<Context* const*>(worker + GameContextOffset), "game-worker");
            }
            gameSwap = swap;
            if (gameBase && settings.enabled && activeCostume != Costume::Unknown) UpdateAction();
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
    const auto* workerSetup = reinterpret_cast<const BYTE*>(gameBase + 0x27a1c);
    gameWorkerLayoutVerified = GameCompatibility::Sha256(workerSetup, 0x9b, hash) &&
        strcmp(hash, "2FB71D01ED3C778EF6FB8F0B5053B3C63D9644ABAFBAE3B8C82CC7519CB9E504") == 0;
    if (!gameWorkerLayoutVerified) Log("GAME_WORKER_LAYOUT_UNVERIFIED");
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
    constexpr uintptr_t PrepareDrawRoutine = 0x4b010;
    constexpr size_t PrepareDrawRoutineSize = 0x60;
    auto* prepareDraw = reinterpret_cast<BYTE*>(gameBase + PrepareDrawRoutine);
    if (GameCompatibility::Sha256(prepareDraw, PrepareDrawRoutineSize, hash) &&
        strcmp(hash, "DCA8EC21B05326EA087439A60CFEDD7BE511406AE46AF5C8D4B060584F2D00F9") == 0 &&
        MH_CreateHook(prepareDraw, reinterpret_cast<void*>(ObservePrepareGameDraw), &next) == MH_OK) {
        originalPrepareGameDraw = reinterpret_cast<PrepareGameDraw>(next);
        if (MH_EnableHook(prepareDraw) == MH_OK) {
            Log("GAME_DRAW_HOOK_READY rva=%zx", size_t(PrepareDrawRoutine));
            return true;
        }
        MH_RemoveHook(prepareDraw);
    }
    Log("GAME_DRAW_HOOK_UNAVAILABLE");
    return true;
}

unsigned ReadNumber(const wchar_t* name, unsigned fallback, unsigned low, unsigned high) {
    return std::clamp(GetPrivateProfileIntW(L"Afterimage", name, fallback, ini.c_str()), low, high);
}

void ReadSettings() {
    Settings updated;
    std::array<Appearance, std::size(Costumes)> loaded;
    wchar_t material[24]{};
    GetPrivateProfileStringW(L"Afterimage", L"Material", L"Classic", material, std::size(material), ini.c_str());
    updated.glassVisual = _wcsicmp(material, L"Sigma2") == 0;
    updated.enabled = ReadNumber(L"Enabled", 1, 0, 1) != 0;
    updated.autoET = ReadNumber(L"AutomaticDragonET", 1, 0, 1) != 0;
    updated.autoUT = ReadNumber(L"AutomaticDragonUT", 1, 0, 1) != 0;
    updated.autoWindRun = ReadNumber(L"AutomaticWindRun", 1, 0, 1) != 0;
    updated.autoWallDownslash = ReadNumber(L"AutomaticWallDownslash", 1, 0, 1) != 0;
    updated.autoSwallow = ReadNumber(L"AutomaticFlyingSwallow", 1, 0, 1) != 0;
    updated.autoWhiteFeather = ReadNumber(L"AutomaticWhiteFeather", 1, 0, 1) != 0;
    updated.sceneDepthMode = ReadNumber(L"SceneDepthMode", 0, 0, 2);
    wchar_t profile[24]{};
    GetPrivateProfileStringW(L"Afterimage", L"RenderProfile", L"", profile, std::size(profile), ini.c_str());
    if (_wcsicmp(profile, L"Standard") == 0) updated.renderProfile = RenderProfile::Standard;
    else if (_wcsicmp(profile, L"Integrated") == 0) updated.renderProfile = RenderProfile::Integrated;
    else if (!profile[0]) {
        const auto legacy = ReadNumber(L"LayerBudgetMode", 0, 0, 2);
        if (legacy) updated.renderProfile = legacy == 1 ? RenderProfile::Integrated : RenderProfile::Standard;
    }
    updated.interval = ReadNumber(L"IntervalMs", 12, 6, 150);
    updated.lifetime = ReadNumber(L"LifetimeMs", 185, 65, 650);
    updated.copies = ReadNumber(L"Copies", 5, 1, 5);
    updated.previewMs = ReadNumber(L"PreviewMs", 8000, 500, 10000);
    updated.revealFrames = ReadNumber(L"RevealFrames", 9, 1, 45);
    updated.utRevealFrames = ReadNumber(L"UTRevealFrames", 12, 1, 45);
    updated.refractionPixels = ReadNumber(L"RefractionPixels", 1, 0, 8);
    updated.glassRefraction = ReadNumber(L"GlassRefractionPixels", 19, 0, 36);
    updated.jointOpacity = ReadNumber(L"JointOpacityPercent", 76, 0, 100);
    for (unsigned i = 0; i < loaded.size(); ++i) {
        const auto costume = static_cast<Costume>(i);
        const bool exposedSkin = costume == Costume::Black || costume == Costume::Blue || costume == Costume::Grip;
        auto load = [&](const wchar_t* section) -> Appearance {
            auto read = [&](const wchar_t* key, unsigned fallback, unsigned maximum) {
                const auto value = static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.c_str()));
                return static_cast<unsigned>(std::clamp(value, 0, static_cast<int>(maximum)));
            };
            return {read(L"OpacityPercent", 40, 100),
                read(L"GlowStrengthPercent", 68, MaxGlowStrength),
                {read(L"TintR", 144, 255), read(L"TintG", 163, 255), read(L"TintB", 253, 255)},
                exposedSkin ? read(L"MuscleHighlightPercent", 45, 100) : 0,
                read(L"SurfaceBrightnessPercent", 100, MaxSurfaceBrightness)};
        };
        if (updated.glassVisual) {
            const auto section = std::wstring(L"Afterimage.Sigma2") + wcschr(Costumes[i].section, L'.');
            loaded[i] = load(section.c_str());
        } else {
            loaded[i] = load(Costumes[i].section);
        }
    }
    const unsigned serial = GetPrivateProfileIntW(L"Diagnostics", L"Capture", 0, ini.c_str());
    std::lock_guard<std::recursive_mutex> guard(renderMutex);
    const auto previousSettings = settings;
    const Appearance previous{settings.alpha, settings.glowStrength, settings.tint, settings.muscleHighlight,
        settings.brightness};
    settings = updated;
    appearances = loaded;
    ApplyAppearance();
    if (previousSettings.sceneDepthMode != settings.sceneDepthMode)
        Log("SCENE_DEPTH_MODE config=%u enabled=%u", settings.sceneDepthMode, unsigned(SceneDepthEnabled()));
    if (previousSettings.renderProfile != settings.renderProfile) LogRenderProfile();
    if (previousSettings.glassVisual != settings.glassVisual)
        Log("MATERIAL_RELOAD %s", settings.glassVisual ? "Sigma2" : "Classic");
    if (activeCostume != Costume::Unknown && (previous.alpha != settings.alpha ||
        previous.glow != settings.glowStrength || previous.tint != settings.tint ||
        previous.muscleHighlight != settings.muscleHighlight || previous.brightness != settings.brightness)) {
        Log("APPEARANCE_RELOAD costume=%u alpha=%u glow=%u rgb=%u,%u,%u muscle=%u brightness=%u",
            unsigned(activeCostume), settings.alpha, settings.glowStrength, settings.tint[0], settings.tint[1],
            settings.tint[2], settings.muscleHighlight, settings.brightness);
    }
    if (previousSettings.enabled != settings.enabled) {
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
    unsigned lastRenderReport = GetTickCount();
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
        if (now - lastRenderReport >= 8000) {
            lastRenderReport = now;
            std::lock_guard<std::recursive_mutex> guard(renderMutex);
            if (hooksReady && settings.enabled) {
                Log("RENDER_ACTIVITY present=%llu indexed=%llu state_ready=%llu costume=%llu scene_color=%llu viewport=%.0fx%.0f prepared=%llu rebound=%llu fast_rebind=%llu rebind_checks=%llu",
                    activity.presented, activity.indexed, activity.stateReady, activity.costume, activity.sceneColor,
                    activity.viewport.Width, activity.viewport.Height, activity.prepared, activity.rebound, activity.fastRebind,
                    activity.rebindChecks);
                const auto& with = activity.cadenceWithOverlay;
                const auto& without = activity.cadenceWithoutOverlay;
                Log("FRAME_PACING with_overlay=%llu over_18ms=%llu over_25ms=%llu peak_us=%llu without_overlay=%llu over_18ms=%llu over_25ms=%llu peak_us=%llu",
                    with.frames, with.over18Ms, with.over25Ms, with.peakUs,
                    without.frames, without.over18Ms, without.over25Ms, without.peakUs);
                if (activity.overlayFrames)
                    Log("OVERLAY_CPU frames=%llu average_us=%llu peak_us=%llu peak_ghost_draws=%llu peak_front_draws=%llu skipped_layers=%llu",
                        activity.overlayFrames, activity.overlayCpuUs / activity.overlayFrames, activity.peakOverlayCpuUs,
                        activity.peakGhostDraws, activity.peakFrontDraws, activity.skippedGhostLayers);
                activity = {};
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
    Log("GAME pid=%lu build=%s afterimage=0.6.22 shader_bytecode=embedded", GetCurrentProcessId(), GameCompatibility::Name(kind));
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
