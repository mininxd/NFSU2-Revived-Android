## NFSU2 Revived v1.0.1

### What's New in v1.0.1

> [!IMPORTANT]
> **Audio Stabilization & Zero-Contention DirectSound Pipeline**
>
> In v1.0.1, the DirectSound voice emulation and real-time audio pipeline have been re-engineered for mobile multi-core architectures. The real-time AAudio callback thread is now completely decoupled from guest CPU emulation locks, eliminating priority inversions, engine audio stuttering, and crackling during high-RPM sweeps and gear shifts.

---

#### 1. Zero-Contention DirectSound Voice Parameters
- **Atomic Voice Control (`PcmVoice`)**:
  - Converted `frequency`, `position`, `left`, `right`, `playing`, and `loop` fields to relaxed atomic primitives.
  - Eliminated mutex acquisition from high-frequency guest calls:
    - **`SetFrequency` (Method 17)**: Called on every engine RPM calculation and gear shift. Now runs lock-free in `O(1)` atomic time.
    - **`SetVolume` / `SetPan` (Methods 15 & 16)**: Dynamic stereo balance and master attenuation update lock-free.
    - **`GetCurrentPosition` (Method 4)**: Cursor queries now read the atomic position snapshot without contending with the mixer thread.
    - **`Play` / `Stop` (Methods 12 & 18)**: Stream state switches lock-free.

#### 2. Non-Blocking Real-Time AAudio Mixer
- **Eliminated AAudio Priority Inversion**:
  - The Android audio server thread previously acquired a blocking `std::mutex` in `soundMixer.render(...)`, which was shared with the guest game thread's `memcpy` operations.
  - Implemented a non-blocking spin try-lock in `PcmMixer::render(...)` with ARM64 `yield` backoff. If the guest is actively copying a buffer slice, the audio callback no longer stalls or misses its hardware DMA deadline.
- **Lock-Free Telemetry Profiling**:
  - `serviceGuestSound()` now uses `std::try_to_lock` for debug stats logging, ensuring end-of-frame telemetry never blocks or hiccups active audio playback.

#### 3. Build & Version Details
- Bumped `versionCode` to **101** and `versionName` to **1.0.1**.
