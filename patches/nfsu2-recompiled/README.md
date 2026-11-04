# Native patches for `prebuilt/lib_arm64.tar.gz`

The launcher options **Region**, **Speedometer Horizontal/Vertical Position** and
**Gamepad Layout**, and the
localized movies/speech, need native code that the current
`libnfsu2_android.so` in `prebuilt/` does not have. That library is built from
[codepdbh/nfsu2-recompiled](https://github.com/codepdbh/nfsu2-recompiled)
(it exports `Java_com_nfsu2_androidevolved_*`), so the changes are kept here as
patches against that repository's `main` (`fb2a359`), to be applied in order:

| Patch | What it does |
|---|---|
| `0001-localized-movies-and-speech.patch` | With a non-English language, `MOVIES/<name>.vp6` and `sdat.viv` open `<name>_sp.vp6` / `sdat_sp.viv` (`_fr`, `_gr`, `_it`, ...) when present, as on European discs. |
| `0002-region-switch.patch` | `nativeRegion(int)`: forces the game SKU at `0x864F24` (0 North America, 1 Europe) so a PAL copy loads `GLOBALTEXTURESROW.BIN` and the European cars. |
| `0003-live-minimap-speedometer.patch` | `nativeSpeedometerOffset(float dx, float dy)` (free speedometer position in 480-line HUD units), and minimap/speedometer offsets applied live (FE geometry cache dropped with `0x0050C7E0`; the radar map element that BlipsHook rewrites in place is tracked and corrected). |
| `0004-console-gamepad-layout.patch` | `nativeGamepadLayout(boolean)`: PS2-style layout (X accept, Circle back, R3 camera, Start without double press, world map / SMS / dialog buttons). |

The same changes are proposed upstream in
[codepdbh/nfsu2-recompiled#2359](https://github.com/codepdbh/nfsu2-recompiled/pull/2359).
Once that is merged, rebuilding `prebuilt/` from upstream `main` is enough and
this folder can be removed.

## Rebuilding the library

In a clone of nfsu2-recompiled that already builds (lifted `src/recomp/gen`,
prepared `build/dxvk-source`):

```sh
git checkout fb2a359
for p in /path/to/NFSU2-Revived-Android/patches/nfsu2-recompiled/0*.patch; do git apply "$p"; done
cd android && ./gradlew assembleDebug
```

Then pack `libnfsu2_android.so` (and the other `.so` files from
`app/build/intermediates/stripped_native_libs/.../arm64-v8a/`) into
`prebuilt/lib_arm64.tar.gz.part_*` as before.

With an older library the launcher keeps working: the new native calls catch
`UnsatisfiedLinkError` and the options have no effect.
