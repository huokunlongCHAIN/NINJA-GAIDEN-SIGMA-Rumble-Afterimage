#pragma once
#include <d3d11.h>

struct AfterimageDrawHandlers {
    bool (*indexed)(ID3D11DeviceContext*, UINT, UINT, INT);
    bool (*indexedInstanced)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
    bool (*execute)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
};

struct ControllerRenderHooks {
    void* (*original)(void*, unsigned);
    void (*connect)(const AfterimageDrawHandlers*);
};
