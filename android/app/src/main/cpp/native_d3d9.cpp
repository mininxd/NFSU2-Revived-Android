#include "native_d3d9.h"
#include "runtime_log.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <dlfcn.h>

namespace {
std::mutex s_window_mutex;
ANativeWindow* s_current_native_window = nullptr;
HWND s_current_window_handle = nullptr;
uint32_t s_render_width = 1280;
uint32_t s_render_height = 720;

inline bool isOpenGLBackend() {
    const char* rend = std::getenv("NFSU2_RENDERER");
    return rend && (std::strcmp(rend, "opengl") == 0 || std::strcmp(rend, "opengl_es") == 0);
}
}

void* resolveGraphicsCacheSymbol(const char* name);

void nativeAndroidSetWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(s_window_mutex);
    s_current_native_window = window;
    s_current_window_handle = window ? reinterpret_cast<HWND>(uintptr_t{0x1000}) : nullptr;
}

HWND nativeAndroidGetWindowHandle() {
    std::lock_guard<std::mutex> lock(s_window_mutex);
    return s_current_window_handle;
}

void nativeAndroidSetRenderSize(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(s_window_mutex);
    s_render_width = width;
    s_render_height = height;
#ifdef __ANDROID__
    if (s_current_native_window && width && height) {
        ANativeWindow_setBuffersGeometry(s_current_native_window, width, height, 0);
    }
#endif
}

extern "C" {
    void dxvkAndroidSetWindow(ANativeWindow* window) { nativeAndroidSetWindow(window); }
    HWND dxvkAndroidGetWindowHandle() { return nativeAndroidGetWindowHandle(); }
    void dxvkAndroidSetRenderSize(uint32_t width, uint32_t height) { nativeAndroidSetRenderSize(width, height); }
    void dxvkAndroidSetInstanceProcAddr(void*) {}
    void* dxvkAndroidGetInstanceProcAddr() { return nullptr; }
}

// ---------------------------------------------------------------------------
// Native D3D9 Classes
// ---------------------------------------------------------------------------

class NativeD3D9Surface : public IDirect3DSurface9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    D3DSURFACE_DESC m_desc{};
    std::vector<uint8_t> m_data;
    uint32_t m_pitch{0};

public:
    NativeD3D9Surface(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
        : m_device(dev), m_desc(desc) {
        uint32_t bpp = 4;
        if (desc.Format == D3DFMT_DXT1) bpp = 1;
        else if (desc.Format == D3DFMT_R5G6B5 || desc.Format == D3DFMT_A1R5G5B5 || desc.Format == D3DFMT_D16) bpp = 2;
        m_pitch = desc.Width * bpp;
        m_data.resize(m_pitch * desc.Height, 0);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_SURFACE; }

    HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void** ppContainer) override {
        if (ppContainer) *ppContainer = nullptr;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* pDesc) override {
        if (pDesc) *pDesc = m_desc;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* pLockedRect, const RECT*, DWORD) override {
        if (!pLockedRect) return D3DERR_INVALIDCALL;
        pLockedRect->Pitch = m_pitch;
        pLockedRect->pBits = m_data.data();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDC(HDC*) override { return D3DERR_NOTAVAILABLE; }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return D3D_OK; }
};

class NativeD3D9Texture : public IDirect3DTexture9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    UINT m_width{0}, m_height{0}, m_levels{1};
    DWORD m_usage{0};
    D3DFORMAT m_format{D3DFMT_A8R8G8B8};
    D3DPOOL m_pool{D3DPOOL_MANAGED};
    std::vector<NativeD3D9Surface*> m_surfaces;

public:
    NativeD3D9Texture(IDirect3DDevice9* dev, UINT w, UINT h, UINT lvls, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
        : m_device(dev), m_width(w), m_height(h), m_levels(lvls ? lvls : 1), m_usage(usage), m_format(fmt), m_pool(pool) {
        UINT cw = w, ch = h;
        for (UINT i = 0; i < m_levels; ++i) {
            D3DSURFACE_DESC desc{};
            desc.Format = fmt;
            desc.Type = D3DRTYPE_SURFACE;
            desc.Usage = usage;
            desc.Pool = pool;
            desc.Width = std::max(1u, cw);
            desc.Height = std::max(1u, ch);
            m_surfaces.push_back(new NativeD3D9Surface(dev, desc));
            cw >>= 1;
            ch >>= 1;
        }
    }

    ~NativeD3D9Texture() {
        for (auto* s : m_surfaces) {
            if (s) s->Release();
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }

    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_NONE; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}

    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc) override {
        if (level < m_surfaces.size() && m_surfaces[level]) {
            return m_surfaces[level]->GetDesc(pDesc);
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9** ppSurfaceLevel) override {
        if (level < m_surfaces.size() && m_surfaces[level]) {
            *ppSurfaceLevel = m_surfaces[level];
            m_surfaces[level]->AddRef();
            return D3D_OK;
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD flags) override {
        if (level < m_surfaces.size() && m_surfaces[level]) {
            return m_surfaces[level]->LockRect(pLockedRect, pRect, flags);
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override {
        if (level < m_surfaces.size() && m_surfaces[level]) {
            return m_surfaces[level]->UnlockRect();
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) override { return D3D_OK; }
};

class NativeD3D9CubeTexture : public IDirect3DCubeTexture9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    UINT m_edge{0}, m_levels{1};
    std::vector<NativeD3D9Surface*> m_surfaces[6];

public:
    NativeD3D9CubeTexture(IDirect3DDevice9* dev, UINT edge, UINT lvls, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
        : m_device(dev), m_edge(edge), m_levels(lvls ? lvls : 1) {
        for (int face = 0; face < 6; ++face) {
            UINT ce = edge;
            for (UINT i = 0; i < m_levels; ++i) {
                D3DSURFACE_DESC desc{};
                desc.Format = fmt;
                desc.Type = D3DRTYPE_SURFACE;
                desc.Usage = usage;
                desc.Pool = pool;
                desc.Width = std::max(1u, ce);
                desc.Height = std::max(1u, ce);
                m_surfaces[face].push_back(new NativeD3D9Surface(dev, desc));
                ce >>= 1;
            }
        }
    }
    ~NativeD3D9CubeTexture() {
        for (int face = 0; face < 6; ++face) {
            for (auto* s : m_surfaces[face]) {
                if (s) s->Release();
            }
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }

    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_NONE; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}

    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc) override {
        if (level < m_surfaces[0].size() && m_surfaces[0][level]) {
            return m_surfaces[0][level]->GetDesc(pDesc);
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** ppSurface) override {
        if (face < 6 && level < m_surfaces[face].size() && m_surfaces[face][level]) {
            *ppSurface = m_surfaces[face][level];
            m_surfaces[face][level]->AddRef();
            return D3D_OK;
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD flags) override {
        if (face < 6 && level < m_surfaces[face].size() && m_surfaces[face][level]) {
            return m_surfaces[face][level]->LockRect(pLockedRect, pRect, flags);
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override {
        if (face < 6 && level < m_surfaces[face].size() && m_surfaces[face][level]) {
            return m_surfaces[face][level]->UnlockRect();
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES, const RECT*) override { return D3D_OK; }
};

class NativeD3D9VolumeTexture : public IDirect3DVolumeTexture9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
public:
    NativeD3D9VolumeTexture(IDirect3DDevice9* dev) : m_device(dev) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VOLUMETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_NONE; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT, D3DVOLUME_DESC*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT, IDirect3DVolume9**) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE LockBox(UINT, D3DLOCKED_BOX*, const D3DBOX*, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX*) override { return D3D_OK; }
};

class NativeD3D9VertexBuffer : public IDirect3DVertexBuffer9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    D3DVERTEXBUFFER_DESC m_desc{};
    std::vector<uint8_t> m_data;

public:
    NativeD3D9VertexBuffer(IDirect3DDevice9* dev, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool)
        : m_device(dev) {
        m_desc.Format = D3DFMT_VERTEXDATA;
        m_desc.Type = D3DRTYPE_VERTEXBUFFER;
        m_desc.Usage = usage;
        m_desc.Pool = pool;
        m_desc.Size = len;
        m_desc.FVF = fvf;
        m_data.resize(len, 0);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }

    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void** ppbData, DWORD) override {
        if (!ppbData) return D3DERR_INVALIDCALL;
        if (offset >= m_data.size()) offset = 0;
        *ppbData = m_data.data() + offset;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* pDesc) override {
        if (pDesc) *pDesc = m_desc;
        return D3D_OK;
    }
};

class NativeD3D9IndexBuffer : public IDirect3DIndexBuffer9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    D3DINDEXBUFFER_DESC m_desc{};
    std::vector<uint8_t> m_data;

public:
    NativeD3D9IndexBuffer(IDirect3DDevice9* dev, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
        : m_device(dev) {
        m_desc.Format = fmt;
        m_desc.Type = D3DRTYPE_INDEXBUFFER;
        m_desc.Usage = usage;
        m_desc.Pool = pool;
        m_desc.Size = len;
        m_data.resize(len, 0);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }

    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void** ppbData, DWORD) override {
        if (!ppbData) return D3DERR_INVALIDCALL;
        if (offset >= m_data.size()) offset = 0;
        *ppbData = m_data.data() + offset;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* pDesc) override {
        if (pDesc) *pDesc = m_desc;
        return D3D_OK;
    }
};

class NativeD3D9VertexDeclaration : public IDirect3DVertexDeclaration9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    std::vector<D3DVERTEXELEMENT9> m_elements;

public:
    NativeD3D9VertexDeclaration(IDirect3DDevice9* dev, const D3DVERTEXELEMENT9* pElements)
        : m_device(dev) {
        if (pElements) {
            for (int i = 0; pElements[i].Stream != 0xFF; ++i) {
                m_elements.push_back(pElements[i]);
            }
            D3DVERTEXELEMENT9 endElem = D3DDECL_END();
            m_elements.push_back(endElem);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* pElements, UINT* pNumElements) override {
        if (pNumElements) *pNumElements = static_cast<UINT>(m_elements.size());
        if (pElements) {
            std::memcpy(pElements, m_elements.data(), m_elements.size() * sizeof(D3DVERTEXELEMENT9));
        }
        return D3D_OK;
    }
};

class NativeD3D9VertexShader : public IDirect3DVertexShader9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    std::vector<DWORD> m_code;
public:
    NativeD3D9VertexShader(IDirect3DDevice9* dev, const DWORD* pFunction) : m_device(dev) {
        if (pFunction) m_code.assign(pFunction, pFunction + 64);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFunction(void*, UINT* pSizeOfData) override {
        if (pSizeOfData) *pSizeOfData = static_cast<UINT>(m_code.size() * sizeof(DWORD));
        return D3D_OK;
    }
};

class NativeD3D9PixelShader : public IDirect3DPixelShader9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    std::vector<DWORD> m_code;
public:
    NativeD3D9PixelShader(IDirect3DDevice9* dev, const DWORD* pFunction) : m_device(dev) {
        if (pFunction) m_code.assign(pFunction, pFunction + 64);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFunction(void*, UINT* pSizeOfData) override {
        if (pSizeOfData) *pSizeOfData = static_cast<UINT>(m_code.size() * sizeof(DWORD));
        return D3D_OK;
    }
};

class NativeD3D9StateBlock : public IDirect3DStateBlock9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
public:
    NativeD3D9StateBlock(IDirect3DDevice9* dev) : m_device(dev) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Capture() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Apply() override { return D3D_OK; }
};

class NativeD3D9Query : public IDirect3DQuery9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    D3DQUERYTYPE m_type{D3DQUERYTYPE_EVENT};
public:
    NativeD3D9Query(IDirect3DDevice9* dev, D3DQUERYTYPE t) : m_device(dev), m_type(t) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    D3DQUERYTYPE STDMETHODCALLTYPE GetType() override { return m_type; }
    DWORD STDMETHODCALLTYPE GetDataSize() override { return sizeof(DWORD); }
    HRESULT STDMETHODCALLTYPE Issue(DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetData(void* pData, DWORD, DWORD) override {
        if (pData) *static_cast<DWORD*>(pData) = 1;
        return S_OK;
    }
};

class NativeD3D9SwapChain : public IDirect3DSwapChain9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3DDevice9* m_device{nullptr};
    NativeD3D9Surface* m_backBuffer{nullptr};
public:
    NativeD3D9SwapChain(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS& p) : m_device(dev) {
        D3DSURFACE_DESC desc{};
        desc.Format = p.BackBufferFormat;
        desc.Type = D3DRTYPE_SURFACE;
        desc.Usage = D3DUSAGE_RENDERTARGET;
        desc.Pool = D3DPOOL_DEFAULT;
        desc.Width = p.BackBufferWidth ? p.BackBufferWidth : 1280;
        desc.Height = p.BackBufferHeight ? p.BackBufferHeight : 720;
        m_backBuffer = new NativeD3D9Surface(dev, desc);
    }
    ~NativeD3D9SwapChain() {
        if (m_backBuffer) m_backBuffer->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT*, const RECT*, HWND, const RGNDATA*, DWORD) override {
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** ppBackBuffer) override {
        if (ppBackBuffer && m_backBuffer) {
            *ppBackBuffer = m_backBuffer;
            m_backBuffer->AddRef();
            return D3D_OK;
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* pMode) override {
        if (pMode) {
            pMode->Width = 1280;
            pMode->Height = 720;
            pMode->RefreshRate = 60;
            pMode->Format = D3DFMT_X8R8G8B8;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) *ppDevice = m_device;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS*) override { return D3D_OK; }
};

// ---------------------------------------------------------------------------
// Native IDirect3DDevice9 Implementation
// ---------------------------------------------------------------------------

class NativeD3D9Device : public IDirect3DDevice9 {
    std::atomic<ULONG> m_refCount{1};
    IDirect3D9* m_d3d{nullptr};
    D3DPRESENT_PARAMETERS m_presentParams{};
    NativeD3D9Surface* m_renderTarget{nullptr};
    NativeD3D9Surface* m_depthStencil{nullptr};
    NativeD3D9SwapChain* m_swapChain{nullptr};

public:
    NativeD3D9Device(IDirect3D9* d3d, const D3DPRESENT_PARAMETERS& params)
        : m_d3d(d3d), m_presentParams(params) {
        D3DSURFACE_DESC rtDesc{};
        rtDesc.Format = params.BackBufferFormat;
        rtDesc.Type = D3DRTYPE_SURFACE;
        rtDesc.Usage = D3DUSAGE_RENDERTARGET;
        rtDesc.Pool = D3DPOOL_DEFAULT;
        rtDesc.Width = params.BackBufferWidth ? params.BackBufferWidth : 1280;
        rtDesc.Height = params.BackBufferHeight ? params.BackBufferHeight : 720;
        m_renderTarget = new NativeD3D9Surface(this, rtDesc);

        D3DSURFACE_DESC dsDesc{};
        dsDesc.Format = params.AutoDepthStencilFormat ? params.AutoDepthStencilFormat : D3DFMT_D24S8;
        dsDesc.Type = D3DRTYPE_SURFACE;
        dsDesc.Usage = D3DUSAGE_DEPTHSTENCIL;
        dsDesc.Pool = D3DPOOL_DEFAULT;
        dsDesc.Width = rtDesc.Width;
        dsDesc.Height = rtDesc.Height;
        m_depthStencil = new NativeD3D9Surface(this, dsDesc);

        m_swapChain = new NativeD3D9SwapChain(this, params);
    }

    ~NativeD3D9Device() {
        if (m_renderTarget) m_renderTarget->Release();
        if (m_depthStencil) m_depthStencil->Release();
        if (m_swapChain) m_swapChain->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 1024 * 1024 * 512; }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override {
        if (ppD3D9) { *ppD3D9 = m_d3d; if (m_d3d) m_d3d->AddRef(); }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps) override {
        return m_d3d ? m_d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, pCaps) : D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE* pMode) override {
        if (pMode) {
            pMode->Width = m_presentParams.BackBufferWidth;
            pMode->Height = m_presentParams.BackBufferHeight;
            pMode->RefreshRate = 60;
            pMode->Format = m_presentParams.BackBufferFormat;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override {
        if (p) {
            p->AdapterOrdinal = 0;
            p->DeviceType = D3DDEVTYPE_HAL;
            p->hFocusWindow = nativeAndroidGetWindowHandle();
            p->BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9*) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) override {}
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL) override { return FALSE; }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* p, IDirect3DSwapChain9** ppSC) override {
        if (ppSC && p) { *ppSC = new NativeD3D9SwapChain(this, *p); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT, IDirect3DSwapChain9** ppSC) override {
        if (ppSC && m_swapChain) { *ppSC = m_swapChain; m_swapChain->AddRef(); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 1; }

    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p) override {
        if (p) {
            m_presentParams = *p;
            nativeAndroidSetRenderSize(p->BackBufferWidth, p->BackBufferHeight);
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE Present(const RECT*, const RECT*, HWND, const RGNDATA*) override {
        if (isOpenGLBackend()) {
            static auto s_gles_present = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_gles_present"));
            if (s_gles_present && s_gles_present() == 0) return D3D_OK;
        } else {
            static auto s_vk_present = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_vk_present"));
            if (s_vk_present && s_vk_present() == 0) return D3D_OK;
            static auto s_gles_present = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_gles_present"));
            if (s_gles_present && s_gles_present() == 0) return D3D_OK;
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** ppBackBuffer) override {
        if (ppBackBuffer && m_renderTarget) {
            *ppBackBuffer = m_renderTarget;
            m_renderTarget->AddRef();
            return D3D_OK;
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP*) override {}
    void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP*) override {}

    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT lvls, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** ppTex, HANDLE*) override {
        if (!ppTex) return D3DERR_INVALIDCALL;
        *ppTex = new NativeD3D9Texture(this, w, h, lvls, usage, fmt, pool);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9** ppTex, HANDLE*) override {
        if (!ppTex) return D3DERR_INVALIDCALL;
        *ppTex = new NativeD3D9VolumeTexture(this);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge, UINT lvls, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9** ppTex, HANDLE*) override {
        if (!ppTex) return D3DERR_INVALIDCALL;
        *ppTex = new NativeD3D9CubeTexture(this, edge, lvls, usage, fmt, pool);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** ppVB, HANDLE*) override {
        if (!ppVB) return D3DERR_INVALIDCALL;
        *ppVB = new NativeD3D9VertexBuffer(this, len, usage, fvf, pool);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9** ppIB, HANDLE*) override {
        if (!ppIB) return D3DERR_INVALIDCALL;
        *ppIB = new NativeD3D9IndexBuffer(this, len, usage, fmt, pool);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** ppSurf, HANDLE*) override {
        if (!ppSurf) return D3DERR_INVALIDCALL;
        D3DSURFACE_DESC desc{};
        desc.Format = fmt; desc.Type = D3DRTYPE_SURFACE; desc.Usage = D3DUSAGE_RENDERTARGET; desc.Pool = D3DPOOL_DEFAULT;
        desc.Width = w; desc.Height = h;
        *ppSurf = new NativeD3D9Surface(this, desc);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** ppSurf, HANDLE*) override {
        if (!ppSurf) return D3DERR_INVALIDCALL;
        D3DSURFACE_DESC desc{};
        desc.Format = fmt; desc.Type = D3DRTYPE_SURFACE; desc.Usage = D3DUSAGE_DEPTHSTENCIL; desc.Pool = D3DPOOL_DEFAULT;
        desc.Width = w; desc.Height = h;
        *ppSurf = new NativeD3D9Surface(this, desc);
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9*, IDirect3DBaseTexture9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9*, IDirect3DSurface9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9*, const RECT*, D3DCOLOR) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, IDirect3DSurface9** ppSurf, HANDLE*) override {
        if (!ppSurf) return D3DERR_INVALIDCALL;
        D3DSURFACE_DESC desc{};
        desc.Format = fmt; desc.Type = D3DRTYPE_SURFACE; desc.Usage = 0; desc.Pool = pool;
        desc.Width = w; desc.Height = h;
        *ppSurf = new NativeD3D9Surface(this, desc);
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD, IDirect3DSurface9* pSurf) override {
        if (pSurf != m_renderTarget) {
            if (m_renderTarget) m_renderTarget->Release();
            m_renderTarget = static_cast<NativeD3D9Surface*>(pSurf);
            if (m_renderTarget) m_renderTarget->AddRef();
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD, IDirect3DSurface9** ppSurf) override {
        if (ppSurf && m_renderTarget) { *ppSurf = m_renderTarget; m_renderTarget->AddRef(); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pSurf) override {
        if (pSurf != m_depthStencil) {
            if (m_depthStencil) m_depthStencil->Release();
            m_depthStencil = static_cast<NativeD3D9Surface*>(pSurf);
            if (m_depthStencil) m_depthStencil->AddRef();
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppSurf) override {
        if (ppSurf && m_depthStencil) { *ppSurf = m_depthStencil; m_depthStencil->AddRef(); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }

    HRESULT STDMETHODCALLTYPE BeginScene() override {
        if (!isOpenGLBackend()) {
            static auto s_vk_begin = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_vk_begin_scene"));
            if (s_vk_begin) s_vk_begin();
        }
        static auto s_gles_begin = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_gles_begin_scene"));
        if (s_gles_begin) s_gles_begin();
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE EndScene() override {
        if (!isOpenGLBackend()) {
            static auto s_vk_end = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_vk_end_scene"));
            if (s_vk_end) s_vk_end();
        }
        static auto s_gles_end = reinterpret_cast<int(*)()>(resolveGraphicsCacheSymbol("nfs_native_gles_end_scene"));
        if (s_gles_end) s_gles_end();
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override {
        if (!isOpenGLBackend()) {
            static auto s_vk_clear = reinterpret_cast<int(*)(uint32_t,uint32_t,float,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_clear"));
            if (s_vk_clear) s_vk_clear(flags, color, z, stencil);
        }
        static auto s_gles_clear = reinterpret_cast<int(*)(uint32_t,uint32_t,float,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_clear"));
        if (s_gles_clear) s_gles_clear(flags, color, z, stencil);
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override {
        if (matrix) {
            if (!isOpenGLBackend()) {
                static auto s_vk_tf = reinterpret_cast<int(*)(uint32_t,const float*)>(resolveGraphicsCacheSymbol("nfs_native_vk_set_transform"));
                if (s_vk_tf) s_vk_tf(state, reinterpret_cast<const float*>(matrix));
            }
            static auto s_gles_tf = reinterpret_cast<int(*)(uint32_t,const float*)>(resolveGraphicsCacheSymbol("nfs_native_gles_set_transform"));
            if (s_gles_tf) s_gles_tf(state, reinterpret_cast<const float*>(matrix));
            static auto s_b_tf = reinterpret_cast<void(*)(uint32_t,const float*)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_transform"));
            if (s_b_tf) s_b_tf(state, reinterpret_cast<const float*>(matrix));
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX* pMatrix) override {
        if (pMatrix) {
            static auto s_b_gtf = reinterpret_cast<void(*)(uint32_t,float*)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_get_transform"));
            if (s_b_gtf) s_b_gtf(state, reinterpret_cast<float*>(pMatrix));
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* vp) override {
        if (vp) {
            if (!isOpenGLBackend()) {
                static auto s_vk_vp = reinterpret_cast<int(*)(int,int,int,int)>(resolveGraphicsCacheSymbol("nfs_native_vk_set_viewport"));
                if (s_vk_vp) s_vk_vp(vp->X, vp->Y, vp->Width, vp->Height);
            }
            static auto s_gles_vp = reinterpret_cast<int(*)(int,int,int,int)>(resolveGraphicsCacheSymbol("nfs_native_gles_set_viewport"));
            if (s_gles_vp) s_gles_vp(vp->X, vp->Y, vp->Width, vp->Height);
            static auto s_b_vp = reinterpret_cast<void(*)(const void*)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_viewport"));
            if (s_b_vp) s_b_vp(vp);
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override {
        if (pViewport) {
            pViewport->X = 0; pViewport->Y = 0;
            pViewport->Width = m_presentParams.BackBufferWidth;
            pViewport->Height = m_presentParams.BackBufferHeight;
            pViewport->MinZ = 0.0f; pViewport->MaxZ = 1.0f;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, const D3DLIGHT9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD, D3DLIGHT9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, WINBOOL) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, WINBOOL* p) override { if (p) *p = TRUE; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, const float*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, float*) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD val) override {
        if (!isOpenGLBackend()) {
            static auto s_vk_rs = reinterpret_cast<int(*)(uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_set_render_state"));
            if (s_vk_rs) s_vk_rs(state, val);
        }
        static auto s_gles_rs = reinterpret_cast<int(*)(uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_set_render_state"));
        if (s_gles_rs) s_gles_rs(state, val);
        static auto s_b_rs = reinterpret_cast<void(*)(uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_render_state"));
        if (s_b_rs) s_b_rs(state, val);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD* pValue) override {
        if (pValue) {
            static auto s_b_grs = reinterpret_cast<uint32_t(*)(uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_get_render_state"));
            *pValue = s_b_grs ? s_b_grs(state) : 0;
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9** ppSB) override {
        if (ppSB) { *ppSB = new NativeD3D9StateBlock(this); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** ppSB) override {
        if (ppSB) { *ppSB = new NativeD3D9StateBlock(this); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9*) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE GetTexture(DWORD, IDirect3DBaseTexture9** ppTex) override {
        if (ppTex) *ppTex = nullptr;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9* pTex) override {
        if (!isOpenGLBackend()) {
            static auto s_vk_tex = reinterpret_cast<int(*)(uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_set_texture"));
            if (s_vk_tex) s_vk_tex(stage, pTex ? 1 : 0);
        }
        static auto s_gles_tex = reinterpret_cast<int(*)(uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_set_texture"));
        if (s_gles_tex) s_gles_tex(stage, pTex ? 1 : 0);
        static auto s_b_tex = reinterpret_cast<void(*)(uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_texture"));
        if (s_b_tex) s_b_tex(stage, pTex ? 1 : 0);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD* p) override {
        if (p) *p = 0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD val) override {
        static auto s_b_tss = reinterpret_cast<void(*)(uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_texture_stage_state"));
        if (s_b_tss) s_b_tss(stage, type, val);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD* p) override {
        if (p) *p = 0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD val) override {
        static auto s_b_ss = reinterpret_cast<void(*)(uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_sampler_state"));
        if (s_b_ss) s_b_ss(sampler, type, val);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* p) override { if (p) *p = 1; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, const PALETTEENTRY*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT, PALETTEENTRY*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* p) override { if (p) *p = 0; return D3D_OK; }

    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* rect) override {
        if (rect) {
            static auto s_b_sc = reinterpret_cast<void(*)(const void*)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_scissor_rect"));
            if (s_b_sc) s_b_sc(rect);
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override {
        if (pRect) { pRect->left = 0; pRect->top = 0; pRect->right = m_presentParams.BackBufferWidth; pRect->bottom = m_presentParams.BackBufferHeight; }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL) override { return D3D_OK; }
    WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return D3D_OK; }
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0.0f; }

    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE primType, UINT startVertex, UINT primCount) override {
        if (isOpenGLBackend()) {
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_primitive"));
            if (s_gles_draw && s_gles_draw(primType, startVertex, primCount) == 0) return D3D_OK;
        } else {
            static auto s_vk_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_draw_primitive"));
            if (s_vk_draw && s_vk_draw(primType, startVertex, primCount) == 0) return D3D_OK;
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_primitive"));
            if (s_gles_draw && s_gles_draw(primType, startVertex, primCount) == 0) return D3D_OK;
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE primType, INT baseVertex, UINT minIdx, UINT numVtx, UINT startIdx, UINT primCount) override {
        if (isOpenGLBackend()) {
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,int,uint32_t,uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_indexed"));
            if (s_gles_draw && s_gles_draw(primType, baseVertex, minIdx, numVtx, startIdx, primCount) == 0) return D3D_OK;
        } else {
            static auto s_vk_draw = reinterpret_cast<int(*)(uint32_t,int,uint32_t,uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_draw_indexed"));
            if (s_vk_draw && s_vk_draw(primType, baseVertex, minIdx, numVtx, startIdx, primCount) == 0) return D3D_OK;
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,int,uint32_t,uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_indexed"));
            if (s_gles_draw && s_gles_draw(primType, baseVertex, minIdx, numVtx, startIdx, primCount) == 0) return D3D_OK;
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE primType, UINT primCount, const void* pData, UINT stride) override {
        if (isOpenGLBackend()) {
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,const void*,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_primitive_up"));
            if (s_gles_draw && s_gles_draw(primType, primCount, pData, stride) == 0) return D3D_OK;
        } else {
            static auto s_vk_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,const void*,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_draw_primitive_up"));
            if (s_vk_draw && s_vk_draw(primType, primCount, pData, stride) == 0) return D3D_OK;
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,const void*,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_primitive_up"));
            if (s_gles_draw && s_gles_draw(primType, primCount, pData, stride) == 0) return D3D_OK;
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primType, UINT minVtxIdx, UINT vtxCount, UINT primCount, const void* pIdxData, D3DFORMAT idxFmt, const void* pVtxData, UINT stride) override {
        if (isOpenGLBackend()) {
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,uint32_t,uint32_t,const void*,uint32_t,const void*,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_indexed_primitive_up"));
            if (s_gles_draw && s_gles_draw(primType, minVtxIdx, vtxCount, primCount, pIdxData, idxFmt, pVtxData, stride) == 0) return D3D_OK;
        } else {
            static auto s_vk_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,uint32_t,uint32_t,const void*,uint32_t,const void*,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_vk_draw_indexed_primitive_up"));
            if (s_vk_draw && s_vk_draw(primType, minVtxIdx, vtxCount, primCount, pIdxData, idxFmt, pVtxData, stride) == 0) return D3D_OK;
            static auto s_gles_draw = reinterpret_cast<int(*)(uint32_t,uint32_t,uint32_t,uint32_t,const void*,uint32_t,const void*,uint32_t)>(resolveGraphicsCacheSymbol("nfs_native_gles_draw_indexed_primitive_up"));
            if (s_gles_draw && s_gles_draw(primType, minVtxIdx, vtxCount, primCount, pIdxData, idxFmt, pVtxData, stride) == 0) return D3D_OK;
        }
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* pElements, IDirect3DVertexDeclaration9** ppDecl) override {
        if (ppDecl && pElements) {
            *ppDecl = new NativeD3D9VertexDeclaration(this, pElements);
            return D3D_OK;
        }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override {
        if (ppDecl) *ppDecl = nullptr;
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override {
        static auto s_b_fvf = reinterpret_cast<void(*)(uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_fvf"));
        if (s_b_fvf) s_b_fvf(fvf);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* p) override { if (p) *p = 0; return D3D_OK; }

    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* pCode, IDirect3DVertexShader9** ppShader) override {
        if (ppShader && pCode) { *ppShader = new NativeD3D9VertexShader(this, pCode); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override {
        if (ppShader) *ppShader = nullptr;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT, const float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT, float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, const int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT, int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, const WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT, WINBOOL*, UINT) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer9* pVB, UINT offset, UINT stride) override {
        static auto s_b_ss = reinterpret_cast<void(*)(uint32_t,uint32_t,uint32_t,uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_stream_source"));
        if (s_b_ss) s_b_ss(stream, pVB ? 1 : 0, offset, stride);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT, IDirect3DVertexBuffer9** ppVB, UINT* pOffset, UINT* pStride) override {
        if (ppVB) *ppVB = nullptr;
        if (pOffset) *pOffset = 0;
        if (pStride) *pStride = 0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT, UINT* p) override { if (p) *p = 1; return D3D_OK; }

    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIB) override {
        static auto s_b_si = reinterpret_cast<void(*)(uint32_t)>(resolveGraphicsCacheSymbol("nfs_d3d9_bridge_set_indices"));
        if (s_b_si) s_b_si(pIB ? 1 : 0);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIB) override {
        if (ppIB) *ppIB = nullptr;
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* pCode, IDirect3DPixelShader9** ppShader) override {
        if (ppShader && pCode) { *ppShader = new NativeD3D9PixelShader(this, pCode); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override {
        if (ppShader) *ppShader = nullptr;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT, const float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT, float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, const int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT, int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, const WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT, WINBOOL*, UINT) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return D3D_OK; }

    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9** ppQuery) override {
        if (ppQuery) { *ppQuery = new NativeD3D9Query(this, type); return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
};

// ---------------------------------------------------------------------------
// Native IDirect3D9 Implementation
// ---------------------------------------------------------------------------

class NativeD3D9 : public IDirect3D9 {
    std::atomic<ULONG> m_refCount{1};
    UINT m_sdkVersion{0};

public:
    NativeD3D9(UINT sdk) : m_sdkVersion(sdk) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9* pId) override {
        if (!pId) return D3DERR_INVALIDCALL;
        std::memset(pId, 0, sizeof(D3DADAPTER_IDENTIFIER9));
        std::strncpy(pId->Driver, "nfs_native_gpu.so", sizeof(pId->Driver) - 1);
        std::strncpy(pId->Description, "NFSU2 Native Mobile Renderer (ARM64)", sizeof(pId->Description) - 1);
        std::strncpy(pId->DeviceName, "\\\\.\\DISPLAY1", sizeof(pId->DeviceName) - 1);
        pId->VendorId = 0x5143; // Qualcomm / Adreno
        pId->DeviceId = 0x0740;
        pId->SubSysId = 0x0001;
        pId->Revision = 1;
        return D3D_OK;
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT, D3DFORMAT) override { return 1; }

    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, D3DFORMAT, UINT, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        pMode->Width = 1280;
        pMode->Height = 720;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        pMode->Width = 1280;
        pMode->Height = 720;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, WINBOOL) override {
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) override {
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, WINBOOL, D3DMULTISAMPLE_TYPE, DWORD* pLevels) override {
        if (pLevels) *pLevels = 1;
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override {
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override {
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9* pCaps) override {
        if (!pCaps) return D3DERR_INVALIDCALL;
        std::memset(pCaps, 0, sizeof(D3DCAPS9));
        pCaps->DeviceType = D3DDEVTYPE_HAL;
        pCaps->AdapterOrdinal = 0;
        pCaps->Caps = D3DCAPS_READ_SCANLINE;
        pCaps->Caps2 = D3DCAPS2_CANAUTOGENMIPMAP;
        pCaps->Caps3 = 0;
        pCaps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
        pCaps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_PUREDEVICE | D3DDEVCAPS_CANRENDERAFTERFLIP;
        pCaps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE;
        pCaps->RasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_MIPMAPLODBIAS;
        pCaps->ZCmpCaps = 0xFF;
        pCaps->SrcBlendCaps = 0xFFFF;
        pCaps->DestBlendCaps = 0xFFFF;
        pCaps->AlphaCmpCaps = 0xFF;
        pCaps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB;
        pCaps->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA;
        pCaps->TextureFilterCaps = 0xFFFFFFFF;
        pCaps->TextureAddressCaps = 0xFF;
        pCaps->MaxTextureWidth = 4096;
        pCaps->MaxTextureHeight = 4096;
        pCaps->MaxVolumeExtent = 512;
        pCaps->MaxTextureRepeat = 8192;
        pCaps->MaxTextureAspectRatio = 4096;
        pCaps->MaxAnisotropy = 16;
        pCaps->MaxVertexW = 1e10f;
        pCaps->GuardBandLeft = -4096.0f;
        pCaps->GuardBandTop = -4096.0f;
        pCaps->GuardBandRight = 4096.0f;
        pCaps->GuardBandBottom = 4096.0f;
        pCaps->ExtentsDummy = 0.0f;
        pCaps->MaxTextureStages = 8;
        pCaps->MaxSimultaneousTextures = 8;
        pCaps->VertexShaderVersion = D3DVS_VERSION(2, 0);
        pCaps->MaxVertexShaderConst = 256;
        pCaps->PixelShaderVersion = D3DPS_VERSION(2, 0);
        pCaps->MaxPixelShaderValue = 1e6f;
        pCaps->MaxStreams = 8;
        pCaps->MaxStreamStride = 256;
        return D3D_OK;
    }

    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override {
        return reinterpret_cast<HMONITOR>(uintptr_t{1});
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS* pParams, IDirect3DDevice9** ppDevice) override {
        if (!ppDevice || !pParams) return D3DERR_INVALIDCALL;
        *ppDevice = new NativeD3D9Device(this, *pParams);
        return D3D_OK;
    }
};

extern "C" IDirect3D9* Direct3DCreate9(UINT sdk) {
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2-Native", "Direct3DCreate9 (Native Pure ARM64 Pipeline sdk=%u)", sdk);
    return new NativeD3D9(sdk);
}
