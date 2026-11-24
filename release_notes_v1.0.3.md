## What's New in v1.0.3

### 1. Snapdragon 8 Elite & All-Big Core CPU Scheduling (Issue #173)
- **Qualcomm Oryon Architecture Recognition**:
  - Added dedicated detection for Qualcomm's custom Oryon architecture (`SM8750` / `sun` / `0x51` CPU implementer) and All-Big core mobile SoCs (MediaTek Dimensity 9300/9400 `MT6989`/`MT6991`).
  - Resolved an issue where chips lacking conventional efficiency cores had their first 4 cores falsely classified as "Little" cores, cutting CPU compute capacity in half.
  - Process and worker threads are now granted full scheduling access across all 8 high-performance Oryon cores (`perf_cores_mask`), enabling Qualcomm's Energy Aware Scheduling (EAS) to achieve locked 60 FPS.
- **SELinux Frequency Access Fallback**:
  - Fixed topology detection fallback on Android 15/16/17 where `/sys/devices/system/cpu/cpufreq/` nodes are restricted by SELinux.
  - Unknown CPU topologies now default to all-core performance binding rather than artificially constraining the game to half the available CPU cores.

### 2. Vulkan LTPO Presentation & Suboptimal Handling
- **`VK_SUBOPTIMAL_KHR` Frame Pacing**:
  - Added native handling for `VK_SUBOPTIMAL_KHR` (1000001003) across `vkAcquireNextImageKHR` and `vkQueuePresentKHR`.
  - Prevents dropped frames and micro-stuttering on variable-refresh-rate (VRR) / LTPO AMOLED panels during resolution changes and dynamic refresh scaling.

### 3. Display Refresh Rate & SurfaceFlinger Synchronization
- **Direct SurfaceFlinger Frame Rate Signaling**:
  - Integrated `View.setFrameRate` (API 30+) directly into `applyDisplayRefreshRate()` to signal desired 60Hz/120Hz frame rates to Android's SurfaceFlinger.
  - Prevents aggressive OEM display governors (Xiaomi HyperOS, OnePlus ColorOS, Samsung One UI) from downclocking screen refresh rates to 30Hz or 45Hz during gameplay.

### 4. Version Bump
- Bumped app `versionCode` to **103** and `versionName` to **1.0.3**.
