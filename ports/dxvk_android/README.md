# DXVK Native Android Port

This target adapts official [doitsujin/dxvk](https://github.com/doitsujin/dxvk) release `v3.1.1`, commit
`b1a1c99ab52b687cf950d62c88bc2fa316b41663`, for Android ARM64. It provides high-performance Direct3D 9 to Vulkan translation on Android mobile GPUs (Adreno, Mali, PanVK, Turnip).

### Updating & Preparing DXVK

To download the latest official DXVK release packages and update the port:

```bash
# 1. Download official GitHub release assets (v3.1.1):
bash ports/download_dxvk_release.sh v3.1.1

# 2. Or update the submodule and prepare the source for Android:
python3 ports/dxvk_android/prepare.py ports/dxvk_modules build/dxvk-source
```

### Key Performance & Architectural Improvements

- **Upstream DXVK v3.1.1 Integration**: Replaces legacy 1.9.2 runtime with modern upstream DXVK featuring native ARM64 support, yielding optimized memory mapping, reduced draw-call latency, and streamlined command recording.
- **Asynchronous Pipeline & Shader Caching**: Integrated with `graphics_cache` / `dxvk_tuner` for zero-stutter frame pacing.
- **Mobile Vulkan Compatibility**:
  - `textureCompressionBC` is dynamically probed and made optional so devices without hardware BC decompression fall back smoothly.
  - Native Android WSI surface presentation (`VK_KHR_android_surface`) seamlessly integrates with `ANativeWindow`.
  - Android logcat integration forwards DXVK warnings and errors under tag `DXVK`.

### Third-party notices

- DXVK: zlib license, retained in the prepared source's `LICENSE` and [DXVK-LICENSE](DXVK-LICENSE). Source is available from [doitsujin/dxvk](https://github.com/doitsujin/dxvk).
- DXVK's bundled native Windows/DirectX headers include Wine-derived material under LGPL 2.1 or later.
