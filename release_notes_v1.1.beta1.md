## What's New in v1.1.beta1

### 1. Performance Core Affinity & CPU Clock Governor Optimization
- **Active Performance Core Pinning (`roam_affinity.c`)**:
  - Implemented dynamic frequency & compute capacity thresholding (`>= 70%` of max CPU capacity) to reliably distinguish Prime and Mid (Big) clusters from Little energy-efficiency cores.
  - Automatically isolates and binds the main emulator and rendering threads exclusively to high-capacity performance cores, preventing the Android EAS (Energy Aware Scheduling) governor from parking critical threads on low-frequency Little cores.
  - Elevated process and thread scheduling priority to `PRIO_PROCESS` nice `-20` via `sys/resource.h` to trigger rapid governor frequency ramping and eliminate DVFS clock drop stutters.
- **Rust `perf_cores` Engine Tuning**:
  - Updated `apply_affinity()` to bind process and rendering threads directly to `perf_cores_mask`.
  - Intelligently separates audio worker threads (which float across all cores with nice `-16` for zero starvation) from heavy emulation, DXVK, and render threads (bound strictly to performance clusters with nice `-20`).

### 2. Version Bump
- Bumped app `versionCode` to **110** and `versionName` to **1.1.beta1**.
