## NFSU2 Revived v0.15.25.beta4

### What's New in v0.15.25.beta4

#### 1. Minimap & Radar Blip Restoration
- **Restored v0.15.22 Minimap Radar Logic**: Reverted the minimap blip logic back to the proven v0.15.22 implementation.
- **Fixed Hidden Radar Icons & Blips**: Resolved the issue introduced in v0.15.25.beta3 where radar icons, event markers, opponent blips, and GPS navigation points disappeared or rendered off-screen.
- **Eliminated Offset Desync**: Removed external coordinate manipulation in the roam companion bridge, ensuring radar blips align accurately with the minimap frame across all supported aspect ratios.

#### 2. Re-tuned Draw Distance Presets
Adjusted draw distance presets to provide a smoother balance between performance scaling and scene visibility:
- **Ultra Performance**: 75m (was 50m)
- **Performance**: 175m (was 100m)
- **Balanced**: 350m (was 250m)
- **Extended**: 600m (was 450m)
- **Original**: Unclamped default game distance

**Full Changelog**: https://github.com/mininxd/NFSU2-Revived-Android/compare/v0.15.25.beta3...v0.15.25.beta4
