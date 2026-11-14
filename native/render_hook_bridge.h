#pragma once
#include <d3d11.h>

using IndexedDraw = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using InstancedIndexedDraw = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using ExecuteDrawList = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);

struct AfterimageDrawHandlers {
    bool (*indexed)(IndexedDraw, ID3D11DeviceContext*, UINT, UINT, INT);
    bool (*indexedInstanced)(InstancedIndexedDraw, ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
    bool (*execute)(ExecuteDrawList, ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
};

struct ControllerRenderHooks {
    void* (*original)(void**, unsigned);
    void (*connect)(const AfterimageDrawHandlers*);
};
