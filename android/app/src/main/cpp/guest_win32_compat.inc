// DirectDraw, Shell32, and Netapi32 Win32 Translation Bridge backed by Rust.
// Connects guest x86 SPEED2.EXE API calls to high-performance Rust implementations in libgraphics_cache.so.

#ifndef GUEST_WIN32_COMPAT_INC
#define GUEST_WIN32_COMPAT_INC

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <dlfcn.h>

constexpr uint32_t ddrawMethodBase = 0x7f000000;
static uint32_t ddrawToken = 0;
static uint32_t ddrawDirectDrawObject = 0;
static uint32_t ddrawRefCount = 1;

// IDirectDraw COM method dispatcher
void ddrawDispatch() {
    uint32_t method = ((ddrawToken - ddrawMethodBase) % 0x1000) / 16;
    uint32_t self = arg(0);

    switch (method) {
        case 0: { // QueryInterface(this, riid, ppvObj)
            uint32_t outObj = arg(2);
            if (outObj) {
                write32(outObj, self);
                ddrawRefCount++;
                ret(0, 3); // S_OK
            } else {
                ret(0x80004003u, 3); // E_POINTER
            }
            return;
        }
        case 1: { // AddRef(this)
            ret(++ddrawRefCount, 1);
            return;
        }
        case 2: { // Release(this)
            if (ddrawRefCount > 0) ddrawRefCount--;
            ret(ddrawRefCount, 1);
            return;
        }
        case 8: { // EnumDisplayModes(this, dwFlags, lpDDSurfaceDesc, lpContext, lpEnumCallback)
            uint32_t callback = arg(4);
            uint32_t context = arg(3);

            static auto s_get_count = reinterpret_cast<uint32_t(*)()>(
                dlsym(RTLD_DEFAULT, "rust_ddraw_get_mode_count")
            );
            static auto s_get_mode = reinterpret_cast<uint32_t(*)(uint32_t, uint8_t*, size_t)>(
                dlsym(RTLD_DEFAULT, "rust_ddraw_get_mode_at")
            );

            if (callback && memory && s_get_count && s_get_mode) {
                uint32_t modeCount = s_get_count();
                uint32_t tempDesc = allocate(108);
                for (uint32_t i = 0; i < modeCount; ++i) {
                    s_get_mode(i, static_cast<uint8_t*>(ptr(tempDesc, 108)), 108);
                    // Invoke guest callback: HRESULT (WINAPI*)(LPDDSURFACEDESC, LPVOID)
                    uint32_t cont = callGuest(callback, {tempDesc, context});
                    if (cont == 0) break; // Callback returned DDENUMRET_CANCEL
                }
                heaps->free(GuestHeaps::process, tempDesc);
            }
            ret(0, 5); // DD_OK
            return;
        }
        case 11: { // GetCaps(this, lpDDDriverCaps, lpDDHELCaps)
            uint32_t drvCaps = arg(1);
            uint32_t helCaps = arg(2);
            uint8_t* drvPtr = drvCaps ? static_cast<uint8_t*>(ptr(drvCaps, 40)) : nullptr;
            uint8_t* helPtr = helCaps ? static_cast<uint8_t*>(ptr(helCaps, 40)) : nullptr;

            static auto s_get_caps = reinterpret_cast<uint32_t(*)(uint8_t*, size_t, uint8_t*, size_t)>(
                dlsym(RTLD_DEFAULT, "rust_ddraw_get_caps")
            );
            if (s_get_caps) {
                uint32_t res = s_get_caps(drvPtr, drvCaps ? 40 : 0, helPtr, helCaps ? 40 : 0);
                ret(res, 3);
            } else {
                if (drvPtr) { std::memset(drvPtr, 0, 40); write32(drvCaps, 40); write32(drvCaps + 4, 0x41); }
                if (helPtr) { std::memset(helPtr, 0, 40); write32(helCaps, 40); write32(helCaps + 4, 0x40); }
                ret(0, 3);
            }
            return;
        }
        case 12: { // GetDisplayMode(this, lpDDSurfaceDesc)
            uint32_t desc = arg(1);
            if (!desc) {
                ret(0x80004003u, 2); // DDERR_INVALIDPARAMS
                return;
            }

            static auto s_get_mode = reinterpret_cast<uint32_t(*)(uint8_t*, size_t, uint32_t, uint32_t, uint32_t)>(
                dlsym(RTLD_DEFAULT, "rust_ddraw_get_display_mode")
            );
            if (s_get_mode) {
                uint32_t res = s_get_mode(static_cast<uint8_t*>(ptr(desc, 108)), 108, 1280, 720, 60);
                ret(res, 2);
            } else {
                uint8_t* p = static_cast<uint8_t*>(ptr(desc, 108));
                std::memset(p, 0, 108);
                write32(desc, 108);       // dwSize
                write32(desc + 4, 0x41047);// dwFlags
                write32(desc + 8, 720);   // dwHeight
                write32(desc + 12, 1280); // dwWidth
                write32(desc + 16, 1280*4);// lPitch
                write32(desc + 20, 60);   // dwRefreshRate
                write32(desc + 72, 32);   // pf_size
                write32(desc + 76, 0x40); // pf_flags: DDPF_RGB
                write32(desc + 84, 32);   // pf_rgb_bit_count
                ret(0, 2);
            }
            return;
        }
        case 15: { // GetMonitorFrequency(this, lpdwFrequency)
            uint32_t outHz = arg(1);
            if (outHz) write32(outHz, 60);
            ret(0, 2); // DD_OK
            return;
        }
        case 17: { // GetVerticalBlankStatus(this, lpbIsInVB)
            uint32_t outVb = arg(1);
            if (outVb) write32(outVb, 1);
            ret(0, 2); // DD_OK
            return;
        }
        case 20: { // SetCooperativeLevel(this, hWnd, dwFlags)
            static auto s_set_coop = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t)>(
                dlsym(RTLD_DEFAULT, "rust_ddraw_set_cooperative_level")
            );
            uint32_t res = s_set_coop ? s_set_coop(arg(1), arg(2)) : 0;
            ret(res, 3);
            return;
        }
        default:
            ret(0, 1); // DD_OK for benign unhandled query stubs
            return;
    }
}

// DirectDrawCreate(LPGUID lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter)
void directDrawCreate() {
    uint32_t outDD = arg(1);
    if (!outDD) {
        ret(0x80004003u, 3); // DDERR_INVALIDPARAMS
        return;
    }

    if (!ddrawDirectDrawObject) {
        ddrawDirectDrawObject = allocate(4);
        uint32_t vtable = allocate(32 * 4);
        for (uint32_t slot = 0; slot < 32; ++slot) {
            write32(vtable + 4 * slot, ddrawMethodBase + 0x1000 + slot * 16);
        }
        write32(ddrawDirectDrawObject, vtable);
    }

    ddrawRefCount++;
    write32(outDD, ddrawDirectDrawObject);
    ret(0, 3); // DD_OK
}

// ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
void shellExecuteA() {
    uint32_t hwnd = arg(0);
    uint32_t opPtr = arg(1);
    uint32_t filePtr = arg(2);
    uint32_t paramsPtr = arg(3);
    uint32_t dirPtr = arg(4);
    int32_t showCmd = static_cast<int32_t>(arg(5));

    std::string opStr = opPtr ? guestString(opPtr) : "";
    std::string fileStr = filePtr ? guestString(filePtr) : "";
    std::string paramsStr = paramsPtr ? guestString(paramsPtr) : "";
    std::string dirStr = dirPtr ? guestString(dirPtr) : "";

    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "ShellExecuteA: op='%s' file='%s' params='%s'",
                    opStr.c_str(), fileStr.c_str(), paramsStr.c_str());

    static auto s_shell_exec = reinterpret_cast<uint32_t(*)(uint32_t, const char*, const char*, const char*, const char*, int32_t)>(
        dlsym(RTLD_DEFAULT, "rust_shell_execute_a")
    );

    uint32_t hinst = 42; // HINSTANCE > 32 = Success
    if (s_shell_exec) {
        hinst = s_shell_exec(
            hwnd,
            opPtr ? opStr.c_str() : nullptr,
            filePtr ? fileStr.c_str() : nullptr,
            paramsPtr ? paramsStr.c_str() : nullptr,
            dirPtr ? dirStr.c_str() : nullptr,
            showCmd
        );
    }
    ret(hinst, 6);
}

// ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
void shellExecuteW() {
    shellExecuteA();
}

// SHGetFolderPathA(HWND hwnd, int nFolder, HANDLE hToken, DWORD dwFlags, LPSTR pszPath)
void shellFolderPathRust() {
    uint32_t hwnd = arg(0);
    int32_t folder = static_cast<int32_t>(arg(1));
    uint32_t token = arg(2);
    uint32_t flags = arg(3);
    uint32_t outPath = arg(4);

    if (!outPath) {
        ret(0x80070057u, 5); // E_INVALIDARG
        return;
    }

    static auto s_get_folder = reinterpret_cast<int32_t(*)(uint32_t, int32_t, uint32_t, uint32_t, char*, size_t, const char*)>(
        dlsym(RTLD_DEFAULT, "rust_shell_get_folder_path_a")
    );

    char localBuffer[512] = {0};
    int32_t hr = 0;

    if (s_get_folder) {
        hr = s_get_folder(
            hwnd, folder, token, flags, localBuffer, sizeof(localBuffer), hostGameRoot.c_str()
        );
    } else {
        std::strncpy(localBuffer, "C:\\nfsu2", sizeof(localBuffer) - 1);
    }

    if (hr == 0) {
        size_t len = std::strlen(localBuffer);
        std::memcpy(ptr(outPath, len + 1), localBuffer, len + 1);
    }
    ret(static_cast<uint32_t>(hr), 5);
}

// Netbios(PNCB pncb) backed by Rust
void netbiosRust() {
    uint32_t ncb = arg(0);
    if (!ncb) {
        ret(0x03, 1); // NRC_ILLCMD
        return;
    }

    auto* ncbPtr = static_cast<uint8_t*>(ptr(ncb, 64));
    uint32_t buffer = read32(ncb + 4);
    uint16_t length = 0;
    std::memcpy(&length, ncbPtr + 8, 2);

    uint8_t* bufPtr = (buffer && length > 0) ? static_cast<uint8_t*>(ptr(buffer, length)) : nullptr;

    static auto s_netbios = reinterpret_cast<uint8_t(*)(uint8_t*, size_t, uint8_t*, size_t, const char*)>(
        dlsym(RTLD_DEFAULT, "rust_netapi_netbios")
    );

    uint8_t status = 0;
    if (s_netbios) {
        status = s_netbios(ncbPtr, 64, bufPtr, length, hostGameRoot.c_str());
    } else {
        // Safe internal fallback
        uint8_t command = ncbPtr[0] & 0x7f;
        if (command == 0x37 && bufPtr && length >= 2) {
            bufPtr[0] = 1; bufPtr[1] = 0;
        } else if (command == 0x33 && bufPtr && length >= 6) {
            uint32_t seed = 0x811c9dc5u;
            for (char c : hostGameRoot) seed = (seed ^ uint8_t(c)) * 0x01000193u;
            uint8_t mac[6] = {0x02, 0x4e, uint8_t(seed), uint8_t(seed>>8), uint8_t(seed>>16), uint8_t(seed>>24)};
            std::memset(bufPtr, 0, length);
            std::memcpy(bufPtr, mac, 6);
        }
        ncbPtr[1] = status;
        ncbPtr[49] = status;
    }
    ret(status, 1);
}

// NetApiBufferFree(LPVOID Buffer)
void netApiBufferFree() {
    static auto s_buf_free = reinterpret_cast<uint32_t(*)(uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_netapi_buffer_free")
    );
    uint32_t res = s_buf_free ? s_buf_free(arg(0)) : 0;
    ret(res, 1);
}

// NetWkstaGetInfo(LMSTR servername, DWORD level, LPBYTE* bufptr)
void netWkstaGetInfo() {
    uint32_t serverPtr = arg(0);
    uint32_t level = arg(1);
    uint32_t outBufPtr = arg(2);

    if (!outBufPtr) {
        ret(87, 3); // ERROR_INVALID_PARAMETER
        return;
    }

    uint32_t guestMem = allocate(64);
    size_t written = 0;
    std::string srvStr = serverPtr ? guestString(serverPtr) : "";

    static auto s_wksta_info = reinterpret_cast<uint32_t(*)(const char*, uint32_t, uint8_t*, size_t, size_t*)>(
        dlsym(RTLD_DEFAULT, "rust_netapi_wksta_get_info")
    );

    if (s_wksta_info) {
        s_wksta_info(
            serverPtr ? srvStr.c_str() : nullptr,
            level,
            static_cast<uint8_t*>(ptr(guestMem, 64)),
            64,
            &written
        );
    } else {
        uint8_t* p = static_cast<uint8_t*>(ptr(guestMem, 64));
        std::memset(p, 0, 64);
        write32(guestMem, 500); // PLATFORM_ID_NT
        write32(guestMem + 12, 5); // Major 5 (XP)
        write32(guestMem + 16, 1); // Minor 1
    }

    write32(outBufPtr, guestMem);
    ret(0, 3); // NERR_Success
}

// NetGetJoinInformation(LPCWSTR lpServer, LPWSTR* lpNameBuffer, PNETSETUP_JOIN_STATUS BufferType)
void netGetJoinInformation() {
    uint32_t outName = arg(1);
    uint32_t outType = arg(2);

    uint32_t statusVal = 2; // NetSetupWorkgroupName
    uint32_t nameBuf = allocate(32);

    static auto s_join_info = reinterpret_cast<uint32_t(*)(uint32_t*, uint8_t*, size_t)>(
        dlsym(RTLD_DEFAULT, "rust_netapi_get_join_information")
    );

    if (s_join_info) {
        s_join_info(
            &statusVal,
            static_cast<uint8_t*>(ptr(nameBuf, 32)),
            32
        );
    } else {
        uint16_t name_u16[] = {'W','O','R','K','G','R','O','U','P',0};
        std::memcpy(ptr(nameBuf, 20), name_u16, 20);
    }

    if (outType) write32(outType, statusVal);
    if (outName) write32(outName, nameBuf);

    ret(0, 3); // NERR_Success
}

// Resolves Win32 compat functions implemented in this unit
inline recomp_func_t resolveWin32CompatFunction(const std::string& dll, const std::string& name) {
    if (dll == "ddraw.dll" && name == "DirectDrawCreate") return directDrawCreate;

    if (dll == "shell32.dll" || dll == "shfolder.dll") {
        if (name == "ShellExecuteA") return shellExecuteA;
        if (name == "ShellExecuteW") return shellExecuteW;
    }

    if (dll == "netapi32.dll") {
        if (name == "Netbios") return netbiosRust;
        if (name == "NetApiBufferFree") return netApiBufferFree;
        if (name == "NetWkstaGetInfo") return netWkstaGetInfo;
        if (name == "NetGetJoinInformation") return netGetJoinInformation;
    }

    return nullptr;
}

#endif // GUEST_WIN32_COMPAT_INC
