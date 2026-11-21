## What's New in v1.1.beta1

### 1. High Priority GLES Context (`EGL_IMG_context_priority`)
- Integrated `EGL_CONTEXT_PRIORITY_LEVEL_IMG` and `EGL_CONTEXT_PRIORITY_HIGH_IMG` during EGL OpenGL ES context creation in `native_gles.rs`.
- Commands the GPU driver and scheduler (Adreno, Mali, PowerVR) to prioritize game render queues and forces the GPU frequency governor into high-performance clock profiles.

### 2. Android Dynamic Performance Framework (ADPF) & Frequency Scaling
- Implemented NDK ADPF integration (`<android/performance_hint.h>`) in `adpf_manager.cpp` and `perf_cores::adpf`:
  - Registers all critical worker threads (`/proc/self/task`) in the ADPF hint session (`APerformanceHint_createSession`).
  - Implemented API 34+ unified CPU & GPU work duration reporting (`AWorkDuration`, `APerformanceHint_reportActualWorkDuration2`) with seamless fallback to `APerformanceHint_reportActualWorkDuration`.
  - Commands Android PowerHAL to aggressively boost and sustain high CPU and GPU frequencies, preventing DVFS clock drop stutters and Little core traps.

### 3. Performance Core Affinity & Governor Optimization
- **Active Performance Core Pinning (`roam_affinity.c`)**:
  - Implemented dynamic frequency & compute capacity thresholding (`>= 70%` of max CPU capacity) to reliably distinguish Prime and Mid (Big) clusters from Little energy-efficiency cores.
  - Automatically isolates and binds the main emulator and rendering threads exclusively to high-capacity performance cores, preventing the Android EAS (Energy Aware Scheduling) governor from parking critical threads on low-frequency Little cores.
  - Elevated process and thread scheduling priority to `PRIO_PROCESS` nice `-20` via `sys/resource.h` to trigger rapid governor frequency ramping and eliminate DVFS clock drop stutters.
- **Rust `perf_cores` Engine Tuning**:
  - Updated `apply_affinity()` to bind process and rendering threads directly to `perf_cores_mask`.
  - Intelligently separates audio worker threads (which float across all cores with nice `-16` for zero starvation) from heavy emulation, DXVK, and render threads (bound strictly to performance clusters with nice `-20`).

### 4. dinput8.dll ASI Loader & NFSU2 Extra Options Mod Support
- **Fixed `graphics_cache` Build**: Fixed compilation syntax error in `d3d9_bridge.rs`, restoring compilation and deployment of `libgraphics_cache.so`.
- **Automatic ASI Mod Detection (`dinput8.dll` / `NFSU2ExtraOptions.asi`)**:
  - Automatically detects `dinput8.dll` and `scripts/NFSU2ExtraOptions.asi` placed in the game root directory.
  - Transparently activates native Extra Options patches in the game engine in the background when the mod files are present.
  - Multi-path INI discovery: checks `<game_root>/scripts/NFSU2ExtraOptionsSettings.ini`, `<game_root>/NFSU2ExtraOptionsSettings.ini`, `/sdcard/nfsu2/`, and `/sdcard/cache/` with case-insensitive path fallbacks.

### 5. Version Bump
- Bumped app `versionCode` to **110** and `versionName` to **1.1.beta1**.

