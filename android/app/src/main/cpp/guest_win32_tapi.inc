// Win32 Telephony API (tapi32.dll) translation bridge backed by Rust (libgraphics_cache.so).
// Implements line device initialization, negotiation, call management, and stubs for SPEED2.EXE.

#ifndef GUEST_WIN32_TAPI_INC
#define GUEST_WIN32_TAPI_INC

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <dlfcn.h>

// lineInitialize(LPHLINEAPP lphLineApp, HINSTANCE hInstance, LINECALLBACK lpfnCallback, LPCSTR lpszAppName, LPDWORD lpdwNumDevs)
inline void win32LineInitialize() {
    uint32_t appPtr = arg(0);
    uint32_t instance = arg(1);
    uint32_t callback = arg(2);
    uint32_t namePtr = arg(3);
    uint32_t devsPtr = arg(4);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t*, uint32_t, uint32_t, const uint8_t*, uint32_t*)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_initialize")
    );
    uint32_t* pApp = appPtr && memory ? reinterpret_cast<uint32_t*>(ptr(appPtr, 4)) : nullptr;
    uint32_t* pDevs = devsPtr && memory ? reinterpret_cast<uint32_t*>(ptr(devsPtr, 4)) : nullptr;
    const uint8_t* pName = namePtr && memory ? static_cast<const uint8_t*>(ptr(namePtr, 1)) : nullptr;

    if (s_fn && pApp && pDevs) {
        ret(s_fn(pApp, instance, callback, pName, pDevs), 5);
    } else {
        if (pApp) *pApp = 0x71000001;
        if (pDevs) *pDevs = 0;
        ret(0, 5); // SUCCESS
    }
}

// lineShutdown(HLINEAPP hLineApp)
inline void win32LineShutdown() {
    uint32_t app = arg(0);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_shutdown")
    );
    int32_t res = s_fn ? s_fn(app) : 0;
    ret(res, 1);
}

// lineOpen(HLINEAPP hLineApp, DWORD dwDeviceID, LPHLINE lphLine, DWORD dwAPIVersion, DWORD dwExtVersion, DWORD_PTR dwCallbackInstance, DWORD dwPrivileges, DWORD dwMediaModes, LPLINECALLPARAMS lpCallParams)
inline void win32LineOpen() {
    uint32_t app = arg(0);
    uint32_t devId = arg(1);
    uint32_t linePtr = arg(2);
    uint32_t apiVer = arg(3);
    uint32_t extVer = arg(4);
    uint32_t cbInst = arg(5);
    uint32_t privs = arg(6);
    uint32_t mediaModes = arg(7);
    uint32_t paramsPtr = arg(8);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_open")
    );
    uint32_t* pLine = linePtr && memory ? reinterpret_cast<uint32_t*>(ptr(linePtr, 4)) : nullptr;
    const uint8_t* pParams = paramsPtr && memory ? static_cast<const uint8_t*>(ptr(paramsPtr, 1)) : nullptr;

    if (s_fn && pLine) {
        ret(s_fn(app, devId, pLine, apiVer, extVer, cbInst, privs, mediaModes, pParams), 9);
    } else {
        if (pLine) *pLine = 0x72000001;
        ret(0, 9);
    }
}

// lineClose(HLINE hLine)
inline void win32LineClose() {
    uint32_t line = arg(0);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_close")
    );
    int32_t res = s_fn ? s_fn(line) : 0;
    ret(res, 1);
}

// lineGetDevCaps(HLINEAPP hLineApp, DWORD dwDeviceID, DWORD dwAPIVersion, DWORD dwExtVersion, LPLINEDEVCAPS lpLineDevCaps)
inline void win32LineGetDevCaps() {
    uint32_t app = arg(0);
    uint32_t devId = arg(1);
    uint32_t apiVer = arg(2);
    uint32_t extVer = arg(3);
    uint32_t capsPtr = arg(4);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*, size_t)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_get_dev_caps")
    );
    uint8_t* pCaps = capsPtr && memory ? static_cast<uint8_t*>(ptr(capsPtr, 16)) : nullptr;

    if (s_fn && pCaps) {
        ret(s_fn(app, devId, apiVer, extVer, pCaps, 16), 5);
    } else {
        if (pCaps) {
            std::memset(pCaps, 0, 16);
            write32(capsPtr, 16);      // dwTotalSize
            write32(capsPtr + 4, 16);  // dwNeededSize
            write32(capsPtr + 8, 16);  // dwUsedSize
            write32(capsPtr + 12, 8);  // dwMediaModes (DATAMODEM)
        }
        ret(0, 5);
    }
}

// lineGetID(HLINE hLine, DWORD dwAddressID, HCALL hCall, DWORD dwSelect, LPVARSTRING lpDeviceID, LPCSTR lpszDeviceClass)
inline void win32LineGetID() {
    uint32_t line = arg(0);
    uint32_t addrId = arg(1);
    uint32_t call = arg(2);
    uint32_t select = arg(3);
    uint32_t varStrPtr = arg(4);
    uint32_t devClassPtr = arg(5);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*, size_t, const uint8_t*)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_get_id")
    );
    uint8_t* pStr = varStrPtr && memory ? static_cast<uint8_t*>(ptr(varStrPtr, 16)) : nullptr;
    const uint8_t* pClass = devClassPtr && memory ? static_cast<const uint8_t*>(ptr(devClassPtr, 1)) : nullptr;

    if (s_fn && pStr) {
        ret(s_fn(line, addrId, call, select, pStr, 16, pClass), 6);
    } else {
        if (pStr) {
            std::memset(pStr, 0, 16);
            write32(varStrPtr, 16);
            write32(varStrPtr + 4, 16);
            write32(varStrPtr + 8, 16);
        }
        ret(0, 6);
    }
}

// lineNegotiateAPIVersion(HLINEAPP hLineApp, DWORD dwDeviceID, DWORD dwAPILowVersion, DWORD dwAPIHighVersion, LPDWORD lpdwAPIVersion, LPLINEEXTENSIONID lpExtensionID)
inline void win32LineNegotiateAPIVersion() {
    uint32_t app = arg(0);
    uint32_t devId = arg(1);
    uint32_t lowVer = arg(2);
    uint32_t highVer = arg(3);
    uint32_t verPtr = arg(4);
    uint32_t extPtr = arg(5);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint8_t*)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_negotiate_api_version")
    );
    uint32_t* pVer = verPtr && memory ? reinterpret_cast<uint32_t*>(ptr(verPtr, 4)) : nullptr;
    uint8_t* pExt = extPtr && memory ? static_cast<uint8_t*>(ptr(extPtr, 16)) : nullptr;

    if (s_fn && pVer) {
        ret(s_fn(app, devId, lowVer, highVer, pVer, pExt), 6);
    } else {
        if (pVer) *pVer = 0x00020000;
        if (pExt) std::memset(pExt, 0, 16);
        ret(0, 6);
    }
}

// lineMakeCall(HLINE hLine, LPHCALL lphCall, LPCSTR lpszDestAddress, DWORD dwCountryCode, LPLINECALLPARAMS lpCallParams)
inline void win32LineMakeCall() {
    uint32_t line = arg(0);
    uint32_t callPtr = arg(1);
    uint32_t destPtr = arg(2);
    uint32_t country = arg(3);
    uint32_t paramsPtr = arg(4);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t*, const uint8_t*, uint32_t, const uint8_t*)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_make_call")
    );
    uint32_t* pCall = callPtr && memory ? reinterpret_cast<uint32_t*>(ptr(callPtr, 4)) : nullptr;
    const uint8_t* pDest = destPtr && memory ? static_cast<const uint8_t*>(ptr(destPtr, 1)) : nullptr;
    const uint8_t* pParams = paramsPtr && memory ? static_cast<const uint8_t*>(ptr(paramsPtr, 1)) : nullptr;

    if (s_fn && pCall) {
        ret(s_fn(line, pCall, pDest, country, pParams), 5);
    } else {
        if (pCall) *pCall = 0x73000001;
        ret(0, 5);
    }
}

// lineAnswer(HCALL hCall, LPCSTR lpsUserUserInfo, DWORD dwSize)
inline void win32LineAnswer() {
    uint32_t call = arg(0);
    uint32_t infoPtr = arg(1);
    uint32_t size = arg(2);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, const uint8_t*, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_tapi_line_answer")
    );
    const uint8_t* pInfo = infoPtr && memory ? static_cast<const uint8_t*>(ptr(infoPtr, size)) : nullptr;
    int32_t res = s_fn ? s_fn(call, pInfo, size) : 0;
    ret(res, 3);
}

inline recomp_func_t resolveWin32TapiFunction(const std::string& dll, const std::string& name) {
    if (dll == "tapi32.dll") {
        if (name == "lineInitialize") return win32LineInitialize;
        if (name == "lineShutdown") return win32LineShutdown;
        if (name == "lineOpen") return win32LineOpen;
        if (name == "lineClose") return win32LineClose;
        if (name == "lineGetDevCaps") return win32LineGetDevCaps;
        if (name == "lineGetID") return win32LineGetID;
        if (name == "lineNegotiateAPIVersion") return win32LineNegotiateAPIVersion;
        if (name == "lineMakeCall") return win32LineMakeCall;
        if (name == "lineAnswer") return win32LineAnswer;
    }
    return nullptr;
}

#endif // GUEST_WIN32_TAPI_INC
