// Core Win32 API translations for SPEED2.EXE and server.dll (kernel32, user32, advapi32)
// Provides memory space queries, locale/string comparisons, process/thread lifecycle,
// registry key creation, and exception handling stubs.

#ifndef GUEST_WIN32_CORE_INC
#define GUEST_WIN32_CORE_INC

#include <cstdint>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <string>
#include <algorithm>

// VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength)
inline void virtualQuery() {
    uint32_t addr = arg(0);
    uint32_t buf = arg(1);
    uint32_t len = arg(2);

    if (!buf || len < 28 || !memory) {
        ret(0, 3);
        return;
    }

    uint32_t pageBase = addr & ~0xFFFu;
    bool isReadable = memory->readable(addr, 1);
    uint32_t allocProtect = isReadable ? 0x40u : 0x01u; // PAGE_EXECUTE_READWRITE or PAGE_NOACCESS
    uint32_t state = isReadable ? 0x1000u : 0x10000u;    // MEM_COMMIT or MEM_FREE
    uint32_t type = (addr >= 0x00400000u && addr < 0x00932000u) ? 0x1000000u : 0x20000u; // MEM_IMAGE or MEM_PRIVATE

    write32(buf, pageBase);          // BaseAddress
    write32(buf + 4, pageBase);      // AllocationBase
    write32(buf + 8, allocProtect);  // AllocationProtect
    write32(buf + 12, 4096);         // RegionSize
    write32(buf + 16, state);        // State
    write32(buf + 20, allocProtect); // Protect
    write32(buf + 24, type);         // Type

    ret(28, 3);
}

// IsBadCodePtr(FARPROC lpfn)
inline void isBadCodePtr() {
    uint32_t ptrVal = arg(0);
    bool bad = !ptrVal || !memory || !memory->readable(ptrVal, 1);
    ret(bad ? 1 : 0, 1);
}

// RegCreateKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult)
inline void regCreateKeyA() {
    registryOpen(arg(2), 3);
}

// CompareStringA(LCID Locale, DWORD dwCmpFlags, LPCSTR lpString1, int cchCount1, LPCSTR lpString2, int cchCount2)
inline void compareStringA() {
    uint32_t flags = arg(1);
    uint32_t s1 = arg(2);
    int32_t cch1 = static_cast<int32_t>(arg(3));
    uint32_t s2 = arg(4);
    int32_t cch2 = static_cast<int32_t>(arg(5));

    std::string str1 = (s1 && memory) ? ((cch1 >= 0) ? std::string(static_cast<const char*>(ptr(s1, cch1)), cch1) : guestString(s1)) : "";
    std::string str2 = (s2 && memory) ? ((cch2 >= 0) ? std::string(static_cast<const char*>(ptr(s2, cch2)), cch2) : guestString(s2)) : "";

    bool ignoreCase = (flags & 0x00000001u) != 0; // NORM_IGNORECASE
    int cmp = ignoreCase ? strcasecmp(str1.c_str(), str2.c_str()) : std::strcmp(str1.c_str(), str2.c_str());

    // CSTR_LESS_THAN = 1, CSTR_EQUAL = 2, CSTR_GREATER_THAN = 3
    uint32_t res = (cmp < 0) ? 1 : (cmp > 0) ? 3 : 2;
    ret(res, 6);
}

// CompareStringW(LCID Locale, DWORD dwCmpFlags, LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2)
inline void compareStringW() {
    uint32_t flags = arg(1);
    uint32_t s1 = arg(2);
    int32_t cch1 = static_cast<int32_t>(arg(3));
    uint32_t s2 = arg(4);
    int32_t cch2 = static_cast<int32_t>(arg(5));

    int len1 = (cch1 >= 0) ? cch1 : 0;
    int len2 = (cch2 >= 0) ? cch2 : 0;
    int cmp = 0;

    if (s1 && s2 && memory) {
        if (cch1 < 0) {
            while (s1 + len1 * 2 < GuestMemory::size && memory->readable(s1 + len1 * 2, 2) &&
                   *static_cast<const uint16_t*>(ptr(s1 + len1 * 2, 2)) != 0) {
                len1++;
            }
        }
        if (cch2 < 0) {
            while (s2 + len2 * 2 < GuestMemory::size && memory->readable(s2 + len2 * 2, 2) &&
                   *static_cast<const uint16_t*>(ptr(s2 + len2 * 2, 2)) != 0) {
                len2++;
            }
        }

        int minLen = std::min(len1, len2);
        bool ignoreCase = (flags & 0x00000001u) != 0;
        for (int i = 0; i < minLen; ++i) {
            uint16_t c1 = *static_cast<const uint16_t*>(ptr(s1 + i * 2, 2));
            uint16_t c2 = *static_cast<const uint16_t*>(ptr(s2 + i * 2, 2));
            if (ignoreCase) {
                c1 = static_cast<uint16_t>(std::towlower(c1));
                c2 = static_cast<uint16_t>(std::towlower(c2));
            }
            if (c1 != c2) {
                cmp = (c1 < c2) ? -1 : 1;
                break;
            }
        }
        if (cmp == 0) cmp = (len1 < len2) ? -1 : (len1 > len2) ? 1 : 0;
    }

    uint32_t res = (cmp < 0) ? 1 : (cmp > 0) ? 3 : 2;
    ret(res, 6);
}

// GetStringTypeA(LCID Locale, DWORD dwInfoType, LPCSTR lpSrcStr, int cchSrc, LPWORD lpCharType)
inline void getStringTypeA() {
    uint32_t src = arg(2);
    int32_t cch = static_cast<int32_t>(arg(3));
    uint32_t out = arg(4);

    if (!src || !out || !memory) {
        ret(0, 5);
        return;
    }

    if (cch < 0) cch = static_cast<int32_t>(std::strlen(static_cast<const char*>(ptr(src, 1))));
    for (int32_t i = 0; i < cch; ++i) {
        unsigned char c = *static_cast<const unsigned char*>(ptr(src + i, 1));
        uint16_t mask = 0;
        if (std::isalpha(c)) mask |= 0x0001; // C1_ALPHA
        if (std::isupper(c)) mask |= 0x0002; // C1_UPPER
        if (std::islower(c)) mask |= 0x0004; // C1_LOWER
        if (std::isdigit(c)) mask |= 0x0008; // C1_DIGIT
        if (std::isspace(c)) mask |= 0x0010; // C1_SPACE
        if (std::ispunct(c)) mask |= 0x0020; // C1_PUNCT
        if (std::isxdigit(c)) mask |= 0x0100; // C1_XDIGIT
        std::memcpy(ptr(out + i * 2, 2), &mask, 2);
    }
    ret(1, 5);
}

// LCMapStringA(LCID Locale, DWORD dwMapFlags, LPCSTR lpSrcStr, int cchSrc, LPSTR lpDestStr, int cchDest)
inline void lcMapStringA() {
    uint32_t flags = arg(1);
    uint32_t src = arg(2);
    int32_t cchSrc = static_cast<int32_t>(arg(3));
    uint32_t dest = arg(4);
    int32_t cchDest = static_cast<int32_t>(arg(5));

    if (!src || !memory) {
        ret(0, 6);
        return;
    }

    if (cchSrc < 0) cchSrc = static_cast<int32_t>(std::strlen(static_cast<const char*>(ptr(src, 1))));
    if (cchDest == 0) {
        ret(cchSrc, 6);
        return;
    }

    int32_t toCopy = std::min(cchSrc, cchDest);
    for (int32_t i = 0; i < toCopy; ++i) {
        char c = *static_cast<const char*>(ptr(src + i, 1));
        if (flags & 0x00000100u) c = char(std::tolower(static_cast<unsigned char>(c))); // LCMAP_LOWERCASE
        else if (flags & 0x00000200u) c = char(std::toupper(static_cast<unsigned char>(c))); // LCMAP_UPPERCASE
        *static_cast<char*>(ptr(dest + i, 1)) = c;
    }
    ret(toCopy, 6);
}

// GetLocaleInfoA(LCID Locale, LCTYPE LCType, LPSTR lpLCData, int cchData)
inline void getLocaleInfoA() {
    uint32_t type = arg(1) & 0xffffu;
    uint32_t out = arg(2);
    int32_t cch = static_cast<int32_t>(arg(3));

    const char* val = "1";
    switch (type) {
        case 0x0001: val = "0409"; break;                   // LOCALE_ILANGUAGE
        case 0x0002: val = "English (United States)"; break;// LOCALE_SLANGUAGE
        case 0x0003: val = "en"; break;                     // LOCALE_SABBREVLANGNAME
        case 0x0004: val = "ENG"; break;                    // LOCALE_SNATIVELANGNAME
        case 0x0005: val = "1"; break;                      // LOCALE_ICOUNTRY
        case 0x0006: val = "United States"; break;          // LOCALE_SCOUNTRY
        case 0x0007: val = "USA"; break;                    // LOCALE_SABBREVCTRYNAME
        case 0x000E: val = "."; break;                      // LOCALE_SDECIMAL
        case 0x000F: val = ","; break;                      // LOCALE_STHOUSAND
        case 0x0010: val = "3;0"; break;                    // LOCALE_SGROUPING
        case 0x0014: val = "$"; break;                      // LOCALE_SCURRENCY
        case 0x0059: val = "en"; break;                     // LOCALE_SISO639LANGNAME
        case 0x005A: val = "US"; break;                     // LOCALE_SISO3166CTRYNAME
        default: val = "1"; break;
    }

    int len = static_cast<int>(std::strlen(val)) + 1;
    if (cch == 0) {
        ret(len, 4);
        return;
    }
    if (out && memory) {
        int copyLen = std::min(len, cch);
        std::memcpy(ptr(out, copyLen), val, copyLen);
    }
    ret(len, 4);
}

// GetLocaleInfoW(LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData)
inline void getLocaleInfoW() {
    uint32_t type = arg(1) & 0xffffu;
    uint32_t out = arg(2);
    int32_t cch = static_cast<int32_t>(arg(3));

    const wchar_t* val = L"1";
    switch (type) {
        case 0x0001: val = L"0409"; break;
        case 0x0002: val = L"English (United States)"; break;
        case 0x0003: val = L"en"; break;
        case 0x000E: val = L"."; break;
        case 0x000F: val = L","; break;
        default: val = L"1"; break;
    }

    int len = static_cast<int>(std::wcslen(val)) + 1;
    if (cch == 0) {
        ret(len, 4);
        return;
    }
    if (out && memory) {
        int copyLen = std::min(len, cch);
        for (int i = 0; i < copyLen; ++i) {
            uint16_t ch = static_cast<uint16_t>(val[i]);
            std::memcpy(ptr(out + i * 2, 2), &ch, 2);
        }
    }
    ret(len, 4);
}

// GetUserDefaultLCID()
inline void getUserDefaultLCID() {
    ret(0x0409u, 0); // en-US
}

// IsValidCodePage(UINT CodePage)
inline void isValidCodePage() {
    uint32_t cp = arg(0);
    bool valid = (cp == 0 || cp == 437 || cp == 1252 || cp == 65001 || cp == 28591);
    ret(valid ? 1 : 0, 1);
}

// IsValidLocale(LCID Locale, DWORD dwFlags)
inline void isValidLocale() {
    ret(1, 2);
}

// EnumSystemLocalesA(LOCALE_ENUMPROCA lpLocaleEnumProc, DWORD dwFlags)
inline void enumSystemLocalesA() {
    uint32_t callback = arg(0);
    if (callback && memory) {
        uint32_t strAddr = allocate(16);
        std::memcpy(ptr(strAddr, 9), "00000409", 9);
        callGuest(callback, {strAddr});
        heaps->free(GuestHeaps::process, strAddr);
    }
    ret(1, 2);
}

// SetStdHandle(DWORD nStdHandle, HANDLE hHandle)
inline void setStdHandle() {
    ret(1, 2);
}

// SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
inline void setEnvironmentVariableA() {
    uint32_t namePtr = arg(0);
    uint32_t valPtr = arg(1);
    if (namePtr && memory) {
        std::string name = guestString(namePtr);
        if (valPtr) {
            std::string val = guestString(valPtr);
            setenv(name.c_str(), val.c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }
    ret(1, 2);
}

// TerminateThread(HANDLE hThread, DWORD dwExitCode)
inline void terminateThread() {
    ret(1, 2);
}

// TerminateProcess(HANDLE hProcess, UINT uExitCode)
inline void terminateProcess() {
    uint32_t exitCode = arg(1);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "NFSU2", "Guest requested TerminateProcess with code %u", exitCode);
    ret(1, 2);
}

// UnhandledExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo)
inline void unhandledExceptionFilter() {
    ret(0, 1); // EXCEPTION_CONTINUE_SEARCH
}

// RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments)
inline void raiseException() {
    uint32_t code = arg(0);
    NFS_RUNTIME_LOG(ANDROID_LOG_WARN, "NFSU2", "Guest RaiseException: code=%08x from %08x", code, g_cur_func);
    ret(0, 4);
}

// RtlUnwind(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue)
inline void rtlUnwind() {
    ret(0, 4);
}

// DebugBreak()
inline void debugBreak() {
    NFS_RUNTIME_LOG(ANDROID_LOG_WARN, "NFSU2", "Guest DebugBreak ignored from %08x", g_cur_func);
    ret(0, 0);
}

// Dynamic symbol resolver for core Win32 functions
inline recomp_func_t resolveCoreWin32Function(const std::string& dll, const std::string& name) {
    if (dll == "user32.dll" && name == "wsprintfA") return winFormatCdeclA;
    if (dll == "advapi32.dll" && name == "RegCreateKeyA") return regCreateKeyA;
    if (dll == "kernel32.dll") {
        if (name == "VirtualQuery") return virtualQuery;
        if (name == "IsBadCodePtr") return isBadCodePtr;
        if (name == "CompareStringA") return compareStringA;
        if (name == "CompareStringW") return compareStringW;
        if (name == "GetStringTypeA") return getStringTypeA;
        if (name == "LCMapStringA") return lcMapStringA;
        if (name == "GetLocaleInfoA") return getLocaleInfoA;
        if (name == "GetLocaleInfoW") return getLocaleInfoW;
        if (name == "GetUserDefaultLCID") return getUserDefaultLCID;
        if (name == "IsValidCodePage") return isValidCodePage;
        if (name == "IsValidLocale") return isValidLocale;
        if (name == "EnumSystemLocalesA") return enumSystemLocalesA;
        if (name == "SetStdHandle") return setStdHandle;
        if (name == "SetEnvironmentVariableA") return setEnvironmentVariableA;
        if (name == "TerminateThread") return terminateThread;
        if (name == "TerminateProcess") return terminateProcess;
        if (name == "UnhandledExceptionFilter") return unhandledExceptionFilter;
        if (name == "RaiseException") return raiseException;
        if (name == "RtlUnwind") return rtlUnwind;
        if (name == "DebugBreak") return debugBreak;
    }
    return nullptr;
}

#endif // GUEST_WIN32_CORE_INC
