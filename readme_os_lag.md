# Fixing OS Stutter & Lag (HyperOS, MIUI, ColorOS, One UI)

If you experience stutter, choppy frame pacing, or sudden lag during gameplay that **immediately becomes butter-smooth when you pull down the status bar / notification shade**, this guide explains why it happens and how to fix it on your device.

---

## The Phenomenon

- **In Fullscreen**: The game stutters or feels sluggish (like 20–30 FPS even though your device has a high-end chipset).
- **Pulling Down Status Bar**: As soon as you swipe down from the top of the screen to reveal the status bar / quick settings, the game in the background suddenly runs at a perfect, fluid 60 or 120 FPS.
- **Closing Status Bar**: The moment the notification shade slides back up, the lag returns.

---

## Why Does This Happen?

Modern Android OEM skins (especially **Xiaomi HyperOS / MIUI**, **OnePlus / OPPO ColorOS**, and **Samsung One UI**) utilize aggressive battery management and dynamic refresh rate policies:

1. **Adaptive Sync / LTPO Panel Throttling:**
   When an unrecognized app runs in immersive fullscreen without frequent system-level UI events, the OS display service drops the physical screen refresh rate down to **30Hz or 48Hz** to save battery. If the game engine renders at 60 FPS while the screen is forced to 30Hz or 48Hz, frames are dropped and frame pacing is destroyed.
2. **System Touch Boost & PowerHAL:**
   When you pull down the notification shade:
   - The OS detects system UI interaction and triggers **PowerHAL Touch Boost**, temporarily ramping up CPU and GPU clock speeds to maximum.
   - The display manager switches the panel back up to **120Hz / 144Hz** to keep the notification animation smooth, allowing the game behind it to render without display bottlenecks.
3. **OEM Game Daemons (Joyose, GOS, HyperBoost):**
   Third-party custom APKs and ports lack built-in OEM whitelist certificates. Without explicit configuration, background system services treat the game as a generic utility and aggressively downclock the performance cores (EAS).

---

## Built-In Code Optimizations in NFSU2 Revived

Recent versions of NFSU2 Revived include built-in engine accommodations to combat OS throttling:

- **Game Category Tagging**: Declared `android:appCategory="game"` in the manifest so modern Android thermal engines recognize the app as a 3D game.
- **Display Mode & Refresh Rate Negotiation**: On Android 6.0+ (API 23) and Android 11+ (API 30), the game requests the highest supported refresh rate mode (`preferredDisplayModeId`, `preferredMinDisplayRefreshRate = 60.0f`, `preferredMaxDisplayRefreshRate = 120.0f`) via `WindowManager.LayoutParams`.
- **EAS CPU Core Affinity**: Render and simulation threads are dynamically pinned to Big and Prime performance CPU cores via `libperf_cores.so`.

However, OEM custom skins can override application requests unless user settings are adjusted.

---

## Device-Specific Fixes

### 1. Xiaomi / POCO / Redmi (HyperOS & MIUI)

#### Fix A: Lock Refresh Rate to 120Hz
1. Open **Settings** → **Display & brightness** → **Refresh rate**.
2. Select **Custom** instead of *Default (Recommended)*.
3. Choose **120Hz** (or **144Hz** if supported).
4. Tap **Apps using high refresh rate** at the bottom.
5. Scroll to **NFSU2 Revived** and set it to **120Hz**.

#### Fix B: Add to Game Turbo
1. Open the **Security** app → tap **Game Turbo**.
2. Tap the **+** (Add) icon and enable the toggle for **NFSU2 Revived**.
3. In Game Turbo settings (gear icon):
   - Set **Performance Mode** to **ON**.
   - Under *Performance optimization*, disable frame rate limitation.
   - In *Additional settings* for NFSU2, set Touch Response to maximum.

#### Fix C: Disable Battery Restrictions
1. Long-press the **NFSU2** app icon on your home screen → tap **App info** (ℹ️).
2. Tap **Battery saver**.
3. Select **No restrictions** (prevents HyperOS from cutting CPU power when thermals rise).

#### Fix D: Developer Options Peak Refresh Rate (Universal Fallback)
1. Go to **Settings** → **About phone** → tap **OS version / MIUI version** 7 times to unlock Developer options.
2. Go to **Settings** → **Additional settings** → **Developer options**.
3. Enable **Force peak refresh rate** (forces the screen to always stay at 120Hz).

---

### 2. OnePlus / OPPO / Realme (OxygenOS, ColorOS, Realme UI)

1. **App Refresh Rate**:
   - Go to **Settings** → **Display & brightness** → **Screen refresh rate**.
   - Select **High**.
   - Tap **App-specific refresh rate** → find **NFSU2 Revived** → select **120 Hz**.
2. **Game Space / Games App**:
   - Open the **Games** app.
   - Add **NFSU2 Revived**.
   - Swipe in the gaming sidebar during launch and select **Pro Gamer Mode** (locks CPU/GPU frequencies).
3. **Battery Settings**:
   - Go to **App info** → **Battery usage** → enable **Allow background activity** and disable **Optimize battery use**.

---

### 3. Samsung Galaxy (One UI)

1. **Motion Smoothness**:
   - Go to **Settings** → **Display** → **Motion smoothness** → select **Adaptive** or **High**.
2. **Game Booster & Labs**:
   - Open the **Game Launcher / Gaming Hub** app.
   - Tap **More** → **Game Booster**.
   - Go to **Labs** → turn ON **Alternate game performance management** (bypasses aggressive Game Optimizing Service throttling).
3. **Never Sleeping App**:
   - Go to **Settings** → **Battery** → **Background usage limits**.
   - Tap **Never auto-sleeping apps** → add **NFSU2 Revived**.

---

### 4. Transsion (Infinix XOS / Tecno HiOS)

1. Go to **Settings** → **Display & brightness** → **Screen refresh rate** → set to **120Hz**.
2. Open **Game Space** / **Dar-Link Engine** and add **NFSU2 Revived**.
3. Set power mode to **Performance Booster**.

---

## Verifying the Fix

To verify that your display is running at full speed:
1. Open **Developer options** on your device.
2. Enable **Show refresh rate** (displays a live FPS/Hz counter in the top-left corner).
3. Launch **NFSU2 Revived**:
   - If the counter stays at **60Hz**, **90Hz**, or **120Hz** without dropping when the status bar closes, the issue is resolved and gameplay will be smooth!
