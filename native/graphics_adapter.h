#pragma once

#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>

enum class GraphicsAdapter { Unknown, Integrated, Discrete, Software };

namespace GraphicsAdapterDetection {
// The MinGW SDK used by the build does not ship dxcore_interface.h. These are
// the leading methods of the two COM interfaces from Microsoft's DirectX-Headers.
// https://github.com/microsoft/DirectX-Headers/blob/v1.616.0/include/directx/dxcore_interface.h
enum class Property : uint32_t { IsHardware = 11, IsIntegrated = 12 };
struct Adapter : IUnknown {
    virtual bool STDMETHODCALLTYPE IsValid() = 0;
    virtual bool STDMETHODCALLTYPE IsAttributeSupported(REFGUID attribute) = 0;
    virtual bool STDMETHODCALLTYPE IsPropertySupported(Property property) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(Property property, size_t bytes, void* value) = 0;
};
struct Factory : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateAdapterList(UINT count, const GUID* attributes, REFIID iid, void** list) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterByLuid(const LUID& luid, REFIID iid, void** adapter) = 0;
};
constexpr GUID FactoryId{0x78ee5945, 0xc36e, 0x4b13, {0xa6, 0x69, 0x00, 0x5d, 0xd1, 0x1c, 0x0f, 0x06}};
constexpr GUID AdapterId{0xf0db4c7f, 0xfe5a, 0x42a2, {0xbd, 0x62, 0xf2, 0xa6, 0xcf, 0x6f, 0xc8, 0x3e}};

inline GraphicsAdapter QueryDxCore(const LUID& luid) {
    using Microsoft::WRL::ComPtr;
    using CreateFactory = HRESULT (WINAPI*)(REFIID, void**);
    HMODULE library = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return GraphicsAdapter::Unknown;
    auto result = GraphicsAdapter::Unknown;
    {
        const auto create = reinterpret_cast<CreateFactory>(GetProcAddress(library, "DXCoreCreateAdapterFactory"));
        ComPtr<Factory> factory;
        ComPtr<Adapter> adapter;
        if (create && SUCCEEDED(create(FactoryId, reinterpret_cast<void**>(factory.GetAddressOf()))) &&
            SUCCEEDED(factory->GetAdapterByLuid(luid, AdapterId, reinterpret_cast<void**>(adapter.GetAddressOf())))) {
            bool hardware = true, integrated = false;
            if (SUCCEEDED(adapter->GetProperty(Property::IsHardware, sizeof(hardware), &hardware)) && !hardware)
                result = GraphicsAdapter::Software;
            else if (SUCCEEDED(adapter->GetProperty(Property::IsIntegrated, sizeof(integrated), &integrated)))
                result = integrated ? GraphicsAdapter::Integrated : GraphicsAdapter::Discrete;
        }
    }
    FreeLibrary(library);
    return result;
}

inline GraphicsAdapter ClassifyLegacy(UINT vendor, SIZE_T dedicated, SIZE_T shared) {
    // Older DXGI runtimes expose the memory sizes but no integrated flag.
    if (vendor == 0x1414) return GraphicsAdapter::Software;
    if ((vendor == 0x8086 || vendor == 0x1002 || vendor == 0x1022) && shared && dedicated <= 512ull * 1024 * 1024)
        return GraphicsAdapter::Integrated;
    if (vendor == 0x10de || dedicated > 512ull * 1024 * 1024)
        return GraphicsAdapter::Discrete;
    return GraphicsAdapter::Unknown;
}

inline const char* Name(GraphicsAdapter adapter) {
    switch (adapter) {
        case GraphicsAdapter::Integrated: return "integrated";
        case GraphicsAdapter::Discrete: return "discrete";
        case GraphicsAdapter::Software: return "software";
        default: return "unknown";
    }
}
}
