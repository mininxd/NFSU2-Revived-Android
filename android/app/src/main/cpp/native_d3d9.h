#pragma once

#include <d3d9.h>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#ifdef __ANDROID__
#include <android/native_window.h>
#endif

// Native Mobile Window State
void nativeAndroidSetWindow(ANativeWindow* window);
HWND nativeAndroidGetWindowHandle();
void nativeAndroidSetRenderSize(uint32_t width, uint32_t height);

// Compatibility exports
extern "C" {
    void dxvkAndroidSetWindow(ANativeWindow* window);
    HWND dxvkAndroidGetWindowHandle();
    void dxvkAndroidSetRenderSize(uint32_t width, uint32_t height);
    IDirect3D9* Direct3DCreate9(UINT sdk);
}
