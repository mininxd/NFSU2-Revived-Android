// Win32 Communications & Serial Port translation bridge backed by Rust (libgraphics_cache.so).
// Implements setup, configuration, timeouts, event masks, and overlapped I/O for SPEED2.EXE.

#ifndef GUEST_WIN32_COMM_INC
#define GUEST_WIN32_COMM_INC

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <dlfcn.h>

// SetupComm(HANDLE hFile, DWORD dwInQueue, DWORD dwOutQueue)
inline void win32SetupComm() {
    uint32_t handle = arg(0);
    uint32_t inQueue = arg(1);
    uint32_t outQueue = arg(2);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_setup")
    );
    int32_t res = s_fn ? s_fn(handle, inQueue, outQueue) : 1;
    ret(res, 3);
}

// SetCommState(HANDLE hFile, LPDCB lpDCB)
inline void win32SetCommState() {
    uint32_t handle = arg(0);
    uint32_t dcbPtr = arg(1);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, const uint8_t*, size_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_set_state")
    );
    const uint8_t* p = dcbPtr && memory ? static_cast<const uint8_t*>(ptr(dcbPtr, 28)) : nullptr;
    int32_t res = (s_fn && p) ? s_fn(handle, p, 28) : 1;
    ret(res, 2);
}

// GetCommState(HANDLE hFile, LPDCB lpDCB)
inline void win32GetCommState() {
    uint32_t handle = arg(0);
    uint32_t dcbPtr = arg(1);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint8_t*, size_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_get_state")
    );
    uint8_t* p = dcbPtr && memory ? static_cast<uint8_t*>(ptr(dcbPtr, 28)) : nullptr;
    if (s_fn && p) {
        ret(s_fn(handle, p, 28), 2);
    } else if (p) {
        std::memset(p, 0, 28);
        write32(dcbPtr, 28);      // DCBlength
        write32(dcbPtr + 4, 115200); // BaudRate
        write32(dcbPtr + 8, 1);   // fBinary
        write8(dcbPtr + 18, 8);   // ByteSize
        ret(1, 2);
    } else {
        ret(0, 2);
    }
}

// SetCommConfig(HANDLE hCommDev, LPCOMMCONFIG lpCC, DWORD dwSize)
inline void win32SetCommConfig() {
    uint32_t handle = arg(0);
    uint32_t cfgPtr = arg(1);
    uint32_t size = arg(2);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, const uint8_t*, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_set_config")
    );
    const uint8_t* p = cfgPtr && memory ? static_cast<const uint8_t*>(ptr(cfgPtr, size)) : nullptr;
    int32_t res = (s_fn && p) ? s_fn(handle, p, size) : 1;
    ret(res, 3);
}

// GetCommConfig(HANDLE hCommDev, LPCOMMCONFIG lpCC, LPDWORD lpdwSize)
inline void win32GetCommConfig() {
    uint32_t handle = arg(0);
    uint32_t cfgPtr = arg(1);
    uint32_t sizePtr = arg(2);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint8_t*, uint32_t*)>(
        dlsym(RTLD_DEFAULT, "rust_comm_get_config")
    );
    uint32_t* pSize = sizePtr && memory ? reinterpret_cast<uint32_t*>(ptr(sizePtr, 4)) : nullptr;
    uint8_t* pCfg = cfgPtr && memory && pSize ? static_cast<uint8_t*>(ptr(cfgPtr, *pSize)) : nullptr;

    if (s_fn && pSize) {
        ret(s_fn(handle, pCfg, pSize), 3);
    } else if (pSize) {
        *pSize = 52;
        if (pCfg) std::memset(pCfg, 0, 52);
        ret(1, 3);
    } else {
        ret(0, 3);
    }
}

// SetCommTimeouts(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts)
inline void win32SetCommTimeouts() {
    uint32_t handle = arg(0);
    uint32_t timeoutsPtr = arg(1);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, const uint8_t*, size_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_set_timeouts")
    );
    const uint8_t* p = timeoutsPtr && memory ? static_cast<const uint8_t*>(ptr(timeoutsPtr, 20)) : nullptr;
    int32_t res = (s_fn && p) ? s_fn(handle, p, 20) : 1;
    ret(res, 2);
}

// SetCommMask(HANDLE hFile, DWORD dwEvtMask)
inline void win32SetCommMask() {
    uint32_t handle = arg(0);
    uint32_t mask = arg(1);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_set_mask")
    );
    int32_t res = s_fn ? s_fn(handle, mask) : 1;
    ret(res, 2);
}

// WaitCommEvent(HANDLE hFile, LPDWORD lpEvtMask, LPOVERLAPPED lpOverlapped)
inline void win32WaitCommEvent() {
    uint32_t handle = arg(0);
    uint32_t maskPtr = arg(1);
    uint32_t ovPtr = arg(2);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t*, uint8_t*)>(
        dlsym(RTLD_DEFAULT, "rust_comm_wait_event")
    );
    uint32_t* pMask = maskPtr && memory ? reinterpret_cast<uint32_t*>(ptr(maskPtr, 4)) : nullptr;
    uint8_t* pOv = ovPtr && memory ? static_cast<uint8_t*>(ptr(ovPtr, 20)) : nullptr;

    if (s_fn) {
        ret(s_fn(handle, pMask, pOv), 3);
    } else {
        if (pMask) *pMask = 0;
        ret(1, 3);
    }
}

// PurgeComm(HANDLE hFile, DWORD dwFlags)
inline void win32PurgeComm() {
    uint32_t handle = arg(0);
    uint32_t flags = arg(1);

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, uint32_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_purge")
    );
    int32_t res = s_fn ? s_fn(handle, flags) : 1;
    ret(res, 2);
}

// GetOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait)
inline void win32GetOverlappedResult() {
    uint32_t handle = arg(0);
    uint32_t ovPtr = arg(1);
    uint32_t bytesPtr = arg(2);
    int32_t wait = static_cast<int32_t>(arg(3));

    static auto s_fn = reinterpret_cast<int32_t(*)(uint32_t, const uint8_t*, uint32_t*, int32_t)>(
        dlsym(RTLD_DEFAULT, "rust_comm_get_overlapped_result")
    );
    const uint8_t* pOv = ovPtr && memory ? static_cast<const uint8_t*>(ptr(ovPtr, 20)) : nullptr;
    uint32_t* pBytes = bytesPtr && memory ? reinterpret_cast<uint32_t*>(ptr(bytesPtr, 4)) : nullptr;

    if (s_fn && pOv) {
        ret(s_fn(handle, pOv, pBytes, wait), 4);
    } else if (pOv) {
        if (pBytes) *pBytes = read32(ovPtr + 4); // InternalHigh
        ret(1, 4);
    } else {
        ret(0, 4);
    }
}

inline recomp_func_t resolveWin32CommFunction(const std::string& dll, const std::string& name) {
    if (dll == "kernel32.dll") {
        if (name == "SetupComm") return win32SetupComm;
        if (name == "SetCommState") return win32SetCommState;
        if (name == "GetCommState") return win32GetCommState;
        if (name == "SetCommConfig") return win32SetCommConfig;
        if (name == "GetCommConfig") return win32GetCommConfig;
        if (name == "SetCommTimeouts") return win32SetCommTimeouts;
        if (name == "SetCommMask") return win32SetCommMask;
        if (name == "WaitCommEvent") return win32WaitCommEvent;
        if (name == "PurgeComm") return win32PurgeComm;
        if (name == "GetOverlappedResult") return win32GetOverlappedResult;
    }
    return nullptr;
}

#endif // GUEST_WIN32_COMM_INC
