#!/usr/bin/env python3
"""
NFSU2 Emulation & Library Progress Tracker
Generates an interactive SVG visualization and JSON configuration of the Win32/DirectX 9
translation surface and library implementation progress for NFSU2 Android/ARM64.

Inspired by AnyPS5 library progress tracking and hackch NFSU2 save-coverage.svg.
"""

import os
import sys
import json
import argparse
from datetime import datetime
from html import escape

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_PATH = os.path.join(ROOT_DIR, "library-progress.json")
SVG_PATH = os.path.join(ROOT_DIR, "library-progress.svg")

# Comprehensive database of Win32 and DirectX APIs required by SPEED2.EXE
DEFAULT_LIBRARIES = {
    "kernel32.dll": [
        # Memory & Heaps
        ("VirtualAlloc", "implemented", "Anonymous mmap heap allocation with 32-bit pointer bounds"),
        ("VirtualFree", "implemented", "Host munmap address unmapping"),
        ("VirtualProtect", "stub", "Permissive memory protection stub (PROT_READ|PROT_WRITE|PROT_EXEC)"),
        ("VirtualQuery", "missing", "Query memory address space region information"),
        ("HeapCreate", "implemented", "Guest heap manager pool creation"),
        ("HeapDestroy", "implemented", "Guest heap manager pool destruction"),
        ("HeapAlloc", "implemented", "Dynamic 8-byte aligned heap allocation"),
        ("HeapFree", "implemented", "Dynamic heap memory deallocation"),
        ("HeapReAlloc", "implemented", "Dynamic heap buffer resizing"),
        ("HeapSize", "implemented", "Allocation block size query"),
        ("GetProcessHeap", "implemented", "Returns process default heap handle (0x70000000)"),
        ("GlobalAlloc", "implemented", "Fixed/movable global memory allocation"),
        ("GlobalFree", "implemented", "Global memory block release"),
        ("IsBadReadPtr", "implemented", "Safe readable virtual address bounds check"),
        ("IsBadWritePtr", "implemented", "Safe writable virtual address bounds check"),
        ("IsBadCodePtr", "missing", "Check code pointer readability"),
        ("MapViewOfFile", "implemented", "Memory-mapped file view binder"),
        ("UnmapViewOfFile", "implemented", "Memory-mapped file view release"),
        ("CreateFileMappingA", "implemented", "File backing or anonymous page file mapping object"),
        
        # File System & I/O
        ("CreateFileA", "implemented", "Translates Windows paths to Android root with case-insensitive resolver"),
        ("CreateFileW", "implemented", "UTF-16 path translation to local file descriptor"),
        ("ReadFile", "implemented", "Synchronous file block reading"),
        ("WriteFile", "implemented", "Synchronous file block writing"),
        ("SetFilePointer", "implemented", "64-bit/32-bit seek with SEEK_SET, SEEK_CUR, SEEK_END"),
        ("SetEndOfFile", "implemented", "Truncates or extends file to current position"),
        ("FlushFileBuffers", "implemented", "Host fsync flush"),
        ("GetFileSize", "implemented", "Stat-based file length query"),
        ("GetFullPathNameA", "implemented", "Normalizes relative game paths"),
        ("GetLongPathNameA", "implemented", "Returns standardized long pathname"),
        ("FindFirstFileA", "implemented", "Directory search traversal with wildcard matching"),
        ("FindNextFileA", "implemented", "Iterates directory entry enumeration"),
        ("FindClose", "implemented", "Closes search handle"),
        ("DeleteFileA", "implemented", "Unlinks target file on storage"),
        ("MoveFileA", "implemented", "Renames or moves target file atomically"),
        ("CreateDirectoryA", "implemented", "Creates single or recursive directory path"),
        ("RemoveDirectoryA", "implemented", "Removes empty directory path"),
        ("GetCurrentDirectoryA", "implemented", "Returns active game root directory"),
        ("GetDiskFreeSpaceA", "implemented", "Host statvfs free bytes calculation"),
        ("GetDiskFreeSpaceExA", "implemented", "64-bit free space for large Android storage volumes"),
        ("GetDriveTypeA", "implemented", "Returns DRIVE_FIXED (hard drive)"),
        ("GetLogicalDrives", "implemented", "Bitmask containing drive C: and D:"),
        ("GetFileType", "implemented", "Returns FILE_TYPE_DISK for regular files"),
        ("SetStdHandle", "missing", "Redirects standard handles"),
        ("GetStdHandle", "implemented", "Returns guest standard input/output/error descriptors"),

        # Threads & Synchronization
        ("CreateThread", "implemented", "Forks native POSIX host thread with guest stack allocation"),
        ("ExitThread", "implemented", "Terminates guest worker thread cleanly"),
        ("TerminateThread", "implemented", "Forces worker thread termination"),
        ("SuspendThread", "implemented", "Pauses guest worker execution"),
        ("ResumeThread", "implemented", "Resumes suspended guest worker execution"),
        ("GetExitCodeThread", "implemented", "Queries thread termination code"),
        ("GetCurrentThread", "implemented", "Pseudo-handle for current thread"),
        ("GetCurrentThreadId", "implemented", "Unique guest thread identifier"),
        ("SetThreadPriority", "implemented", "Maps Win32 priority to nice levels"),
        ("GetThreadPriority", "implemented", "Returns mapped thread priority"),
        ("CreateMutexA", "implemented", "Recursive POSIX mutex with named lookup"),
        ("ReleaseMutex", "implemented", "Releases owned mutex"),
        ("CreateEventA", "implemented", "Manual/auto-reset conditional variable event"),
        ("SetEvent", "implemented", "Signals waiting threads on event"),
        ("ResetEvent", "implemented", "Clears event signal state"),
        ("WaitForSingleObject", "implemented", "Waits on mutex, event, or thread with timeout"),
        ("WaitForMultipleObjects", "implemented", "Wait-any or wait-all synchronization barrier"),
        ("InitializeCriticalSection", "implemented", "Fast user-mode lock structure initialization"),
        ("InitializeCriticalSectionAndSpinCount", "implemented", "Spin-count user-mode lock initialization"),
        ("EnterCriticalSection", "implemented", "Exclusive reentrant critical section acquisition"),
        ("LeaveCriticalSection", "implemented", "Critical section lock release"),
        ("DeleteCriticalSection", "implemented", "Cleans up critical section resources"),
        ("InterlockedExchange", "implemented", "Atomic 32-bit exchange"),
        ("InterlockedIncrement", "implemented", "Atomic 32-bit increment"),
        ("InterlockedDecrement", "implemented", "Atomic 32-bit decrement"),
        ("InterlockedCompareExchange", "implemented", "Atomic 32-bit compare-and-swap"),
        ("Sleep", "implemented", "Yields machine lock and sleeps host thread"),
        ("SleepEx", "implemented", "Alertable sleep with timeout"),

        # Process & Modules
        ("GetCurrentProcess", "implemented", "Pseudo-handle for process (0xFFFFFFFF)"),
        ("GetCurrentProcessId", "implemented", "Process ID query"),
        ("ExitProcess", "implemented", "Clean shutdown and lifecycle termination"),
        ("TerminateProcess", "implemented", "Forced process kill"),
        ("GetPriorityClass", "implemented", "Returns NORMAL_PRIORITY_CLASS"),
        ("SetPriorityClass", "implemented", "Sets process execution priority"),
        ("GetProcessAffinityMask", "implemented", "Topology affinity mask query"),
        ("SetProcessAffinityMask", "implemented", "Tuned CPU cluster affinity configuration"),
        ("GetCommandLineA", "implemented", "Returns SPEED2.EXE command line arguments"),
        ("GetStartupInfoA", "implemented", "Supplies STARTUPINFO structure for CRT bring-up"),
        ("GetModuleHandleA", "implemented", "Returns base address of SPEED2.EXE or loaded DLLs"),
        ("GetModuleHandleW", "implemented", "Wide-string module lookup"),
        ("GetModuleFileNameA", "implemented", "Full path to SPEED2.EXE on storage"),
        ("LoadLibraryA", "implemented", "Dynamic module lookup and lifted DLL binding"),
        ("FreeLibrary", "implemented", "Decrements module reference count"),
        ("GetProcAddress", "implemented", "Resolves exported guest symbols and HLE thunks"),
        ("CreateToolhelp32Snapshot", "implemented", "Captures running process/module list snapshot"),
        ("Process32First", "implemented", "First process entry in toolhelp snapshot"),
        ("Process32Next", "implemented", "Next process entry in toolhelp snapshot"),
        ("Module32First", "implemented", "First module entry in toolhelp snapshot"),
        ("Module32Next", "implemented", "Next module entry in toolhelp snapshot"),
        ("DuplicateHandle", "implemented", "Duplicates synchronization or file handle"),
        ("CloseHandle", "implemented", "Closes kernel handle"),
        ("SetHandleCount", "implemented", "Obsolete Win32 file limit helper (no-op)"),

        # Timing & Diagnostics
        ("GetTickCount", "implemented", "Millisecond counter since system boot"),
        ("QueryPerformanceCounter", "implemented", "Nanosecond steady clock performance counter"),
        ("QueryPerformanceFrequency", "implemented", "High-resolution timer frequency (10 MHz)"),
        ("GetSystemTimeAsFileTime", "implemented", "64-bit UTC timestamp in 100ns units"),
        ("FileTimeToSystemTime", "implemented", "Decomposes 64-bit filetime to calendar fields"),
        ("FileTimeToLocalFileTime", "implemented", "Adjusts UTC filetime to local timezone"),
        ("GetTimeZoneInformation", "implemented", "Supplies daylight saving and UTC bias"),
        ("GetSystemInfo", "implemented", "Page size (4KB) and processor architecture (x86)"),
        ("GetVersion", "implemented", "Windows XP SP2 version tuple (5.1.2600)"),
        ("GetVersionExA", "implemented", "OSVERSIONINFOA with XP SP2 identifier"),
        ("IsProcessorFeaturePresent", "implemented", "Reports MMX, SSE, SSE2, and CMPXCHG8B support"),
        ("IsDebuggerPresent", "implemented", "Returns false to bypass anti-debug assertions"),
        ("DebugBreak", "missing", "Hard breakpoint trap"),
        ("OutputDebugStringA", "implemented", "Redirects guest debug prints to Android logcat"),
        ("SetErrorMode", "implemented", "Suppresses Windows crash dialogs"),
        ("GetLastError", "implemented", "Thread-local error code retrieval"),
        ("SetLastError", "implemented", "Thread-local error code assignment"),
        ("TlsAlloc", "implemented", "Thread-local storage slot reservation"),
        ("TlsFree", "implemented", "Thread-local storage slot release"),
        ("TlsGetValue", "implemented", "Retrieves thread-local slot value"),
        ("TlsSetValue", "implemented", "Stores thread-local slot value"),

        # Strings & Localization
        ("MultiByteToWideChar", "implemented", "Converts ASCII/CP1252 to UTF-16"),
        ("WideCharToMultiByte", "implemented", "Converts UTF-16 to ASCII/CP1252"),
        ("lstrcmpiA", "implemented", "Case-insensitive ASCII string comparison"),
        ("CompareStringA", "missing", "Locale-sensitive string comparison"),
        ("CompareStringW", "missing", "Locale-sensitive wide string comparison"),
        ("GetStringTypeA", "missing", "Character type classification (ANSI)"),
        ("GetStringTypeW", "implemented", "Character type classification (Unicode)"),
        ("LCMapStringA", "missing", "Locale-specific string transformation (ANSI)"),
        ("LCMapStringW", "implemented", "Locale-specific string transformation (Unicode)"),
        ("GetCPInfo", "implemented", "Code page character width metrics"),
        ("GetACP", "implemented", "Active ANSI code page (1252 Latin 1)"),
        ("GetOEMCP", "implemented", "Active OEM code page (437 US)"),
        ("IsValidCodePage", "missing", "Code page validation check"),
        ("IsValidLocale", "missing", "Locale validation check"),
        ("EnumSystemLocalesA", "missing", "Enumerates installed system locales"),
        ("GetLocaleInfoA", "missing", "Retrieves locale attributes (ANSI)"),
        ("GetLocaleInfoW", "missing", "Retrieves locale attributes (Unicode)"),
        ("GetUserDefaultLCID", "missing", "Default user locale ID"),
        ("GetThreadLocale", "implemented", "Returns thread locale ID (0x0409 US English)"),
        ("GetEnvironmentStrings", "implemented", "Retrieves process environment block"),
        ("GetEnvironmentStringsA", "implemented", "ANSI environment block retrieval"),
        ("GetEnvironmentStringsW", "implemented", "Unicode environment block retrieval"),
        ("FreeEnvironmentStringsA", "implemented", "Frees ANSI environment block"),
        ("FreeEnvironmentStringsW", "implemented", "Frees Unicode environment block"),
        ("SetEnvironmentVariableA", "missing", "Sets environment variable"),

        # Resources & Exceptions
        ("FindResourceA", "implemented", "Locates RT_RCDATA effect files in guest PE resource tree"),
        ("FindResourceW", "implemented", "Wide resource directory locator"),
        ("LoadResource", "implemented", "Returns memory pointer to resource payload"),
        ("LockResource", "implemented", "Accesses resource data buffer in memory"),
        ("SizeofResource", "implemented", "Returns byte size of resource payload"),
        ("SetUnhandledExceptionFilter", "implemented", "Installs top-level exception handler"),
        ("UnhandledExceptionFilter", "missing", "Default crash handler filter"),
        ("RaiseException", "missing", "Invokes software exception"),
        ("RtlUnwind", "missing", "Unwinds call stack frames"),
        ("EncodePointer", "implemented", "Pointer obfuscation identity pass-through"),
        ("DecodePointer", "implemented", "Pointer deobfuscation identity pass-through"),

        # Serial & Comm Stubs (unused in Android single-player / WiFi roam)
        ("SetupComm", "missing", "Communications device buffer configuration"),
        ("SetCommState", "missing", "Serial port baud rate and parity setup"),
        ("GetCommState", "missing", "Serial port configuration query"),
        ("SetCommConfig", "missing", "Communications configuration setter"),
        ("GetCommConfig", "missing", "Communications configuration getter"),
        ("SetCommTimeouts", "missing", "Serial port timeout parameters"),
        ("SetCommMask", "missing", "Serial event mask configuration"),
        ("WaitCommEvent", "missing", "Waits for serial port communications event"),
        ("PurgeComm", "missing", "Discards communications buffer data"),
        ("GetOverlappedResult", "missing", "Overlapped asynchronous I/O completion status")
    ],

    "user32.dll": [
        ("CreateWindowExA", "implemented", "Creates game frame window and sets up message queue"),
        ("DestroyWindow", "implemented", "Dispatches WM_DESTROY/WM_NCDESTROY and cleans up window"),
        ("ShowWindow", "implemented", "Controls window visibility and posts WM_SHOWWINDOW"),
        ("UpdateWindow", "implemented", "Directly dispatches WM_PAINT to window proc"),
        ("SetWindowPos", "implemented", "Positions and sizes window surface with WM_WINDOWPOSCHANGED"),
        ("AdjustWindowRect", "implemented", "Calculates required client window dimensions"),
        ("GetWindowRect", "implemented", "Returns window bounding screen coordinates"),
        ("GetClientRect", "implemented", "Returns inner rendering viewport dimensions"),
        ("GetWindowLongA", "implemented", "Queries window procedure, style, exstyle, and user data"),
        ("SetWindowLongA", "implemented", "Modifies window procedure, style, or instance data"),
        ("GetDesktopWindow", "implemented", "Returns virtual desktop handle (0xF0000001)"),
        ("IsIconic", "implemented", "Always reports false (game window is never minimized)"),
        ("RegisterClassExA", "implemented", "Registers WNDCLASSEXA with atom identifier and procedure"),
        ("UnregisterClassA", "implemented", "Unregisters window class atom"),
        ("DefWindowProcA", "implemented", "Default window message handling (WM_NCCREATE, WM_ERASEBKGND)"),
        ("GetMessageA", "implemented", "Blocks until message arrives in queue"),
        ("PeekMessageA", "implemented", "Non-blocking check for queued input or window messages"),
        ("TranslateMessage", "implemented", "Translates virtual keystrokes to WM_CHAR messages"),
        ("DispatchMessageA", "implemented", "Passes queued message to registered window procedure"),
        ("SendMessageA", "implemented", "Synchronously invokes window procedure with message"),
        ("PostMessageA", "implemented", "Enqueues asynchronous message to target window"),
        ("PostThreadMessageA", "implemented", "Enqueues message directly to thread queue"),
        ("PostQuitMessage", "implemented", "Posts WM_QUIT to trigger message loop exit"),
        ("SetActiveWindow", "implemented", "Activates target window and dispatches WM_ACTIVATE"),
        ("GetActiveWindow", "implemented", "Returns currently active window handle"),
        ("SetForegroundWindow", "implemented", "Brings window to front with WM_ACTIVATEAPP"),
        ("GetForegroundWindow", "implemented", "Returns foreground window handle"),
        ("SetFocus", "implemented", "Sets keyboard input focus"),
        ("GetFocus", "implemented", "Returns focused window handle"),
        ("SetCapture", "implemented", "Directs all mouse input to target window"),
        ("ReleaseCapture", "implemented", "Releases mouse capture"),
        ("GetCursorPos", "implemented", "Returns mouse pointer coordinates mapped to touch"),
        ("SetCursor", "implemented", "Sets active cursor handle"),
        ("ShowCursor", "implemented", "Adjusts cursor visibility counter"),
        ("LoadCursorA", "implemented", "Loads standard IDC_ARROW cursor handle"),
        ("LoadIconA", "implemented", "Loads standard IDI_APPLICATION icon handle"),
        ("GetSystemMetrics", "implemented", "Screen resolution, SM_CXSCREEN, SM_CYSCREEN, borders"),
        ("SetRect", "implemented", "Fills RECT structure coordinates"),
        ("BeginPaint", "implemented", "Prepares window for drawing and returns paint DC"),
        ("EndPaint", "implemented", "Marks window drawing region as validated"),
        ("wvsprintfA", "implemented", "Formatted string printing using stdarg list"),
        ("wsprintfA", "missing", "Formatted string printing with variable arguments"),
        ("MessageBoxA", "stub", "Tester notification stub; answers IDOK and logs to logcat")
    ],

    "d3d9.dll": [
        ("Direct3DCreate9", "implemented", "Initializes DXVK D3D9 bridge with Vulkan backend"),
        ("IDirect3D9::CreateDevice", "implemented", "Creates native Vulkan swapchain and D3D9 device"),
        ("IDirect3D9::GetAdapterCount", "implemented", "Enumerates primary graphics adapter"),
        ("IDirect3D9::GetAdapterDisplayMode", "implemented", "Queries Android display resolution and refresh rate"),
        ("IDirect3D9::GetDeviceCaps", "implemented", "Reports shader model 2.0/3.0 capabilities"),
        ("IDirect3D9::CheckDeviceFormat", "implemented", "Validates D3DFMT texture support on Vulkan"),
        ("IDirect3D9::CheckDeviceType", "implemented", "Confirms HAL hardware acceleration"),
        ("IDirect3D9::CheckDepthStencilMatch", "implemented", "Verifies D24S8 depth-stencil compatibility"),
        ("IDirect3DDevice9::Present", "implemented", "VK swapchain presentation with frame pacer and ADPF"),
        ("IDirect3DDevice9::BeginScene", "implemented", "Opens frame render command buffer"),
        ("IDirect3DDevice9::EndScene", "implemented", "Submits draw commands to Vulkan queue"),
        ("IDirect3DDevice9::Clear", "implemented", "Clears color, depth, and stencil surfaces"),
        ("IDirect3DDevice9::SetRenderState", "implemented", "Translates D3D9 states (alpha test, blend, cull, z-test)"),
        ("IDirect3DDevice9::GetRenderState", "implemented", "Retrieves active pipeline state values"),
        ("IDirect3DDevice9::SetTexture", "implemented", "Binds 2D textures, cubemaps, and volume textures"),
        ("IDirect3DDevice9::SetSamplerState", "implemented", "Configures anisotropic filtering and texture address modes"),
        ("IDirect3DDevice9::SetStreamSource", "implemented", "Binds vertex buffer streams to input assembler"),
        ("IDirect3DDevice9::SetIndices", "implemented", "Binds 16-bit/32-bit index buffers"),
        ("IDirect3DDevice9::SetFVF", "implemented", "Fixed function vertex layout definition"),
        ("IDirect3DDevice9::DrawPrimitive", "implemented", "Draws non-indexed primitive lists and strips"),
        ("IDirect3DDevice9::DrawIndexedPrimitive", "implemented", "Draws indexed triangles and lines"),
        ("IDirect3DDevice9::DrawPrimitiveUP", "implemented", "Draws user-memory vertices"),
        ("IDirect3DDevice9::DrawIndexedPrimitiveUP", "implemented", "Draws indexed user-memory geometry"),
        ("IDirect3DDevice9::CreateTexture", "implemented", "Allocates Vulkan image with BC1/BC2/BC3 compression"),
        ("IDirect3DDevice9::CreateVertexBuffer", "implemented", "Allocates Vulkan device memory vertex buffer"),
        ("IDirect3DDevice9::CreateIndexBuffer", "implemented", "Allocates Vulkan device memory index buffer"),
        ("IDirect3DDevice9::CreateVertexDeclaration", "implemented", "Compiles D3DVERTEXELEMENT9 input layout"),
        ("IDirect3DDevice9::CreateVertexShader", "implemented", "Translates DXBC vertex shaders via DXVK SPIR-V compiler"),
        ("IDirect3DDevice9::CreatePixelShader", "implemented", "Translates DXBC pixel shaders via DXVK SPIR-V compiler"),
        ("IDirect3DDevice9::SetVertexShader", "implemented", "Binds active vertex shader pipeline"),
        ("IDirect3DDevice9::SetPixelShader", "implemented", "Binds active pixel shader pipeline"),
        ("IDirect3DDevice9::SetVertexShaderConstantF", "implemented", "Pushes 4-float matrix/vector vertex constants"),
        ("IDirect3DDevice9::SetPixelShaderConstantF", "implemented", "Pushes 4-float lighting/color pixel constants"),
        ("IDirect3DDevice9::Reset", "implemented", "Handles surface recreation and resolution changes"),
        ("IDirect3DDevice9::TestCooperativeLevel", "implemented", "Always reports D3D_OK; ignores device loss"),
        ("IDirect3DDevice9::GetBackBuffer", "implemented", "Retrieves swapchain color surface"),
        ("IDirect3DDevice9::SetRenderTarget", "implemented", "Binds off-screen render target for mirrors/glow"),
        ("IDirect3DDevice9::GetRenderTarget", "implemented", "Retrieves current active render target"),
        ("IDirect3DDevice9::SetDepthStencilSurface", "implemented", "Binds z-buffer surface"),
        ("IDirect3DDevice9::GetDepthStencilSurface", "implemented", "Retrieves current depth surface"),
        ("IDirect3DDevice9::SetViewport", "implemented", "Sets rasterizer viewport rectangle"),
        ("IDirect3DDevice9::GetViewport", "implemented", "Retrieves active rasterizer viewport")
    ],

    "dsound.dll": [
        ("DirectSoundCreate", "implemented", "Factory method initializing AAudio high-priority stream"),
        ("#1 (DirectSoundEnumerate)", "implemented", "Ordinal #1 returning primary sound device"),
        ("IDirectSound::CreateSoundBuffer", "implemented", "Creates primary or secondary PCM audio buffer"),
        ("IDirectSound::SetCooperativeLevel", "implemented", "Configures DSSCL_PRIORITY level"),
        ("IDirectSound::GetCaps", "implemented", "Reports 44.1kHz/48kHz stereo 16-bit PCM capabilities"),
        ("IDirectSound::SetSpeakerConfig", "implemented", "Configures stereo speaker output"),
        ("IDirectSound::GetSpeakerConfig", "implemented", "Returns DSSPEAKER_STEREO (4)"),
        ("IDirectSoundBuffer::Play", "implemented", "Starts buffer playback with looping support"),
        ("IDirectSoundBuffer::Stop", "implemented", "Halts buffer playback and voice mixer"),
        ("IDirectSoundBuffer::Lock", "implemented", "Locks cyclic ring buffer for writing guest samples"),
        ("IDirectSoundBuffer::Unlock", "implemented", "Publishes PCM audio data to lockless mixer"),
        ("IDirectSoundBuffer::SetCurrentPosition", "implemented", "Updates voice read cursor in ring buffer"),
        ("IDirectSoundBuffer::GetCurrentPosition", "implemented", "Reports accurate play and write cursors"),
        ("IDirectSoundBuffer::SetVolume", "implemented", "Millibel volume attenuation to linear gain"),
        ("IDirectSoundBuffer::GetVolume", "implemented", "Returns cached millibel volume level"),
        ("IDirectSoundBuffer::SetPan", "implemented", "Stereo balance and panning calculation"),
        ("IDirectSoundBuffer::GetPan", "implemented", "Returns cached stereo pan level"),
        ("IDirectSoundBuffer::SetFrequency", "implemented", "Resampling frequency adjustments (100Hz - 200kHz)"),
        ("IDirectSoundBuffer::GetFrequency", "implemented", "Queries playback sample rate"),
        ("IDirectSoundBuffer::GetStatus", "implemented", "Returns DSBSTATUS_PLAYING / DSBSTATUS_LOOPING"),
        ("IDirectSoundBuffer::Restore", "implemented", "No-op; software buffers are never lost by driver"),
        ("IDirectSoundBuffer::GetCaps", "implemented", "Queries buffer capacity and format flags"),
        ("IDirectSoundBuffer::SetFormat", "implemented", "Updates primary buffer format description")
    ],

    "dinput8.dll": [
        ("DirectInput8Create", "implemented", "Initializes DirectInput 8 subsystems and hooks"),
        ("IDirectInput8A::CreateDevice", "implemented", "Instantiates keyboard, mouse, or gamepad device"),
        ("IDirectInput8A::EnumDevices", "implemented", "Enumerates attached controllers and input pads"),
        ("IDirectInputDevice8A::SetDataFormat", "implemented", "Binds c_dfDIJoystick2, c_dfDIKeyboard, or mouse format"),
        ("IDirectInputDevice8A::SetCooperativeLevel", "implemented", "DISCL_FOREGROUND | DISCL_NONEXCLUSIVE"),
        ("IDirectInputDevice8A::Acquire", "implemented", "Acquires input device and enables touch/gamepad stream"),
        ("IDirectInputDevice8A::Unacquire", "implemented", "Releases device acquisition"),
        ("IDirectInputDevice8A::GetDeviceState", "implemented", "Polls axes, buttons, and keys into guest memory"),
        ("IDirectInputDevice8A::GetDeviceData", "implemented", "Buffered input event packet delivery"),
        ("IDirectInputDevice8A::Poll", "implemented", "Polls gamepad analog sticks, triggers, and D-pad"),
        ("IDirectInputDevice8A::SetProperty", "implemented", "Configures deadzones, saturation, and buffer size"),
        ("IDirectInputDevice8A::GetProperty", "implemented", "Queries device range and joystick capabilities"),
        ("IDirectInputDevice8A::EnumObjects", "implemented", "Enumerates axis and button controls"),
        ("IDirectInputDevice8A::CreateEffect", "stub", "Haptic force feedback effect stub"),
        ("IDirectInputDevice8A::SendForceFeedbackCommand", "stub", "Haptic vibration trigger stub")
    ],

    "ws2_32.dll": [
        ("WSAStartup", "implemented", "Initializes Windows Sockets 2.2 subsystem"),
        ("WSACleanup", "implemented", "Terminates Winsock library context"),
        ("WSAGetLastError", "implemented", "Translates POSIX errno to WSA error constants"),
        ("WSAIoctl", "implemented", "Socket I/O control command handling"),
        ("socket", "implemented", "Creates IPv4 TCP (SOCK_STREAM) or UDP (SOCK_DGRAM) socket"),
        ("closesocket", "implemented", "Closes socket file descriptor"),
        ("shutdown", "implemented", "Halts send and receive operations"),
        ("bind", "implemented", "Binds socket to local address and port"),
        ("listen", "implemented", "Places stream socket in listening state for LAN hosting"),
        ("accept", "implemented", "Accepts incoming multiplayer client connection"),
        ("connect", "implemented", "Connects to adhoc roaming peer or classic lobby"),
        ("send", "implemented", "Sends stream data packet"),
        ("sendto", "implemented", "Sends UDP datagram to broadcast or peer address"),
        ("recv", "implemented", "Receives stream data from socket"),
        ("recvfrom", "implemented", "Receives UDP datagram with remote address resolution"),
        ("select", "implemented", "Non-blocking multiplexed socket polling with timeout"),
        ("ioctlsocket", "implemented", "FIONBIO non-blocking mode configuration"),
        ("setsockopt", "implemented", "Configures SO_BROADCAST, SO_REUSEADDR, and TCP_NODELAY"),
        ("getsockname", "implemented", "Retrieves local socket IP and port"),
        ("getpeername", "implemented", "Retrieves remote connected peer address"),
        ("gethostname", "implemented", "Returns local Android hostname"),
        ("gethostbyname", "implemented", "DNS host resolution and address lookup")
    ],

    "winmm.dll": [
        ("timeGetTime", "implemented", "High-precision millisecond multimedia timer"),
        ("timeBeginPeriod", "implemented", "Timer resolution adjustment (1ms target)"),
        ("timeEndPeriod", "implemented", "Restores system timer resolution"),
        ("timeSetEvent", "implemented", "Schedules periodic worker timer callback"),
        ("timeKillEvent", "implemented", "Cancels scheduled timer event"),
        ("timeGetDevCaps", "implemented", "Reports minimum (1ms) and maximum (1000000ms) timer resolution")
    ],

    "advapi32.dll": [
        ("RegOpenKeyA", "implemented", "Opens registry key for options and career saves"),
        ("RegOpenKeyExA", "implemented", "Opens registry key with access rights"),
        ("RegCloseKey", "implemented", "Flushes and closes registry key handle"),
        ("RegQueryValueExA", "implemented", "Reads profile options, language, and install path"),
        ("RegSetValueExA", "implemented", "Writes updated game options and resolution preferences"),
        ("RegCreateKeyA", "missing", "Creates new registry key entry")
    ],

    "gdi32.dll": [
        ("DeleteObject", "implemented", "Frees GDI bitmap or font object"),
        ("SetTextColor", "missing", "Sets device context text color"),
        ("SetBkColor", "missing", "Sets device context background color"),
        ("SetBkMode", "missing", "Sets background mix mode"),
        ("CreateCompatibleDC", "missing", "Creates memory device context"),
        ("DeleteDC", "missing", "Deletes device context"),
        ("CreateFontA", "missing", "Creates logical font"),
        ("CreateBitmap", "missing", "Creates bitmap surface"),
        ("ExtTextOutA", "missing", "Draws text using font"),
        ("SelectObject", "missing", "Selects object into DC"),
        ("BitBlt", "missing", "Bit-block transfer between DCs"),
        ("GetPixel", "missing", "Retrieves RGB color value of pixel")
    ],

    "tapi32.dll": [
        ("lineInitialize", "stub", "Telephony API stub for legacy modem dialer"),
        ("lineShutdown", "stub", "Telephony API shutdown stub"),
        ("lineOpen", "missing", "Opens telephony line device"),
        ("lineClose", "missing", "Closes telephony line device"),
        ("lineGetDevCaps", "missing", "Telephony line device capabilities"),
        ("lineGetID", "missing", "Telephony device identifier query"),
        ("lineNegotiateAPIVersion", "missing", "Telephony version negotiation"),
        ("lineMakeCall", "missing", "Telephony outgoing call dialer"),
        ("lineAnswer", "missing", "Telephony incoming call answer")
    ],

    "shfolder.dll": [
        ("SHGetFolderPathA", "implemented", "Maps CSIDL_LOCAL_APPDATA to /sdcard/NFSU2")
    ],

    "shell32.dll": [
        ("ShellExecuteA", "implemented", "Rust-backed ShellExecute external launcher & URL dispatcher")
    ],

    "netapi32.dll": [
        ("Netbios", "implemented", "Rust-backed Netbios LANA enumeration and MAC query"),
        ("NetApiBufferFree", "implemented", "Rust-backed NetApi buffer deallocation"),
        ("NetWkstaGetInfo", "implemented", "Rust-backed Workstation info query"),
        ("NetGetJoinInformation", "implemented", "Rust-backed Workgroup join status query")
    ],

    "ddraw.dll": [
        ("DirectDrawCreate", "implemented", "Rust-backed DirectDraw device & mode enumeration")
    ]
}

def load_data():
    if os.path.exists(JSON_PATH):
        try:
            with open(JSON_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            print(f"Warning: Failed loading existing {JSON_PATH}: {e}")
    
    # Construct initial JSON model
    data = {
        "title": "NFSU2 Win32 & DirectX 9 Translation Progress",
        "subtitle": "SPEED2.EXE ARM64 / Android HLE Surface Coverage",
        "description": "Library implementation status and function coverage for Need for Speed: Underground 2 recompilation.",
        "last_updated": datetime.utcnow().strftime("%Y-%m-%d %H:%M:%SZ"),
        "libraries": {}
    }
    for lib, funcs in DEFAULT_LIBRARIES.items():
        data["libraries"][lib] = [
            {"name": name, "status": status, "description": desc}
            for name, status, desc in funcs
        ]
    save_data(data)
    return data

def save_data(data):
    data["last_updated"] = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%SZ")
    with open(JSON_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print(f"Saved configuration database to: {JSON_PATH}")

def calculate_stats(data):
    total = 0
    implemented = 0
    stubbed = 0
    missing = 0
    lib_stats = {}

    for lib, funcs in data["libraries"].items():
        l_tot = len(funcs)
        l_imp = sum(1 for f in funcs if f["status"] == "implemented")
        l_stb = sum(1 for f in funcs if f["status"] == "stub")
        l_mis = sum(1 for f in funcs if f["status"] == "missing")
        total += l_tot
        implemented += l_imp
        stubbed += l_stb
        missing += l_mis
        lib_stats[lib] = {
            "total": l_tot,
            "implemented": l_imp,
            "stubbed": l_stb,
            "missing": l_mis,
            "percent": (l_imp + l_stb * 0.5) / l_tot * 100.0 if l_tot else 0.0
        }

    overall_pct = (implemented + stubbed * 0.5) / total * 100.0 if total else 0.0
    return {
        "total": total,
        "implemented": implemented,
        "stubbed": stubbed,
        "missing": missing,
        "percent": overall_pct,
        "libraries": lib_stats
    }

def generate_svg(data, output_path=SVG_PATH):
    stats = calculate_stats(data)
    
    WIDTH = 960
    # Colors matching hackch NFSU2 save-coverage.svg and dark theme
    BG_COLOR = "#212830"
    CARD_BG = "#1e293b"
    BORDER_COLOR = "#334155"
    COLOR_IMP = "#10b981"   # Emerald Green
    COLOR_STUB = "#f59e0b"  # Amber Orange
    COLOR_MISS = "#475569"  # Slate Grey
    COLOR_ACCENT = "#38bdf8"# Sky Cyan
    TEXT_MAIN = "#f8fafc"
    TEXT_MUTED = "#94a3b8"
    TEXT_DIM = "#64748b"

    elements = []
    
    # Outer Card & Background
    elements.append(f'<rect width="100%" height="100%" fill="{BG_COLOR}" rx="12"/>')
    elements.append(f'<rect x="1" y="1" width="{WIDTH-2}" height="100%" fill="none" stroke="{BORDER_COLOR}" stroke-width="1" rx="12"/>')

    # Header Section
    y = 38
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, -apple-system, sans-serif" font-size="22" font-weight="bold" fill="{TEXT_MAIN}">{escape(data["title"])}</text>')
    y += 24
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, -apple-system, sans-serif" font-size="14" fill="{COLOR_ACCENT}">{escape(data["subtitle"])}</text>')
    y += 20
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, -apple-system, sans-serif" font-size="12" fill="{TEXT_MUTED}">{escape(data["description"])}</text>')

    # KPI Summary Cards
    y += 20
    card_width = (WIDTH - 64 - 24) / 4
    cards_data = [
        ("Total APIs", f"{stats['total']}", "Tracked surface", COLOR_ACCENT),
        ("Implemented", f"{stats['implemented']} ({stats['implemented']*100.0/stats['total']:.1f}%)", "Native execution", COLOR_IMP),
        ("Stubs & Partial", f"{stats['stubbed']} ({stats['stubbed']*100.0/stats['total']:.1f}%)", "Emulated behavior", COLOR_STUB),
        ("Missing / Pending", f"{stats['missing']} ({stats['missing']*100.0/stats['total']:.1f}%)", "Unsupported", COLOR_MISS),
    ]

    for idx, (label, val, sub, col) in enumerate(cards_data):
        cx = 32 + idx * (card_width + 8)
        elements.append(f'<rect x="{cx:.1f}" y="{y}" width="{card_width:.1f}" height="64" rx="8" fill="{CARD_BG}" stroke="{BORDER_COLOR}" stroke-width="1"/>')
        elements.append(f'<rect x="{cx:.1f}" y="{y}" width="4" height="64" rx="2" fill="{col}"/>')
        elements.append(f'<text x="{cx + 14:.1f}" y="{y + 20}" font-family="Segoe UI, sans-serif" font-size="11" font-weight="600" fill="{TEXT_MUTED}" text-transform="uppercase">{escape(label)}</text>')
        elements.append(f'<text x="{cx + 14:.1f}" y="{y + 42}" font-family="Segoe UI, sans-serif" font-size="16" font-weight="bold" fill="{TEXT_MAIN}">{escape(val)}</text>')
        elements.append(f'<text x="{cx + 14:.1f}" y="{y + 56}" font-family="Segoe UI, sans-serif" font-size="10" fill="{TEXT_DIM}">{escape(sub)}</text>')

    y += 82

    # Section 1: Subsystem Breakdown Rows
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="15" font-weight="bold" fill="{TEXT_MAIN}">Subsystem Progress</text>')
    elements.append(f'<text x="{WIDTH-32}" y="{y}" font-family="Segoe UI, sans-serif" font-size="12" fill="{TEXT_MUTED}" text-anchor="end">Status breakdown by shared library</text>')
    y += 14

    bar_x = 180
    bar_width = WIDTH - 32 - bar_x - 140

    for lib, lstat in stats["libraries"].items():
        elements.append(f'<text x="32" y="{y+12}" font-family="monospace" font-size="12" font-weight="600" fill="{TEXT_MAIN}">{escape(lib)}</text>')
        # Track background
        elements.append(f'<rect x="{bar_x}" y="{y}" width="{bar_width}" height="14" rx="3" fill="{CARD_BG}"/>')
        
        # Segmented progress
        imp_w = (lstat["implemented"] / lstat["total"]) * bar_width if lstat["total"] else 0
        stb_w = (lstat["stubbed"] / lstat["total"]) * bar_width if lstat["total"] else 0
        
        if imp_w > 0:
            elements.append(f'<rect x="{bar_x}" y="{y}" width="{imp_w:.1f}" height="14" rx="3" fill="{COLOR_IMP}"><title>{lib}: {lstat["implemented"]} implemented</title></rect>')
        if stb_w > 0:
            elements.append(f'<rect x="{bar_x + imp_w:.1f}" y="{y}" width="{stb_w:.1f}" height="14" rx="3" fill="{COLOR_STUB}"><title>{lib}: {lstat["stubbed"]} stubbed</title></rect>')

        pct_label = f'{lstat["implemented"]}/{lstat["total"]} ({lstat["percent"]:.1f}%)'
        elements.append(f'<text x="{WIDTH-32}" y="{y+11}" font-family="monospace" font-size="11" fill="{TEXT_MUTED}" text-anchor="end">{pct_label}</text>')
        y += 22

    y += 12

    # Section 2: Interactive Function Discovery Grid
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="15" font-weight="bold" fill="{TEXT_MAIN}">API Function Discovery Matrix</text>')
    y += 18
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="12" fill="{TEXT_DIM}">Each cell represents an imported function or COM method. Hover over cells for implementation details.</text>')
    
    # Legend
    leg_x = WIDTH - 32 - 320
    elements.append(f'<rect x="{leg_x}" y="{y-12}" width="10" height="10" rx="2" fill="{COLOR_IMP}"/>')
    elements.append(f'<text x="{leg_x+16}" y="{y-3}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_MUTED}">Implemented</text>')
    elements.append(f'<rect x="{leg_x+105}" y="{y-12}" width="10" height="10" rx="2" fill="{COLOR_STUB}"/>')
    elements.append(f'<text x="{leg_x+121}" y="{y-3}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_MUTED}">Stubbed</text>')
    elements.append(f'<rect x="{leg_x+190}" y="{y-12}" width="10" height="10" rx="2" fill="{COLOR_MISS}"/>')
    elements.append(f'<text x="{leg_x+206}" y="{y-3}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_MUTED}">Missing</text>')

    y += 16

    # Render Grid Cells (40 columns per row)
    COLS = 40
    CELL_SIZE = 18
    CELL_GAP = 4
    GRID_START_X = 32

    # Flatten all functions in order
    all_funcs = []
    for lib, funcs in data["libraries"].items():
        for f in funcs:
            all_funcs.append((lib, f["name"], f["status"], f["description"]))

    for idx, (lib, name, status, desc) in enumerate(all_funcs):
        row = idx // COLS
        col = idx % COLS
        cx = GRID_START_X + col * (CELL_SIZE + CELL_GAP)
        cy = y + row * (CELL_SIZE + CELL_GAP)

        color = COLOR_IMP if status == "implemented" else (COLOR_STUB if status == "stub" else COLOR_MISS)
        tip = f"{lib}!{name} [{status.upper()}]\n{desc}"

        elements.append(
            f'<g>'
            f'<title>{escape(tip)}</title>'
            f'<rect x="{cx}" y="{cy}" width="{CELL_SIZE}" height="{CELL_SIZE}" rx="3" fill="{color}" '
            f'stroke="{BG_COLOR}" stroke-width="1"/>'
            f'</g>'
        )

    grid_rows = (len(all_funcs) + COLS - 1) // COLS
    y += grid_rows * (CELL_SIZE + CELL_GAP) + 24

    # Footer
    elements.append(f'<line x1="32" y1="{y}" x2="{WIDTH-32}" y2="{y}" stroke="{BORDER_COLOR}" stroke-width="1"/>')
    y += 18
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_DIM}">Last updated: {data["last_updated"]} | Configured via library-progress.json | Run python scripts/library_progress.py</text>')
    elements.append(f'<text x="{WIDTH-32}" y="{y}" font-family="Segoe UI, sans-serif" font-size="11" fill="{COLOR_ACCENT}" text-anchor="end">git:mininxd/NFSU2-Revived-Android</text>')
    y += 24

    # Build final SVG
    svg_content = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{y}" viewBox="0 0 {WIDTH} {y}" '
        f'role="img" aria-labelledby="title desc">\n'
        f'<title id="title">{escape(data["title"])}</title>\n'
        f'<desc id="desc">{stats["implemented"]} implemented, {stats["stubbed"]} stubbed, {stats["missing"]} missing out of {stats["total"]} tracked APIs ({stats["percent"]:.1f}% coverage).</desc>\n'
        + "\n".join(elements)
        + "\n</svg>\n"
    )

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print(f"Generated SVG progress visualization: {output_path} ({stats['percent']:.1f}% coverage)")
    return stats

def main():
    parser = argparse.ArgumentParser(description="NFSU2 Library Progress & SVG Generator")
    parser.add_argument("--set", nargs=3, metavar=("DLL", "FUNC", "STATUS"), help="Set function status: implemented, stub, missing")
    parser.add_argument("--desc", type=str, help="Description note when updating a function")
    parser.add_argument("--json", type=str, default=JSON_PATH, help="Path to JSON data file")
    parser.add_argument("--svg", type=str, default=SVG_PATH, help="Path to output SVG file")
    parser.add_argument("--rebuild", action="store_true", help="Rebuild SVG from JSON")
    parser.add_argument("--summary", action="store_true", help="Display coverage summary")
    parser.add_argument("--list", type=str, nargs="?", const="all", metavar="DLL", help="List functions and statuses (optional: filter by DLL)")
    args = parser.parse_args()

    data = load_data()

    if args.list:
        target_dll = args.list.lower() if args.list != "all" else None
        print(f"\n--- Library Function Listing ---")
        for lib, funcs in sorted(data["libraries"].items()):
            if target_dll and target_dll != lib.lower():
                continue
            print(f"\n[{lib}] ({len(funcs)} functions):")
            for f in sorted(funcs, key=lambda x: x["name"]):
                status_char = "[X]" if f["status"] == "implemented" else ("[~]" if f["status"] == "stub" else "[ ]")
                print(f"  {status_char} {f['name']:<35} - {f['status']:<11} ({f.get('description', '')})")
        print()
        if not args.set and not args.rebuild:
            return

    if args.set:
        dll, func, status = args.set
        dll = dll.lower()
        if status not in ("implemented", "stub", "missing"):
            print(f"Error: status must be one of: implemented, stub, missing (got {status})")
            sys.exit(1)
        
        found = False
        if dll in data["libraries"]:
            for item in data["libraries"][dll]:
                if item["name"].lower() == func.lower():
                    item["status"] = status
                    if args.desc:
                        item["description"] = args.desc
                    found = True
                    print(f"Updated {dll}!{item['name']} -> {status}")
                    break
        if not found:
            if dll not in data["libraries"]:
                data["libraries"][dll] = []
            data["libraries"][dll].append({
                "name": func,
                "status": status,
                "description": args.desc or f"{dll} function"
            })
            print(f"Added {dll}!{func} -> {status}")
        save_data(data)

    stats = generate_svg(data, args.svg)
    print(f"\n--- Coverage Summary ---")
    print(f"Total APIs Tracked:  {stats['total']}")
    print(f"Implemented:         {stats['implemented']} ({stats['implemented']*100.0/stats['total']:.1f}%)")
    print(f"Stubbed / Partial:   {stats['stubbed']} ({stats['stubbed']*100.0/stats['total']:.1f}%)")
    print(f"Missing / Pending:   {stats['missing']} ({stats['missing']*100.0/stats['total']:.1f}%)")
    print(f"Weighted Coverage:   {stats['percent']:.1f}%\n")

if __name__ == "__main__":
    main()
