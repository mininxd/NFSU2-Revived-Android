## NFSU2 Revived v0.15.25 (Official Release)

### What's New in v0.15.25

#### 1. Transition to Official Stable Release (Out of Beta)
- Graduated from the v0.15.25 beta test cycle to the official stable release **v0.15.25**.
- Bumped app `versionCode` to **92** and `versionName` to **0.15.25**.

#### 2. Default Configuration Harmonization
- **Default Draw Distance**: Set to **"Original"** by default for maximum fidelity and accurate asset streaming.
- **Default Minimap Position**: Set to **"Bottom left (original)"** by default, aligning with the authentic Need for Speed Underground 2 HUD layout while retaining full runtime customization.

#### 3. Native Graphics & Performance Highlights
- **Direct Native GLES & Vulkan Presentation**: Direct EGL/Vulkan swapchain buffers and pipeline state management in pure Rust, bypassing desktop emulation bottlenecks.
- **Hybrid Precision Pacing & GameCore Tuning**: Enhanced CPU/GPU governor affinity and high-precision frame pacing across multi-core big.LITTLE mobile topologies.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta9...v0.15.25
