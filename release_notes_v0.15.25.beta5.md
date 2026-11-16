## NFSU2 Revived v0.15.25.beta5

### What's New in v0.15.25.beta5

#### 1. Dynamic Camera Back-Culling (50M) & Frustum Culling
- **50M Dynamic Back-Culling**: 3D world geometry and draw calls located more than 50 meters behind the active camera viewing plane (`depth < -50.0m`) are discarded before GPU vertex shading, significantly improving frame rates and reducing GPU overdraw.
- **Dynamic Look-Behind Response**: Culling dynamically tracks the camera's active view angle rather than being tied statically to the car body. When looking behind (Look Behind button or stick), the camera view direction rotates 180 degrees:
  - The scene behind the car is now **in front** of the camera and rendered up to the user's selected draw distance preset (75m, 175m, 350m, 600m).
  - The front of the car and road ahead are now **behind** the camera and get dynamically culled beyond 50m.
- **Near-Field Buffer (-50m to 0m)**: Objects within 50m behind the camera are preserved to ensure headlights, car shadows, tire smoke, and closely trailing cars remain fully visible without popping or visual artifacts.
- **Lateral Frustum Culling**: Objects in front of the camera that lie completely outside the horizontal/vertical screen view frustum are automatically culled.
- **Rear-View Mirror**: Removed rear-view mirror suppression in widescreen runtime to ensure mirrors and rear-facing viewports render cleanly.

#### 2. Vulkan API & Qualcomm Adreno Exclusive Extensions
- **`robustBufferAccess2` Support (`VK_EXT_robustness2`)**: Intercepted Vulkan device creation to query physical device capabilities and force-enable `robustBufferAccess2 = 1` in `VkPhysicalDeviceRobustness2FeaturesEXT`.
- **Qualcomm Adreno Hardware Optimization**: Added vendor detection for Qualcomm Adreno GPUs (`vendorID == 0x5143`), automatically probing and enabling Qualcomm-specific device extensions when available:
  - `VK_QCOM_tile_properties` (Tile memory dimensions & TBDR layout optimization)
  - `VK_QCOM_render_pass_transform`
  - `VK_QCOM_rotated_copy_commands`

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta4...v0.15.25.beta5
