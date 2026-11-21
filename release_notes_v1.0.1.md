## What's New in v1.0.1

### 1. DirectSound Audio Engine Overhaul & Zero Contention
- **Atomic Voice Control (`PcmVoice`)**:
  - Converted `frequency`, `position`, `left`, `right`, `playing`, and `loop` fields to lock-free atomic primitives.
  - Eliminated mutex acquisition from high-frequency guest calls:
    - **`SetFrequency` (Method 17)**: Called on every engine RPM calculation and gear shift. Runs lock-free in `O(1)` atomic time.
    - **`SetVolume` / `SetPan` (Methods 15 & 16)**: Dynamic stereo balance and master attenuation update lock-free.
    - **`GetCurrentPosition` (Method 4)**: Cursor queries read the atomic position snapshot without contending with the mixer thread.
    - **`Play` / `Stop` (Methods 12 & 18)**: Stream state switches lock-free.
- **Non-Blocking AAudio Mixer**:
  - Implemented a non-blocking spin try-lock in `PcmMixer::render(...)` with ARM64 `yield` backoff. Decouples the Android audio callback thread from guest memory copies and eliminates audio stuttering and crackles under heavy load.
  - Replaced blocking mutex locks in telemetry profiling with non-blocking checks to prevent frame pacing drops.

### 2. Graphics Presets
- Added one-tap graphics presets directly beneath the PLAY button on the launcher:
  - **Performance**: 50% Internal Resolution, Performance Draw Distance (175m).
  - **Balanced**: 70% Internal Resolution, Extended Draw Distance (600m).
  - **Original**: 100% Native Resolution, Original Draw Distance (unclamped).

### 3. Custom Game Location
- Integrated a **"Choose Game Folder"** button directly inside the Game Files card box.
- Supports Android Storage Access Framework (`ACTION_OPEN_DOCUMENT_TREE`) and direct custom paths, allowing games stored on external SD cards or custom directories to be selected and remembered.

### 4. Launcher UI Cleanup
- Simplified the settings section title to **"SETTINGS"**.
- Removed unnecessary status checklists for a cleaner launcher screen.
