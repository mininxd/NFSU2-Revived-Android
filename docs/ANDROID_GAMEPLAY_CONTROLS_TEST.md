# Android gameplay / controls 0.10.0 — 2026-10-08

The generated audio callback changes described in
[the menu/profile report](ANDROID_MENU_PROFILE_TEST.md) let the connected
SM-S938B load Resort Loop, start the countdown and render the race HUD,
opponents and track. The user confirmed reaching the race.

## Performance changes

The initial ARM64 reference used `-O0` for the lifted game and Android runtime.
The first race measured 2.5–2.7 presentation FPS at 1170×540. Both targets now
use `-O2`, retaining `-fno-strict-aliasing` and disabled floating-point
contraction. DXVK was already built with `-O2`.

Android CMake splits generated units larger than 64 MiB at complete function
boundaries into roughly 6 MiB units, with six compiler jobs to bound memory.
The 118 MB unit became 21 units. A reconstruction check verified the same
400 function definitions, order and full source bodies. Split game C remains
in the ignored build directory and is never committed.

Runtime checkpoints still check cancellation and foreground pause immediately.
Full guest-register saves and native thread handovers are amortized: every
64 checkpoints the runtime checks whether the 500-microsecond scheduling
interval expired. Blocking imports continue to yield explicitly. The scheduler
self-test forces 100 handovers independently of this production cadence.

The owner's local Most Wanted Android tree was inspected for comparison:
its Android CMake enables ThinLTO in Release; its touch view keeps pointer
ownership and sends state changes; its performance history documents CPU cost
per draw, native rendering and profiling. This port still uses its own x86
runtime and D3D9 bridge, rather than the Xbox 360 renderer.

## Controls and lifecycle

`RacingControlsView` replaces the large flat button rows with a transparent
Canvas overlay. Menu mode provides navigation, accept, back and profile text.
The **DRIVE** button switches to steering, accelerator, brake, nitro and
handbrake; **MENU** restores navigation. **PAUSE** sends Escape.
The overlay also follows the installed guest keyboard action map automatically
when entering/leaving driving mode. Manual mode switching remains available.
Driving includes up/down shifts and camera selection.
Each touch pointer owns a control, allowing held controls to coexist.
Sliding off, cancelling a gesture, opening text input or backgrounding the
activity releases held keys. Buttons show a pressed state. System bars hide
while the game has focus and can be temporarily shown by swiping.

The keyboard action-map switch previously rejected `DIDSAM_FORCESAVE` (2).
The game's `ControllerStruct874C40::5CF320` unacquires the keyboard and uses
that flag when changing action formats. Supporting it installs the 53-action
driving format, including accelerator (user 1), brake (2), steering (3/4),
handbrake (5) and nitro (6), instead of leaving the 32-action frontend format.
The startup test covers remapping while unacquired, rejecting an acquired
remap, and delivering the new driving press/release with its application ID.

**SETTINGS** opens an overlay editor without injecting game keys. Drag a button
to move it; select it and use **− / +** to change its size (55–250%). **SAVE**
stores normalized positions and size factors in app SharedPreferences, with
separate menu and driving layouts. **CANCEL** reloads saved settings;
**RESET** previews defaults until saved. Pause the race before editing.

**SETTINGS → OPTIONS** adds classic, Xbox and PlayStation 3 visual layouts.
The owner's Most Wanted and Carbon touch views provided the layout reference:
left steering stick, RT/LT pedals and circular A/B/X/Y buttons in a diamond.
This port implements those controls against its keyboard action bridge; the
PS3 style uses R2/L2 and ×/○/□/△. Accelerator/brake remain on the pedals;
the face buttons provide handbrake, nitro, camera and look-back. Each driving
style stores its own button placement. The stick currently converts horizontal
movement to left/right keys with a center dead zone, rather than analog input.

Named custom layouts save the current mode/style and all button positions/sizes
as JSON in app preferences. Loading restores that layout; menu and driving
presets are listed separately. Style choice and named-preset operations apply
immediately, while **CANCEL** discards unsaved placement edits.

The inclination option uses the accelerometer with display-rotation correction,
low-pass smoothing, a calibrated neutral position, hysteresis, invert and 1–4×
sensitivity. It activates digital left/right in driving mode. Sensors unregister
on pause, steering clears in menus/editing, and touch/tilt key ownership combines
so releasing one source cannot release the other. Enabling or resuming calibrates
at the current phone position. Proportional analog steering remains future work.

Bluetooth gamepads are requested for a subsequent development step. Planned
coverage is Android gamepad buttons, analog steering, trigger pedals, per-device
dead zones and concurrent touch/gamepad input ownership. This version does not
claim Bluetooth controller support.

On background/window loss, guest execution pauses and input clears. Android
WSI assigns an opaque window identity to each native-surface generation.
D3D9 Present uses the current identity as its destination override, so DXVK
recreates its presenter after resume while keeping the game device/resources.
This also handles ANativeWindow pointer reuse across surface replacements.

## Validation

The optimized APK builds against Android API 35 and is installed on SM-S938B.
The runtime scheduler, audio callbacks and DirectInput startup checks pass.
Resort Loop at 1170×540 measured 37.5 FPS in the initial optimized sample;
later samples were approximately 35–51 presentation FPS, versus 2.5–2.7 with
the unoptimized lifted game. The user confirmed the performance improvement.
With the corrected action switch, the race screenshot shows 100 km/h and a
held accelerator (`work/android-driving-fix-race.png` in the parent workspace).
Home and re-entering GameActivity preserved process 5169 and recovered the
same race surface (`work/android-controls-resume.png`).
The user confirmed the 0.9.0 editor worked. Saved menu coordinates were present
in the app's `touch_layouts_v1.xml`, outside Git. Version 0.10.0 builds and installs
successfully; the options dialog was observed on the phone, and the user
confirmed the new version works. Detailed physical inclination calibration and
all named-preset combinations have not been independently validated.
A complete three-lap race, 120 FPS, Vulkan-1.1-only hardware, resolution changes
in the original display menu and full widescreen HUD/FOV remain unverified.
