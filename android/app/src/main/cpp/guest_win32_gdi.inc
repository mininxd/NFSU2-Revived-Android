// Win32 Graphics Device Interface (gdi32.dll) translation bridge backed by Rust (libgraphics_cache.so).
// Implements device contexts, font metrics, bitmaps, and fallback raster operations for SPEED2.EXE.

#ifndef GUEST_WIN32_GDI_INC
#define GUEST_WIN32_GDI_INC

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <dlfcn.h>

// CreateCompatibleDC(HDC hdc)
inline void win32CreateCompatibleDC() {
    uint32_t hdc = arg(0);

    static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_create_compatible_dc")
    );
    uint32_t res = s_fn ? s_fn(hdc) : 0x74000001;
    ret(res, 1);
}

// DeleteDC(HDC hdc)
inline void win32DeleteDC() {
    uint32_t hdc = arg(0);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_delete_dc")
    );
    int32_t res = s_fn ? s_fn(hdc) : 1;
    ret(res, 1);
}

// CreateFontA(...)
inline void win32CreateFontA() {
    int32_t cHeight = static_cast<int32_t>(arg(0));
    int32_t cWidth = static_cast<int32_t>(arg(1));
    int32_t cEscapement = static_cast<int32_t>(arg(2));
    int32_t cOrientation = static_cast<int32_t>(arg(3));
    int32_t cWeight = static_cast<int32_t>(arg(4));
    uint32_t bItalic = arg(5);
    uint32_t bUnderline = arg(6);
    uint32_t bStrikeOut = arg(7);
    uint32_t iCharSet = arg(8);
    uint32_t iOutPrecision = arg(9);
    uint32_t iClipPrecision = arg(10);
    uint32_t iQuality = arg(11);
    uint32_t iPitchAndFamily = arg(12);
    uint32_t pszFaceName = arg(13);

    static auto s_fn = reinterpret_cast<uint32_t(*)(
        int32_t, int32_t, int32_t, int32_t, int32_t,
        uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
        uint32_t, uint32_t, uint32_t, const uint8_t*
    )>(dlsym(RTLD_DEFAULT, "rust_gdi_create_font_a"));

    const uint8_t* pFace = pszFaceName && memory ? static_cast<const uint8_t*>(ptr(pszFaceName, 1)) : nullptr;
    uint32_t res = s_fn ? s_fn(
        cHeight, cWidth, cEscapement, cOrientation, cWeight,
        bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
        iClipPrecision, iQuality, iPitchAndFamily, pFace
    ) : 0x75000001;

    ret(res, 14);
}

// CreateBitmap(int nWidth, int nHeight, UINT nPlanes, UINT nBitCount, const void *lpBits)
inline void win32CreateBitmap() {
    int32_t nWidth = static_cast<int32_t>(arg(0));
    int32_t nHeight = static_cast<int32_t>(arg(1));
    uint32_t nPlanes = arg(2);
    uint32_t nBitCount = arg(3);
    uint32_t lpBits = arg(4);

    static auto s_fn = reinterpret_cast<uint32_t(*)(int32_t, int32_t, uint32_t, uint32_t, const uint8_t*)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_create_bitmap")
    );
    const uint8_t* pBits = lpBits && memory ? static_cast<const uint8_t*>(ptr(lpBits, 1)) : nullptr;
    uint32_t res = s_fn ? s_fn(nWidth, nHeight, nPlanes, nBitCount, pBits) : 0x76000001;
    ret(res, 5);
}

// ExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCSTR lpString, UINT c, const INT *lpDx)
inline void win32ExtTextOutA() {
    uint32_t hdc = arg(0);
    int32_t x = static_cast<int32_t>(arg(1));
    int32_t y = static_cast<int32_t>(arg(2));
    uint32_t options = arg(3);
    uint32_t rectPtr = arg(4);
    uint32_t strPtr = arg(5);
    uint32_t c = arg(6);
    uint32_t dxPtr = arg(7);

    static auto s_fn = reinterpret_cast<int32_t(*)(
        uint32_t, int32_t, int32_t, uint32_t, const int32_t*, const uint8_t*, uint32_t, const int32_t*
    )>(dlsym(RTLD_DEFAULT, "rust_gdi_ext_text_out_a"));

    const int32_t* pRect = rectPtr && memory ? reinterpret_cast<const int32_t*>(ptr(rectPtr, 16)) : nullptr;
    const uint8_t* pStr = strPtr && memory ? static_cast<const uint8_t*>(ptr(strPtr, c)) : nullptr;
    const int32_t* pDx = dxPtr && memory ? reinterpret_cast<const int32_t*>(ptr(dxPtr, c * 4)) : nullptr;

    int32_t res = s_fn ? s_fn(hdc, x, y, options, pRect, pStr, c, pDx) : 1;
    ret(res, 8);
}

// SelectObject(HDC hdc, HGDIOBJ h)
inline void win32SelectObject() {
    uint32_t hdc = arg(0);
    uint32_t h = arg(1);

    static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_select_object")
    );
    uint32_t res = s_fn ? s_fn(hdc, h) : h;
    ret(res, 2);
}

// BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
inline void win32BitBlt() {
    uint32_t hdc = arg(0);
    int32_t x = static_cast<int32_t>(arg(1));
    int32_t y = static_cast<int32_t>(arg(2));
    int32_t cx = static_cast<int32_t>(arg(3));
    int32_t cy = static_cast<int32_t>(arg(4));
    uint32_t hdcSrc = arg(5);
    int32_t x1 = static_cast<int32_t>(arg(6));
    int32_t y1 = static_cast<int32_t>(arg(7));
    uint32_t rop = arg(8);

    static auto s_fn = reinterpret_cast<int32_t(*)(
        uint32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t, int32_t, uint32_t
    )>(dlsym(RTLD_DEFAULT, "rust_gdi_bit_blt"));

    int32_t res = s_fn ? s_fn(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop) : 1;
    ret(res, 9);
}

// GetPixel(HDC hdc, int x, int y)
inline void win32GetPixel() {
    uint32_t hdc = arg(0);
    int32_t x = static_cast<int32_t>(arg(1));
    int32_t y = static_cast<int32_t>(arg(2));

    static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, int32_t, int32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_get_pixel")
    );
    uint32_t res = s_fn ? s_fn(hdc, x, y) : 0x00202020;
    ret(res, 3);
}

// SetTextColor(HDC hdc, COLORREF color)
inline void win32SetTextColor() {
    uint32_t hdc = arg(0);
    uint32_t color = arg(1);

    static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_set_text_color")
    );
    uint32_t res = s_fn ? s_fn(hdc, color) : 0;
    ret(res, 2);
}

// SetBkColor(HDC hdc, COLORREF color)
inline void win32SetBkColor() {
    uint32_t hdc = arg(0);
    uint32_t color = arg(1);

    static auto s_fn = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_set_bk_color")
    );
    uint32_t res = s_fn ? s_fn(hdc, color) : 0x00FFFFFF;
    ret(res, 2);
}

// SetBkMode(HDC hdc, int mode)
inline void win32SetBkMode() {
    uint32_t hdc = arg(0);
    int32_t mode = static_cast<int32_t>(arg(1));

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, int32_t)>(
        dlsym(RTLD_DEFAULT, "rust_gdi_set_bk_mode")
    );
    int32_t res = s_fn ? s_fn(hdc, mode) : 2; // OPAQUE
    ret(res, 2);
}

inline recomp_func_t resolveWin32GdiFunction(const std::string& dll, const std::string& name) {
    if (dll == "gdi32.dll") {
        if (name == "CreateCompatibleDC") return win32CreateCompatibleDC;
        if (name == "DeleteDC") return win32DeleteDC;
        if (name == "CreateFontA") return win32CreateFontA;
        if (name == "CreateBitmap") return win32CreateBitmap;
        if (name == "ExtTextOutA") return win32ExtTextOutA;
        if (name == "SelectObject") return win32SelectObject;
        if (name == "BitBlt") return win32BitBlt;
        if (name == "GetPixel") return win32GetPixel;
        if (name == "SetTextColor") return win32SetTextColor;
        if (name == "SetBkColor") return win32SetBkColor;
        if (name == "SetBkMode") return win32SetBkMode;
    }
    return nullptr;
}

#endif // GUEST_WIN32_GDI_INC
