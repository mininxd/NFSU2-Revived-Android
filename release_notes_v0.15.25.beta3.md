## NFSU2 Revived v0.15.25.beta3 (Pre-Release)

> [!NOTE]
> This is a **Pre-Release** build intended for testers on the **Beta** Update Channel. Players on the **Stable** channel will remain on official milestone releases.

### What's New in v0.15.25.beta3

#### 1. Draw Distances and Scene Geometry Culling
- **Unified 3D Scene View FarZ Management**: Harmonized draw distance clamping across all 3D scene camera passes (primary camera, secondary views, road reflections, and world scenery).
- **Elimination of FarZ Overrides**: Removed legacy overrides that previously forced distant scenery and reflection camera passes to remain rendered at maximum distance regardless of the user-configured draw distance.
- **Hardware GPU Frustum Clipping**: Enforced dynamic perspective projection far-plane clamping in `SetTransform(D3DTS_PROJECTION)`. Distant geometry beyond the configured boundary is physically culled and discarded by GPU hardware rasterization:
```cpp
// Direct3D 9 hardware perspective culling
if (farDist >= 30.0f && !bypassed && matrix->_34 == 1.0f && matrix->_33 > 1.0f) {
    float zn = -matrix->_43 / matrix->_33;
    if (zn > 0.01f && zn < farDist) {
        float currentZf = (matrix->_33 > 1.0001f) ? (zn * matrix->_33 / (matrix->_33 - 1.0f)) : 10000.0f;
        if (currentZf > farDist) {
            tf_copy = *matrix;
            tf_copy._33 = farDist / (farDist - zn);
            tf_copy._43 = -zn * farDist / (farDist - zn);
            matrix = &tf_copy;
        }
    }
}
```
- **Extended Draw Call Culling**: Integrated visibility tests into `DrawPrimitiveUP` and `DrawIndexedPrimitiveUP` to ensure complete draw call suppression.

#### 2. Resolution Scaling & GPU Workload Optimization
- **Dynamic Window Buffer Synchronization**: Resolved an issue in the Android WSI layer where window buffer geometry was locked after initialization. Lower internal render resolutions now directly synchronize with the native window buffer geometry:
```cpp
extern "C" DXVK_EXPORT void dxvkAndroidSetRenderSize(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> guard(windowMutex);
    renderWidth = width;
    renderHeight = height;
#ifdef __ANDROID__
    if (currentWindow && width && height &&
        ANativeWindow_setBuffersGeometry(currentWindow, width, height, 0) == 0) {
        bufferWidth = width;
        bufferHeight = height;
    }
#endif
}
```
- **Zero-Overhead Hardware Upscaling**: Eliminated redundant full-screen swapchain blits and oversized Vulkan surface allocations, allowing the system display processor to scale the image with zero GPU overhead.

#### 3. DXVK Asynchronous Pipeline Caching & Shader Compilation
- **Asynchronous Pipeline Compilation**: Activated `dxvk.enableAsync = True` and `dxvk.gplAsyncCache = True` with Graphics Pipeline Libraries (`VK_EXT_graphics_pipeline_library`) to decouple pipeline compilation from the main render loop:
```ini
dxvk.enableStateCache = True
dxvk.enableAsync = True
dxvk.gplAsyncCache = True
dxvk.enableGraphicsPipelineLibrary = True
dxvk.trackPipelineLifetime = True
d3d9.deferSurfaceCreation = True
d3d9.maxFrameLatency = 1
```
- **Deferred Surface Creation**: Enabled `d3d9.deferSurfaceCreation` and optimized frame latency to reduce transient memory bandwidth spikes during gameplay.

#### 4. Universal Platform Stability
- **Universal CPU Topology Support**: Dynamic `/proc/cpuinfo` parsing for CPU thread scheduling across all mobile multi-core architectures.
- **Direct Launch Pipeline**: Streamlined launcher execution flow to immediately dispatch gameplay directly upon driver selection.

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta2...v0.15.25.beta3
