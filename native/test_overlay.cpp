#include "afterimage.cpp"
#include <cassert>
#include <d3d11_1.h>

// Match the state at presentation: opaque scene props have already filled the target.
int main() {
    logFile = stdout;
    settings.glassVisual = false;
    constexpr UINT Size = 128;
    const HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, Size, Size, nullptr, nullptr, nullptr, nullptr);
    assert(window);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = desc.BufferDesc.Height = Size;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Device> device;
    ComPtr<Context> immediate;
    assert(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, swap.GetAddressOf(), device.GetAddressOf(), nullptr, immediate.GetAddressOf())));
    ComPtr<ID3D11Texture2D> surface;
    assert(SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()))));
    ComPtr<ID3D11RenderTargetView> propTarget;
    assert(SUCCEEDED(device->CreateRenderTargetView(surface.Get(), nullptr, propTarget.GetAddressOf())));
    const FLOAT propColor[]{0, 1, 0, 0.33f};
    // Attach after the first rendered frame, as the launcher does for a running game.
    immediate->ClearRenderTargetView(propTarget.Get(), propColor);
    swap->Present(0, 0);

    assert(MH_Initialize() == MH_OK);
    alignas(void*) std::array<BYTE, 0x3088> renderer{};
    auto* liveSwap = swap.Get();
    auto* liveDevice = device.Get();
    auto* liveContext = immediate.Get();
    memcpy(renderer.data() + 0x3080, &liveSwap, sizeof(liveSwap));
    memcpy(renderer.data() + 0x3078, &liveDevice, sizeof(liveDevice));
    memcpy(renderer.data() + 0x10, &liveContext, sizeof(liveContext));
    originalGameFrame = [](void*, void*) {};
    ObserveGameFrame(renderer.data(), nullptr);
    assert(hooksReady && gameSwap == liveSwap);

    auto& res = resources[device.Get()];
    assert(PrepareGhost(device.Get(), res));
    const char* vertexShader = R"(
        float4 main(float2 p:POSITION, out float2 uv:TEXCOORD0):SV_Position {
            uv = float2(0.35 + 0.65 * p.x, 0.35);
            return float4(p,0.5,1);
        })";
    ComPtr<ID3DBlob> code;
    assert(SUCCEEDED(D3DCompile(vertexShader, strlen(vertexShader), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, code.GetAddressOf(), nullptr)));
    OverlayDraw packet;
    assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, packet.shader.GetAddressOf())));
    const auto defaultVertex = packet.shader;
    D3D11_INPUT_ELEMENT_DESC input{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    assert(SUCCEEDED(device->CreateInputLayout(&input, 1, code->GetBufferPointer(), code->GetBufferSize(), packet.layout.GetAddressOf())));
    const char* gripVertexShader = R"(
        float4 main(float2 p:POSITION, out float2 uv:TEXCOORD0):SV_Position {
            uv = float2(0.35 + 0.65 * p.x, 0.53);
            return float4(p,0.5,1);
        })";
    assert(SUCCEEDED(D3DCompile(gripVertexShader, strlen(gripVertexShader), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, code.ReleaseAndGetAddressOf(), nullptr)));
    ComPtr<ID3D11VertexShader> gripVertex;
    assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, gripVertex.GetAddressOf())));
    const FLOAT positions[]{-0.8f, -0.8f, 0, 0.8f, 0.8f, -0.8f};
    const UINT16 indices[]{0, 1, 2};
    D3D11_BUFFER_DESC buffer{sizeof(positions), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA initial{positions, 0, 0};
    assert(SUCCEEDED(device->CreateBuffer(&buffer, &initial, packet.pose.vertices[0].GetAddressOf())));
    buffer.ByteWidth = sizeof(indices);
    buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initial.pSysMem = indices;
    assert(SUCCEEDED(device->CreateBuffer(&buffer, &initial, packet.index.GetAddressOf())));
    packet.draw.count = 3;
    packet.strides[0] = 2 * sizeof(FLOAT);
    packet.indexFormat = DXGI_FORMAT_R16_UINT;
    packet.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    packet.color = res.colors.back();
    ComPtr<ID3D11Buffer> left, right;
    const FLOAT leftPositions[]{-0.8f, -0.8f, 0, 0.8f, 0, -0.8f};
    const FLOAT rightPositions[]{0, -0.8f, 0, 0.8f, 0.8f, -0.8f};
    buffer.ByteWidth = sizeof(leftPositions);
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    initial.pSysMem = leftPositions;
    assert(SUCCEEDED(device->CreateBuffer(&buffer, &initial, left.GetAddressOf())));
    initial.pSysMem = rightPositions;
    assert(SUCCEEDED(device->CreateBuffer(&buffer, &initial, right.GetAddressOf())));
    D3D11_TEXTURE2D_DESC image{};
    surface->GetDesc(&image);
    image.BindFlags = 0;
    image.Usage = D3D11_USAGE_STAGING;
    image.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    assert(SUCCEEDED(device->CreateTexture2D(&image, nullptr, readback.GetAddressOf())));
    ComPtr<ID3D11DeviceContext1> context1;
    assert(SUCCEEDED(immediate.As(&context1)));
    ComPtr<ID3D11VertexShader> curvedShader;
    ComPtr<ID3D11DepthStencilView> obstacle;
    bool clearObstacleBeforePresent = false;
    auto draw = [&](unsigned strength, float anchorOpacity = 0, unsigned fadeStep = FadeSteps - 1,
        unsigned distortion = 0, bool seam = false, bool playerFront = false,
        GhostMaterial material = GhostMaterial::Fabric) {
        settings.glowStrength = strength;
        settings.refractionPixels = distortion;
        immediate->ClearRenderTargetView(propTarget.Get(), propColor);
        if (distortion) {
            const FLOAT stripe[]{1, 0, 0, propColor[3]};
            const D3D11_RECT band = playerFront ? D3D11_RECT{64, 35, 80, 101} : D3D11_RECT{56, 35, 64, 101};
            context1->ClearView(propTarget.Get(), stripe, &band, 1);
        }
        auto* rtv = propTarget.Get();
        immediate->OMSetRenderTargets(1, &rtv, nullptr);
        if (obstacle) {
            const D3D11_VIEWPORT viewport{0, 0, float(Size), float(Size), 0, 1};
            RememberSceneDepth(device.Get(), res.sceneDepth, obstacle.Get(), nullptr, viewport);
            if (clearObstacleBeforePresent)
                immediate->ClearDepthStencilView(obstacle.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        }
        packet.pose.renderFrame = renderFrame;
        packet.anchorOpacity = anchorOpacity;
        packet.color = res.colors[fadeStep];
        packet.opacity = anchorOpacity > 0 ? anchorOpacity : 0.48f * (fadeStep + 1) / FadeSteps;
        packet.material = material;
        packet.shader = settings.glassVisual ? curvedShader :
            (material == GhostMaterial::GripAtlas ? gripVertex : defaultVertex);
        res.overlayDraws = {packet, packet};
        if (seam) {
            res.overlayDraws[0].pose.vertices[0] = left;
            res.overlayDraws[0].bodyPart = BodyPart::Head;
            res.overlayDraws[1].pose.vertices[0] = right;
            res.overlayDraws[1].bodyPart = BodyPart::Torso;
        }
        if (playerFront) {
            res.playerFront.Begin(renderFrame, PoseClockUs());
            res.playerFront.parts = {packet};
            res.playerFront.parts[0].pose.vertices[0] = right;
            res.playerFront.count = 1;
        }
        assert(RenderOverlay(swap.Get(), device.Get(), res));
        res.overlayDraws.clear();
        ++renderFrame;
        ComPtr<ID3D11RenderTargetView> restored;
        immediate->OMGetRenderTargets(1, restored.GetAddressOf(), nullptr);
        assert(restored.Get() == propTarget.Get());
        immediate->CopyResource(readback.Get(), surface.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        assert(SUCCEEDED(immediate->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        const auto* pixels = static_cast<const BYTE*>(mapped.pData);
        std::array<std::array<BYTE, 4>, 8> samples{};
        const UINT rows[]{64, 115, 117, 125};
        for (unsigned i = 0; i < 4; ++i)
            std::copy_n(pixels + rows[i] * mapped.RowPitch + 64 * 4, 4, samples[i].begin());
        std::copy_n(pixels + 64 * mapped.RowPitch + 84 * 4, 4, samples[4].begin());
        std::copy_n(pixels + 114 * mapped.RowPitch + 64 * 4, 4, samples[5].begin());
        std::copy_n(pixels + 64 * mapped.RowPitch + 55 * 4, 4, samples[6].begin());
        std::copy_n(pixels + 64 * mapped.RowPitch + 63 * 4, 4, samples[7].begin());
        if (distortion && !playerFront && !settings.glassVisual) {
            // Black tint makes any red outside the stripe attributable to displaced scene sampling.
            bool displaced = false;
            for (unsigned y = 55; y < 88; ++y) {
                const auto* row = pixels + y * mapped.RowPitch;
                displaced |= row[55 * 4] > 5 || row[64 * 4] > 5;
            }
            assert(displaced && res.refraction.scene && !res.refraction.unavailable);
        }
        if (seam) {
            const auto* row = pixels + 64 * mapped.RowPitch;
            const auto inner = row[56 * 4];
            const auto joint = row[63 * 4];
            assert(inner >= 30 && inner <= 35 && std::abs(float(joint) / inner - 0.76f) < 0.06f &&
                row[64 * 4] == joint);
            printf("Anatomical opacity: part interior red %u, joint red %u.\n", inner, joint);
        }
        if (playerFront && distortion && !settings.glassVisual) {
            // The refracted sample cannot pull pixels from the live silhouette.
            for (unsigned y = 55; y < 88; ++y) {
                const auto* row = pixels + y * mapped.RowPitch;
                assert(row[63 * 4] == 0 && row[64 * 4] == 255);
            }
            puts("Refraction did not sample the front character into the background.");
        } else if (playerFront && !settings.glassVisual) {
            const auto* row = pixels + 64 * mapped.RowPitch;
            const std::array<BYTE, 4> liveColor{0, 255, 0, 84};
            assert(std::equal(liveColor.begin(), liveColor.end(), row + 64 * 4));
            assert(row[56 * 4] >= 30 && row[56 * 4] <= 35);
            puts("Live player pixel preserved, uncovered prop still receives the ghost.");
        }
        immediate->Unmap(readback.Get(), 0);
        return samples;
    };
    const auto baseline = draw(0);
    const auto glowing = draw(68);
    const auto fading = draw(68, 0, FadeSteps / 2 - 1);
    const auto anchor = draw(68, 0.20f);
    const auto skin = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::Skin);
    const auto armor = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::Armor);
    const auto glove = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::Glove);
    const auto grip = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::GripAtlas);
    const uint64_t curseDraws[]{0xe3b96406dab591c5ull, 0x47662b39ee55c832ull,
        0xa46b0ab6852a32e4ull, 0x71fda10252d65b7bull, 0xb920ed7be23812a5ull};
    for (const auto key : curseDraws) {
        const auto* part = FindCostumeMesh(Costume::Grip, key);
        assert(part && part->material == GhostMaterial::Skin);
    }
    const auto curse = draw(68, 0, FadeSteps - 1, 0, false, false,
        FindCostumeMesh(Costume::Grip, curseDraws[0])->material);
    settings.muscleHighlight = 100;
    const auto fullSkin = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::Skin);
    const auto fullArmor = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::Armor);
    settings.muscleHighlight = 0;
    const auto unlitSkin = draw(68, 0, FadeSteps - 1, 0, false, false, GhostMaterial::Skin);
    settings.muscleHighlight = 45;
    const auto restored = draw(0);
    draw(0, 0, FadeSteps - 1, 0, true);
    draw(0, 0, FadeSteps - 1, 0, false, true);
    assert(baseline == restored);
    const std::array<BYTE, 4> prop{0, 255, 0, 84};
    for (const auto* samples : {&baseline, &glowing, &fading, &anchor})
        assert((*samples)[0][3] == prop[3] && (*samples)[1] == prop && (*samples)[2] == prop && (*samples)[3] == prop);
    assert(baseline[0][0] >= 30 && baseline[0][0] <= 35 && baseline[0][1] > 205);
    assert(glowing[0][2] > baseline[0][2] + 28 && glowing[0][2] < 142);
    assert(fullSkin[0][0] > skin[0][0] && skin[0][0] > unlitSkin[0][0]);
    assert(fullSkin[0][2] > skin[0][2] && skin[0][2] > unlitSkin[0][2]);
    assert(armor == fullArmor);
    assert(glowing[5][0] > 0 && glowing[5][0] < glowing[0][0]);
    assert(armor[0][0] < glowing[0][0] && armor[0][2] < glowing[0][2]);
    assert(glove[0] == skin[0] && glove[4] == armor[4] && glove[4][0] < glove[0][0]);
    assert(grip[0] == skin[0] && grip[4] == armor[4] && grip[4][0] < grip[0][0]);
    assert(curse[0] == grip[0] && curse[4] == skin[4]);
    assert(ClassifyGhostMaterial(0xd453dc07e6a77402ull) == GhostMaterial::Armor);
    assert(ClassifyGhostMaterial(0x663687d9e35d6010ull) == GhostMaterial::Armor);
    assert(ClassifyGhostMaterial(0x4dc0ca0f283250d1ull) == GhostMaterial::Skin);
    assert(ClassifyGhostMaterial(0x1714166d02abe3baull) == GhostMaterial::Glove);
    assert(std::abs(int(fading[0][2]) - glowing[0][2] * 0.5f) <= 1);
    assert(std::abs(int(anchor[0][2]) - glowing[0][2] * (0.20f / 0.48f)) <= 1);
    printf("Material emission: fabric RGB %u/%u/%u, skin RGB %u/%u/%u. Background and fading verified.\n",
        glowing[0][0], glowing[0][1], glowing[0][2], skin[0][0], skin[0][1], skin[0][2]);
    printf("Glove atlas: skin red %u, cloth red %u. Armor red %u.\n", glove[0][0], glove[4][0], armor[0][0]);
    printf("Grip atlas: skin red %u, leather red %u.\n", grip[0][0], grip[4][0]);
    puts("Grip's right-arm curse uses the same highlight RGB as its left-arm skin.");
    puts("Skin highlight varies independently from armor; silhouette edge coverage tapers.");
    settings.tint = {0, 0, 0};
    assert(PrepareGhost(device.Get(), res));
    const auto refracted = draw(0, 0, FadeSteps - 1, 3);
    assert(refracted[3] == prop);
    draw(0, 0, FadeSteps - 1, 3, false, true);

    const auto now = PoseClockUs();
    renderFrame = 20;
    res.cameraFrame = renderFrame;
    for (unsigned i = 0; i < 7; ++i) {
        auto& pose = res.trail[i];
        pose.Begin(10 + i, now - 80000 + i * 10000);
        pose.parts = {packet, packet};
        pose.parts[0].pose.renderFrame = pose.parts[1].pose.renderFrame = pose.frame;
        pose.count = 2;
    }
    res.nextTrail = 7;
    auto& stomp = res.stompOrigins.front();
    stomp.Begin(2, now - 300000);
    stomp.parts = {packet};
    stomp.count = 1;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.size() == 11);
    assert(std::abs(res.overlayDraws.front().anchorOpacity - settings.alpha / 200.0f) < 0.001f);
    for (unsigned i = 1; i < res.overlayDraws.size(); i += 2) {
        assert(res.overlayDraws[i].pose.renderFrame == res.overlayDraws[i + 1].pose.renderFrame);
        if (i > 1) assert(res.overlayDraws[i - 1].pose.renderFrame < res.overlayDraws[i].pose.renderFrame);
    }
    for (unsigned age = 0; age < 10; ++age) assert(StompAlpha(age) == 1);
    for (unsigned age = 10; age < 28; ++age)
        assert(std::abs(StompAlpha(age) - float(27 - age) / 18.0f) < 0.001f);
    assert(StompAlpha(28) == 0);
    res.overlayDraws.clear();
    auto& laterStomp = res.stompOrigins[1];
    laterStomp.Begin(renderFrame, now);
    laterStomp.parts = {packet};
    laterStomp.count = 1;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.size() == 12 && res.overlayDraws[1].anchorOpacity == settings.alpha / 100.0f);
    ResetPoses();
    auto& release = res.chargeOrigin;
    release.Begin(20, now);
    release.parts = {packet};
    release.count = 1;
    auto& fadingTrail = res.trail[0];
    fadingTrail.Begin(64, now);
    fadingTrail.parts = {packet};
    fadingTrail.count = 1;
    fadingTrail.fadeFrames = 9;
    res.nextTrail = 1;
    res.leadingPose.Begin(72, now);
    res.leadingPose.parts = {packet};
    res.leadingPose.count = 1;
    effectFrame.leading = true;
    renderFrame = res.cameraFrame = 72;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.size() == 3);
    assert(std::abs(res.overlayDraws[1].opacity - settings.alpha / 900.0f) < 0.001f);
    res.overlayDraws.clear();
    effectFrame.leading = false;
    renderFrame = res.cameraFrame = 82;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.size() == 1 && res.overlayDraws.front().anchorOpacity > 0);
    res.overlayDraws.clear();
    renderFrame = res.cameraFrame = 83;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.empty());
    ResetPoses();
    airCombo.branch = 1351;
    auto& airOrigin = res.airOrigins[0];
    airOrigin.Begin(30, now);
    airOrigin.parts = {packet};
    airOrigin.count = 1;
    for (unsigned i = 0; i < AirTrailCount; ++i) {
        auto& pose = res.airTrail[i];
        pose.Begin(30 + AirFirstTrailFrame + i - 1, now);
        pose.parts = {packet};
        pose.count = 1;
    }
    renderFrame = res.cameraFrame = 43;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.size() == 4 && res.overlayDraws.front().anchorOpacity > settings.alpha / 300.0f);
    res.overlayDraws.clear();
    renderFrame = res.cameraFrame = 65;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.size() == 1 && res.overlayDraws.front().anchorOpacity > 0);
    res.overlayDraws.clear();
    renderFrame = res.cameraFrame = 66;
    BuildOverlayDraws(res, now);
    assert(res.overlayDraws.empty());
    for (unsigned age = 0; age <= AirFadeFrames; ++age)
        assert(std::abs(AirFade(age) - float(AirFadeFrames - age) / AirFadeFrames) < 0.001f);
    assert(AirOriginFade(4) == 1 && AirOriginFade(17) == 0.36f && AirOriginFade(AirOriginFrames) == 0);
    puts("Aerial departure pose has a longer tail than the moving poses.");
    puts("Five paired poses, independent 10-frame stomp holds, linear fades, and scene displacement passed.");
    // Costume identity comes from a model-specific torso mesh. Shared parts
    // remain eligible once the current costume has been identified.
    activeCostume = Costume::Unknown;
    for (unsigned i = 0; i < std::size(Costumes); ++i) {
        const auto& outfit = Costumes[i];
        const auto anchor = std::find_if(outfit.begin, outfit.end, [](const CostumeMesh& mesh) {
            return mesh.torsoAnchor;
        });
        assert(anchor != outfit.end);
        appearances[i] = {35 + i, 65 + i, {100 + i, 120 + i, 140 + i}};
        if (i) assert(!ObserveCostume(anchor->key));
        renderFrame += 3;
        assert(ObserveCostume(anchor->key));
        assert(activeCostume == static_cast<Costume>(i));
        assert(settings.alpha == 35 + i && settings.glowStrength == 65 + i && settings.tint[0] == 100 + i);
        for (auto part = outfit.begin; part != outfit.end; ++part)
            assert(ObserveCostume(part->key) == part);
    }
    puts("Five distinct live costume meshes select their own brightness, tint and opacity.");
    wchar_t tempDir[MAX_PATH]{}, tempIni[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, tempDir) && GetTempFileNameW(tempDir, L"ngs", 0, tempIni));
    ini = tempIni;
    assert(WritePrivateProfileStringW(L"Afterimage.Grip", L"OpacityPercent", L"37", tempIni));
    assert(WritePrivateProfileStringW(L"Afterimage.Grip", L"TintG", L"47", tempIni));
    assert(WritePrivateProfileStringW(L"Afterimage.Grip", L"GlowStrengthPercent", L"57", tempIni));
    assert(WritePrivateProfileStringW(L"Afterimage.Grip", L"MuscleHighlightPercent", L"24", tempIni));
    assert(WritePrivateProfileStringW(L"Afterimage.Ashtar", L"MuscleHighlightPercent", L"100", tempIni));
    ReadSettings();
    assert(settings.alpha == 37 && settings.glowStrength == 57 && settings.tint[1] == 47);
    assert(settings.muscleHighlight == 24 && appearances[0].muscleHighlight == 45 && appearances[3].muscleHighlight == 0);
    assert(appearances[0].alpha == 40 && appearances[0].glow == 68 && appearances[0].tint[1] == 163);
    assert(WritePrivateProfileStringW(L"Afterimage.Grip", L"OpacityPercent", L"77", tempIni));
    ReadSettings();
    assert(activeCostume == Costume::Grip && settings.alpha == 77);
    assert(WritePrivateProfileStringW(L"Afterimage", L"Material", L"Sigma2", tempIni));
    const wchar_t* sigma2Sections[]{L"Afterimage.Sigma2.Black", L"Afterimage.Sigma2.Purple",
        L"Afterimage.Sigma2.Blue", L"Afterimage.Sigma2.Ashtar", L"Afterimage.Sigma2.Grip"};
    for (unsigned i = 0; i < std::size(Costumes); ++i) {
        assert(WritePrivateProfileStringW(sigma2Sections[i], L"SurfaceBrightnessPercent",
            std::to_wstring(196 + i).c_str(), tempIni));
        assert(WritePrivateProfileStringW(sigma2Sections[i], L"GlowStrengthPercent",
            std::to_wstring(196 + i).c_str(), tempIni));
        assert(WritePrivateProfileStringW(sigma2Sections[i], L"OpacityPercent",
            std::to_wstring(60 + i).c_str(), tempIni));
    }
    ReadSettings();
    assert(settings.glassVisual && settings.alpha == 64 && settings.brightness == 200);
    for (unsigned i = 0; i < std::size(Costumes); ++i) {
        activeCostume = static_cast<Costume>(i);
        ApplyAppearance();
        assert(settings.brightness == 196 + i && settings.glowStrength == 196 + i && settings.alpha == 60 + i);
    }
    assert(WritePrivateProfileStringW(L"Afterimage", L"Material", L"Classic", tempIni));
    ReadSettings();
    assert(!settings.glassVisual && activeCostume == Costume::Grip && settings.alpha == 77);
    assert(DeleteFileW(tempIni));
    puts("Runtime reload switches material and applies the corresponding preset for each outfit.");
    ResetPoses();
    airCombo = {};
    DWORD foregroundOwner = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundOwner);
    assert(foregroundOwner != GetCurrentProcessId());
    // Submit a saved pose through the game's hooked Present path while the
    // rendering window is in the background.
    auto& backgroundTrail = res.trail[0];
    backgroundTrail.Begin(renderFrame - 1, PoseClockUs());
    backgroundTrail.parts = {packet};
    backgroundTrail.count = 1;
    backgroundTrail.fadeFrames = 9;
    res.nextTrail = 1;
    res.cameraFrame = renderFrame;
    const unsigned beforeBackgroundPresent = loggedOverlays;
    ObservePresent(swap.Get(), 0, 0);
    assert(loggedOverlays == beforeBackgroundPresent + 1);
    puts("The hooked Present composites a saved pose without foreground focus.");

    const char* curvedVertex = R"(
        float4 main(float2 p : POSITION) : SV_Position {
            float w = 1.0 - 0.55 * p.x;
            return float4(p * w, 0.5 * w, w);
        }
    )";
    assert(SUCCEEDED(D3DCompile(curvedVertex, strlen(curvedVertex), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0,
        code.ReleaseAndGetAddressOf(), nullptr)));
    assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
        curvedShader.GetAddressOf())));
    settings.glassVisual = true;
    settings.glassRefraction = 0;
    const auto transparent = draw(0, 0, FadeSteps - 1, 1);
    settings.glassRefraction = 36;
    const auto glass = draw(0, 0, FadeSteps - 1, 1);
    printf("Glass scene green %u -> %u, red stripe displaced %u -> %u.\n",
        transparent[0][1], glass[0][1], transparent[6][0], glass[6][0]);
    fflush(stdout);
    assert(glass[0][1] > 100 && transparent[0][1] > 180);
    assert(transparent[6][0] < 65 && glass[6][0] > transparent[6][0] + 60);
    const auto protectedPlayer = draw(0, 0, FadeSteps - 1, 1, false, true);
    const std::array<BYTE, 4> frontStripe{255, 0, 0, prop[3]};
    assert(protectedPlayer[0] == frontStripe && protectedPlayer[7][0] < 65);
    settings.brightness = 0;
    const auto dark = draw(0);
    settings.brightness = 100;
    const auto bright = draw(0);
    const auto emissive = draw(100);
    settings.brightness = 200;
    const auto brighter = draw(0);
    settings.brightness = 100;
    const auto strongerGlow = draw(200);
    assert(bright[4][2] > dark[4][2] && emissive[4][2] > bright[4][2]);
    assert(std::abs(int(brighter[4][2]) - 2 * int(bright[4][2]) + int(dark[4][2])) <= 2);
    assert(std::abs(int(strongerGlow[4][2]) - 2 * int(emissive[4][2]) + int(bright[4][2])) <= 2);
    const auto disabledGlass = draw(0, 0, FadeSteps - 1, 0);
    assert(disabledGlass[1] == prop && disabledGlass[2] == prop && disabledGlass[3] == prop);
    assert(res.refraction.maskFormat == DXGI_FORMAT_R8G8B8A8_UNORM);
    settings.glassVisual = false;
    draw(0);
    assert(res.refraction.maskFormat == DXGI_FORMAT_R8G8_UNORM);
    auto depthDesc = MaskDescription();
    depthDesc.Width = depthDesc.Height = Size;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTexture;
    std::array<UINT, Size * Size> splitDepth{};
    for (unsigned y = 0; y < Size; ++y)
        for (unsigned x = 0; x < Size; ++x)
            splitDepth[y * Size + x] = x < Size / 2 ? 0x00333333u : 0x00ccccccu;
    const D3D11_SUBRESOURCE_DATA initialDepth{splitDepth.data(), Size * sizeof(UINT), 0};
    assert(SUCCEEDED(device->CreateTexture2D(&depthDesc, &initialDepth, depthTexture.GetAddressOf())));
    assert(SUCCEEDED(device->CreateDepthStencilView(depthTexture.Get(), nullptr, obstacle.GetAddressOf())));
    ComPtr<ID3D11Texture2D> splitScene;
    assert(SUCCEEDED(device->CreateTexture2D(&depthDesc, &initialDepth, splitScene.GetAddressOf())));
    for (bool sigma2 : {false, true}) {
        settings.glassVisual = sigma2;
        auto visibleEdge = draw(100);
        assert(visibleEdge[6] == prop && visibleEdge[0] != prop);
        immediate->ClearDepthStencilView(obstacle.Get(), D3D11_CLEAR_DEPTH, 0.8f, 0);
        const auto inFront = draw(100);
        assert(inFront[0] != prop);
        immediate->ClearDepthStencilView(obstacle.Get(), D3D11_CLEAR_DEPTH, 0.2f, 0);
        const auto behind = draw(100);
        assert(behind[0] == prop);
        clearObstacleBeforePresent = true;
        const auto preserved = draw(100);
        assert(res.sceneDepth.frozen && preserved[0] == prop);
        clearObstacleBeforePresent = false;
        immediate->CopyResource(depthTexture.Get(), splitScene.Get());
    }
    puts("Both materials hide the part of a pose behind scene depth, including depth cleared before Present.");
    puts("Glass style preserves background detail, bends it along the surface, and protects the live silhouette.");
    hooksReady = false;
    MH_Uninitialize();
    DestroyWindow(window);
}
