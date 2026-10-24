# Android device verification — 2026-10-07

## Artifact and device

- App: `io.mininxd.nfsu2`, version `0.1.0-bootstrap`, ARM64 debug APK.
- Icon: exact `ICONO/ICONO.png` packaged as `drawable/icono.png`.
- Device: Samsung SM-S938B, Android API 36, Adreno 830.
- Vulkan physical-device API reported: 1.3. The instance requests Vulkan 1.1,
  and device initialization enables only VK_KHR_swapchain, without optional
  features. This is not a test on a Vulkan-1.1-only driver.
- Native surface: 2340 × 1080.
- APK SHA-256:
  `8d10a7a4a53ba1c9f9bd6400d6660f2d03fb7caeb458700a060b57e59efefdbb`.

## Data transfer

Created `/storage/emulated/0/nfsu2`, initially absent. Copied 1,262 files,
1,854,310,887 bytes from the owner's local installation: game content folders,
the original executable, supporting root files, and existing memcard data.
Excluded PC DirectX installers, Support installers, uninstallers and unrelated
root screenshots/icons. Compared every copied file's SHA-256 to the source:
all 1,262 matched, including the expected SPEED2.EXE hash.

The APK reads directly from this folder; it does not embed the game content.
Future transfers can use `scripts/copy_android_data.ps1`; it retains an existing
device memcard directory rather than replacing saved progress.

## Tests and results

| Test | Result |
|---|---|
| Gradle assembleDebug, NDK ARM64 with compiler warnings treated as errors | Passed |
| Install APK over ADB and cold launch | Passed |
| Initial storage access request opens Android app-specific all-files settings | Passed |
| Storage grant | Enabled through ADB appops for the authorized device test |
| Runtime reads executable header/size and content directories | Passed |
| 4 GiB guest virtual reservation, commit/read/write high offsets, wrap rejection | Passed on ARM64 |
| Vulkan instance/device, swapchain, render-pass clear, Present | Passed |
| Home and return | Passed; swapchain recreated and presentation resumed |
| Screen sleep/wake | Passed; presentation resumed after wake |
| Status overlay visual inspection | Passed after fixing its separate Android surface |
| App-specific Java/native error log | No crash/error observed in the tested process |

After returning from Home, one uninterrupted presentation segment reached
2,400 frames. After screen wake, another segment reached 2,400 frames. The
observed cadence was approximately 120 presentations/s for a clear-only scene;
this measures the bootstrap, not game performance.

Evidence is kept locally in workspace `work/android-device-test.log`,
`work/android-bootstrap.png`, `work/android-copy-verification.txt`, and
`work/android-device-sha256.txt` (not game material for distribution).

## Scope

The original game executable is inspected as data, not executed. Generated C,
portable Win32/COM dispatch, D3D9 translation, game input and game audio are
not connected. These results do not certify gameplay or a complete renderer.
The user-facing status states that the game is not yet integrated.
