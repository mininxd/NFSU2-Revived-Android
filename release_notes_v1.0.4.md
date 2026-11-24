## What's New in v1.0.4

### 1. CPU Prime Core Pinning & Maximum Frequency Enforcement (Issue #173)
- **Prime Core Affinity (`RenderMain`)**:
  - The main rendering and emulation thread is now explicitly pinned to the SoC's **Prime cores** (e.g., Cores 6 & 7 on Snapdragon 8 Elite running at 4.32 GHz).
  - Eliminates scheduler drift to lower-frequency cores (3.53 GHz), delivering an immediate 22% single-thread execution boost to lock gameplay to a solid 60 FPS.
- **Governor Active Pacing**:
  - Replaced sub-6ms `sleep_for` sleeps in `yieldGuestMachine` with ARM `yield` active spin-pacing.
  - Prevents kernel timer overshoots (which caused frame times to drift from 16.6ms to 18.5ms / 54 FPS) and keeps the Linux kernel EAS governor at peak performance P-states.
- **ADPF Governor Escalation**:
  - Enhanced Android Dynamic Performance Framework reports to escalate work duration when under load, forcing PowerHAL and the GPU governor to run at maximum frequencies.

### 2. Audio Pipeline Stabilization & Zero-Stutter Fixes
- **Watchdog Deconfliction**:
  - Increased synthetic advance threshold from 20ms to 250ms, matching true callback stall conditions.
  - Prevents normal 20ms audio buffer callbacks from triggering false synthetic clock ticks and advancing voices twice.
- **Lock-Free DirectSound Unlock**:
  - Removed global mixer mutex acquisition during DirectSound `Unlock` (`memcpy`).
  - The real-time AAudio thread no longer suffers transient lock contention or drops frames to silence.
- **DirectSound Write Margin**:
  - Increased circular buffer write cursor lead time from 10ms to 30ms, preventing the playback cursor from overtaking write regions during hardware bursts.
- **Atomic Stream Telemetry**:
  - Converted `publishes` and `staleFrames` in `PcmVoice` to atomic primitives with memory barriers to prevent false streaming voice muting.

### 3. Version Bump
- Bumped app `versionCode` to **104** and `versionName` to **1.0.4**.
