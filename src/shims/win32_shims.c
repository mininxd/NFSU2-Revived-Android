/*
 * win32_shims.c - the few imports that cannot go straight to Windows.
 *
 * Everything else in SPEED2.EXE's import table is bound to the real function
 * by native32. A shim exists only where the host process differs from the
 * original one:
 *
 *   - the "main module" is the host exe, not SPEED2.EXE, so module-handle and
 *     module-path queries for NULL must answer with the guest image.
 *
 * Shims run inside the lifted model: arguments at g_esp+4.., result in g_eax,
 * and they pop their own stdcall arguments (g_esp += 4 + 4*n).
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include "recomp_types.h"
#include "native32.h"
#include "nfs_runtime.h"
#include "nfs_log.h"

#define ARG(i) (*(uint32_t *)(uintptr_t)(g_esp + 4 + 4 * (i)))
#define RET(v, nargs) do { g_eax = (uint32_t)(v); g_esp += 4 + 4 * (nargs); } while (0)

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) uint32_t rust_shim_get_module_handle_a(const char *name);
__attribute__((weak)) uint32_t rust_shim_find_resource(size_t base, uint32_t name, uint32_t rtype);
__attribute__((weak)) uint32_t rust_shim_load_resource(size_t base, uint32_t hrsrc);
__attribute__((weak)) uint32_t rust_shim_sizeof_resource(uint32_t hrsrc);
#endif

static int is_guest_name(const char *s) {
    const char *b = strrchr(s, '\\'), *c = strrchr(s, '/');
    if (c > b) b = c;
    b = b ? b + 1 : s;
    return !_stricmp(b, "SPEED2.EXE") || !_stricmp(b, "SPEED2");
}

/* HMODULE GetModuleHandleA(LPCSTR name) */
static void shim_GetModuleHandleA(void) {
    const char *name = (const char *)(uintptr_t)ARG(0);
#if defined(__GNUC__) || defined(__clang__)
    if (rust_shim_get_module_handle_a) {
        uint32_t rust_r = rust_shim_get_module_handle_a(name);
        if (rust_r) {
            NFS_LOG(WIN32, "GetModuleHandleA(%s) [Rust] -> %08X", name ? name : "NULL", rust_r);
            RET(rust_r, 1);
            return;
        }
    }
#endif
    uint32_t r;
    if (!name || is_guest_name(name)) r = NFS_GUEST_BASE;
    else r = (uint32_t)(uintptr_t)GetModuleHandleA(name);
    NFS_LOG(WIN32, "GetModuleHandleA(%s) -> %08X", name ? name : "NULL", r);
    RET(r, 1);
}

/* DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD n) */
static void shim_GetModuleFileNameA(void) {
    uint32_t m = ARG(0);
    char *buf = (char *)(uintptr_t)ARG(1);
    DWORD n = ARG(2), r;
    if (m == 0 || m == NFS_GUEST_BASE) {
        size_t len = strlen(g_nfs_exe_path);
        if (n == 0) { r = 0; }
        else if (len >= n) { memcpy(buf, g_nfs_exe_path, n - 1); buf[n - 1] = 0; r = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); }
        else { memcpy(buf, g_nfs_exe_path, len + 1); r = (DWORD)len; }
    } else {
        r = GetModuleFileNameA((HMODULE)(uintptr_t)m, buf, n);
    }
    NFS_LOG(WIN32, "GetModuleFileNameA(%08X) -> \"%s\"", m, r ? buf : "");
    RET(r, 3);
}


/* ---- resources of the guest image ----
 *
 * Windows only knows modules its loader mapped; SPEED2.EXE was mapped by
 * image_loader, so FindResourceA(NULL or 0x400000, ...) searched the host exe
 * and failed. NFSU2 keeps its 23 D3DX effect files (IDI_WORLD_FX, IDI_CAR_FX,
 * ...) as RT_RCDATA resources and dereferenced the NULL. These walk the guest's
 * own resource directory. An HRSRC is the guest VA of the
 * IMAGE_RESOURCE_DATA_ENTRY, an HGLOBAL the guest VA of the data. */
static int is_guest_module(uint32_t m) { return m == 0 || m == NFS_GUEST_BASE; }
static int in_guest(uint32_t va) { return native32_in_guest(va); }

static IMAGE_RESOURCE_DIRECTORY *rsrc_root(void) {
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(uintptr_t)(NFS_GUEST_BASE +
        ((IMAGE_DOS_HEADER *)(uintptr_t)NFS_GUEST_BASE)->e_lfanew);
    IMAGE_DATA_DIRECTORY d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    return d.VirtualAddress ? (IMAGE_RESOURCE_DIRECTORY *)(uintptr_t)(NFS_GUEST_BASE + d.VirtualAddress) : NULL;
}

/* key: an integer id (< 0x10000), or a wide string (no '#' form here). */
static IMAGE_RESOURCE_DIRECTORY_ENTRY *rsrc_find(IMAGE_RESOURCE_DIRECTORY *root,
                                                 IMAGE_RESOURCE_DIRECTORY *dir,
                                                 uintptr_t id, const wchar_t *wname) {
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    int n = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    for (int i = 0; i < n; i++, e++) {
        if (wname) {
            if (!e->NameIsString) continue;
            IMAGE_RESOURCE_DIR_STRING_U *str = (IMAGE_RESOURCE_DIR_STRING_U *)((BYTE *)root + e->NameOffset);
            if ((int)wcslen(wname) == str->Length && !_wcsnicmp(wname, str->NameString, str->Length)) return e;
        } else if (!e->NameIsString && e->Id == id) {
            return e;
        }
    }
    return NULL;
}

/* Turn a FindResource name/type argument into (id, wide name). */
static void rsrc_key(uint32_t arg, int wide, uintptr_t *id, wchar_t *buf, const wchar_t **wname) {
    *wname = NULL; *id = 0;
    if (arg < 0x10000) { *id = arg; return; }
    if (wide) wcsncpy(buf, (const wchar_t *)(uintptr_t)arg, 255);
    else MultiByteToWideChar(CP_ACP, 0, (const char *)(uintptr_t)arg, -1, buf, 256);
    buf[255] = 0;
    if (buf[0] == L'#') { *id = (uintptr_t)_wtoi(buf + 1); return; }
    *wname = buf;
}

static uint32_t guest_find_resource(uint32_t name, uint32_t type, int wide) {
#if defined(__GNUC__) || defined(__clang__)
    if (rust_shim_find_resource && !wide) {
        uint32_t rust_r = rust_shim_find_resource(NFS_GUEST_BASE, name, type);
        if (rust_r) return rust_r;
    }
#endif
    IMAGE_RESOURCE_DIRECTORY *root = rsrc_root();
    if (!root) return 0;
    wchar_t nb[256], tb[256];
    const wchar_t *nw, *tw;
    uintptr_t nid, tid;
    rsrc_key(name, wide, &nid, nb, &nw);
    rsrc_key(type, wide, &tid, tb, &tw);
    IMAGE_RESOURCE_DIRECTORY_ENTRY *t = rsrc_find(root, root, tid, tw);
    if (!t || !t->DataIsDirectory) return 0;
    IMAGE_RESOURCE_DIRECTORY *td = (IMAGE_RESOURCE_DIRECTORY *)((BYTE *)root + t->OffsetToDirectory);
    IMAGE_RESOURCE_DIRECTORY_ENTRY *nm = rsrc_find(root, td, nid, nw);
    if (!nm || !nm->DataIsDirectory) return 0;
    IMAGE_RESOURCE_DIRECTORY *ld = (IMAGE_RESOURCE_DIRECTORY *)((BYTE *)root + nm->OffsetToDirectory);
    if (ld->NumberOfNamedEntries + ld->NumberOfIdEntries == 0) return 0;
    IMAGE_RESOURCE_DIRECTORY_ENTRY *lang = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(ld + 1);   /* first language */
    if (lang->DataIsDirectory) return 0;
    return (uint32_t)(uintptr_t)((BYTE *)root + lang->OffsetToData);
}

static void log_res(const char *fn, uint32_t name, uint32_t type, int wide, uint32_t r) {
    char n[128] = "", t[32] = "";
    if (name < 0x10000) snprintf(n, sizeof n, "#%u", name);
    else if (wide) WideCharToMultiByte(CP_ACP, 0, (const wchar_t *)(uintptr_t)name, -1, n, sizeof n, NULL, NULL);
    else snprintf(n, sizeof n, "%s", (const char *)(uintptr_t)name);
    if (type < 0x10000) snprintf(t, sizeof t, "#%u", type); else snprintf(t, sizeof t, "(named)");
    NFS_LOG(FILE, "%s(guest, %s, %s) -> %08X", fn, n, t, r);
}

/* HRSRC FindResourceA(HMODULE, LPCSTR name, LPCSTR type) */
static void shim_FindResourceA(void) {
    uint32_t m = ARG(0), r;
    if (is_guest_module(m)) { r = guest_find_resource(ARG(1), ARG(2), 0); log_res("FindResourceA", ARG(1), ARG(2), 0, r); if (!r) SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND); }
    else r = (uint32_t)(uintptr_t)FindResourceA((HMODULE)(uintptr_t)m, (LPCSTR)(uintptr_t)ARG(1), (LPCSTR)(uintptr_t)ARG(2));
    RET(r, 3);
}
/* HRSRC FindResourceW(HMODULE, LPCWSTR name, LPCWSTR type) */
static void shim_FindResourceW(void) {
    uint32_t m = ARG(0), r;
    if (is_guest_module(m)) { r = guest_find_resource(ARG(1), ARG(2), 1); log_res("FindResourceW", ARG(1), ARG(2), 1, r); if (!r) SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND); }
    else r = (uint32_t)(uintptr_t)FindResourceW((HMODULE)(uintptr_t)m, (LPCWSTR)(uintptr_t)ARG(1), (LPCWSTR)(uintptr_t)ARG(2));
    RET(r, 3);
}
/* HGLOBAL LoadResource(HMODULE, HRSRC) */
static void shim_LoadResource(void) {
    uint32_t m = ARG(0), h = ARG(1), r;
#if defined(__GNUC__) || defined(__clang__)
    if (rust_shim_load_resource && in_guest(h)) {
        uint32_t rust_r = rust_shim_load_resource(NFS_GUEST_BASE, h);
        if (rust_r) { RET(rust_r, 2); return; }
    }
#endif
    if (in_guest(h)) r = NFS_GUEST_BASE + ((IMAGE_RESOURCE_DATA_ENTRY *)(uintptr_t)h)->OffsetToData;
    else r = (uint32_t)(uintptr_t)LoadResource((HMODULE)(uintptr_t)m, (HRSRC)(uintptr_t)h);
    RET(r, 2);
}
/* DWORD SizeofResource(HMODULE, HRSRC) */
static void shim_SizeofResource(void) {
    uint32_t m = ARG(0), h = ARG(1), r;
#if defined(__GNUC__) || defined(__clang__)
    if (rust_shim_sizeof_resource && in_guest(h)) {
        uint32_t rust_r = rust_shim_sizeof_resource(h);
        if (rust_r) { RET(rust_r, 2); return; }
    }
#endif
    if (in_guest(h)) r = ((IMAGE_RESOURCE_DATA_ENTRY *)(uintptr_t)h)->Size;
    else r = SizeofResource((HMODULE)(uintptr_t)m, (HRSRC)(uintptr_t)h);
    RET(r, 2);
}
/* LPVOID LockResource(HGLOBAL) */
static void shim_LockResource(void) {
    uint32_t h = ARG(0), r;
    r = in_guest(h) ? h : (uint32_t)(uintptr_t)LockResource((HGLOBAL)(uintptr_t)h);
    RET(r, 1);
}


/* ---- HINSTANCE validation ----
 * APIs that check an HINSTANCE against the loader's module list reject the
 * guest image (Windows did not load it). Hand them the host module instead;
 * the guest never sees the difference. */
typedef HRESULT (WINAPI *di8create_t)(HINSTANCE, DWORD, const void *, void **, void *);
/* HRESULT DirectInput8Create(HINSTANCE, DWORD ver, REFIID, LPVOID *out, LPUNKNOWN outer) */
static void shim_DirectInput8Create(void) {
    static di8create_t real;
    if (!real) real = (di8create_t)GetProcAddress(LoadLibraryA("dinput8.dll"), "DirectInput8Create");
    HINSTANCE h = (HINSTANCE)(uintptr_t)ARG(0);
    if (is_guest_module(ARG(0))) h = GetModuleHandleA(NULL);
    HRESULT hr = real(h, ARG(1), (const void *)(uintptr_t)ARG(2), (void **)(uintptr_t)ARG(3),
                      (void *)(uintptr_t)ARG(4));
    if (hr == 0 && ARG(3)) { extern void nfs_dinput_register(uint32_t); nfs_dinput_register(*(uint32_t *)(uintptr_t)ARG(3)); }
    if (hr == 0 && ARG(3)) { extern void nfs_dinput_hook(uint32_t); nfs_dinput_hook(*(uint32_t *)(uintptr_t)ARG(3)); }
    NFS_LOG(WIN32, "DirectInput8Create(hinst %08X -> %p, ver %X) -> %08lX, obj %08X", ARG(0), h, ARG(1),
            (unsigned long)hr, ARG(3) ? *(uint32_t *)(uintptr_t)ARG(3) : 0);
    RET((uint32_t)hr, 5);
}


/* ---- windowed mode: keep a framed window ----
 * NFSU2 restyles its window for fullscreen (WS_POPUP, topmost) after the
 * device exists. In windowed mode the style is kept framed and movable, and
 * the extended style loses WS_EX_TOPMOST. Only windows of class "GameFrame". */
extern int g_nfs_windowed;
static int is_game_window(HWND h) {
    char c[32] = "";
    return h && GetClassNameA(h, c, sizeof c) && !strcmp(c, "GameFrame");
}
/* LONG SetWindowLongA(HWND, int index, LONG value) */
static void shim_SetWindowLongA(void) {
    HWND h = (HWND)(uintptr_t)ARG(0);
    int idx = (int)ARG(1);
    LONG v = (LONG)ARG(2);
    if (g_nfs_windowed && is_game_window(h)) {
        if (idx == GWL_STYLE) v = (LONG)((WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | (v & WS_VISIBLE));
        else if (idx == GWL_EXSTYLE) v &= ~WS_EX_TOPMOST;
    }
    RET((uint32_t)SetWindowLongA(h, idx, v), 3);
}


/* ---- file opens, timestamped (FILE log) ---- */
/* HANDLE CreateFileA(name, access, share, sa, disposition, flags, template) */
static void shim_CreateFileA(void) {
    static DWORD t0; if (!t0) t0 = GetTickCount();
    const char *name = (const char *)(uintptr_t)ARG(0);
    HANDLE h = CreateFileA(name, ARG(1), ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3), ARG(4), ARG(5),
                           (HANDLE)(uintptr_t)ARG(6));
    DWORD err = GetLastError();
    NFS_LOG(FILE, "+%6lu ms CreateFileA(\"%s\", %08X) -> %p%s", GetTickCount() - t0, name ? name : "(null)",
            ARG(1), h, h == INVALID_HANDLE_VALUE ? " (failed)" : "");
    SetLastError(err);
    RET((uint32_t)(uintptr_t)h, 7);
}


/* IDirect3D9 *Direct3DCreate9(UINT sdk) -- real call, then the vtable hooks
 * (windowed mode) are installed on the returned object. A shim rather than a
 * bridge post-hook so it also runs when the guest executes natively. */
static void shim_Direct3DCreate9(void) {
    typedef void *(WINAPI *create9_t)(UINT);
    static create9_t real;
    extern void nfs_d3d9_hook_object(uint32_t);
    if (!real) real = (create9_t)GetProcAddress(LoadLibraryA("d3d9.dll"), "Direct3DCreate9");
    void *d3d = real(ARG(0));
    NFS_LOG(D3D9, "Direct3DCreate9(%u) -> %p", ARG(0), d3d);
    if (d3d) nfs_d3d9_hook_object((uint32_t)(uintptr_t)d3d);
    RET((uint32_t)(uintptr_t)d3d, 1);
}

native32_shim_t g_nfs_shims[] = {
    { "GetModuleHandleA", shim_GetModuleHandleA },
    { "GetModuleFileNameA", shim_GetModuleFileNameA },
    { "FindResourceA", shim_FindResourceA },
    { "FindResourceW", shim_FindResourceW },
    { "LoadResource", shim_LoadResource },
    { "SizeofResource", shim_SizeofResource },
    { "LockResource", shim_LockResource },
    { "DirectInput8Create", shim_DirectInput8Create },
    { "SetWindowLongA", shim_SetWindowLongA },
    { "CreateFileA", shim_CreateFileA },
    { "Direct3DCreate9", shim_Direct3DCreate9 },
};
int g_nfs_nshims = sizeof g_nfs_shims / sizeof g_nfs_shims[0];
