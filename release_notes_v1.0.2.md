## What's New in v1.0.2

### 1. OpenGL ES Native Backend Fix
- **Prevented Vulkan Surface Hijack in OpenGL ES Mode**:
  - Fixed an issue where selecting OpenGL ES still initialized and ran on the Vulkan bootstrap renderer and swapchain.
  - The Vulkan bootstrap renderer (`HostRenderer`) is now cleanly bypassed when OpenGL ES is chosen, preventing Vulkan from taking exclusive control over the native Android surface.
  - Devices with limited or unsupported Vulkan 1.1+ run cleanly on OpenGL ES without crashing on startup.
- **Eliminated Redundant EGL Teardown & Context Thrashing**:
  - Fixed `setD3D9HostWindow` and `nfs_native_gles_set_window` repeatedly destroying and reinitializing EGL displays, surfaces, and contexts on every single frame.
  - Cached active window references across host window callbacks so EGL stays persistent and responsive.
- **Pure OpenGL ES Dispatch Pipeline**:
  - Ensured all Direct3D 9 device calls (`DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`, `Present`, `SetTransform`, `SetViewport`, etc.) dispatch directly to the pure OpenGL ES pipeline when OpenGL is selected, rather than falling through to Vulkan.
  - Dynamically configured graphics tuner (`dxvk_tuner`) with the correct OpenGL ES profile before game initialization.

### 2. Custom Game Folder & Detection Fixes
- **Smart Path Resolution for Custom Folders**:
  - Fixed an issue where picking a custom folder (e.g. `/Download/nfsu2` or external SD card directories) was not properly recognized and fell back to the default path.
  - Added recursive subfolder detection: selecting a parent folder containing an `nfsu2` subdirectory or game data now automatically resolves to the proper game root.
  - Added full case-insensitive executable detection supporting `speed2.exe`, `SPEED2.EXE`, and `Speed2.exe`.
- **Eliminated Unwanted Uppercase Folder Creation**:
  - Removed aggressive directory creation that previously generated an empty uppercase `NFSU2` folder on internal storage and confused game file detection when files were placed in lowercase `nfsu2`.
  - Logging and diagnostic subsystems (`GameLog`, `NetLog`, `Diagnostics`) now respect the resolved game directory without forcefully creating new folders.
- **Cross-Process Path Synchronization**:
  - Custom game directory paths are now forwarded directly via Intent extras and synchronized into `NFSU2_DATA_DIR` so native engine shims, widescreen patches, and graphics tuners load game assets from the exact selected folder.

### 3. Custom Graphics Preset & Settings Persistence
- **Saved Custom Preset Button**:
  - Added a dedicated **Custom** preset button alongside Performance, Balanced, and Original.
  - Automatically records and updates custom parameters whenever graphics settings (Internal Resolution, Draw Distance, Graphics API, FPS Limit) are adjusted manually in the options list.
  - Tapping **Custom** instantly restores the user's saved personalized configuration, allowing effortless switching between default factory presets and custom tuned profiles.

### 4. Version Bump
- Bumped app `versionCode` to **102** and `versionName` to **1.0.2**.
