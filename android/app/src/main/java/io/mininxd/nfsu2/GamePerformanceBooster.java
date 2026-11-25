package io.mininxd.nfsu2;

import android.app.Activity;
import android.app.GameManager;
import android.content.Context;
import android.os.Build;
import android.os.PowerManager;
import android.os.Process;
import android.util.Log;
import android.view.Window;
import android.view.WindowManager;

import java.io.File;

/**
 * Optimizes performance, stability, and governor scheduling for NFSU2 Revived.
 * Powered by pure Rust perf_cores and graphics_cache native acceleration engines.
 */
public final class GamePerformanceBooster {
    private static final String TAG = "NFSU2-Booster";
    private static boolean applied = false;
    private static boolean nativeBoosterLoaded = false;
    private static boolean nativeDxvkLoaded = false;

    static {
        try {
            System.loadLibrary("perf_cores");
            nativeBoosterLoaded = true;
        } catch (Throwable t) {
            Log.d(TAG, "libperf_cores not loaded for booster: " + t.getMessage());
        }
        try {
            System.loadLibrary("graphics_cache");
            nativeDxvkLoaded = true;
        } catch (Throwable t) {
            Log.d(TAG, "libgraphics_cache not loaded for booster: " + t.getMessage());
        }
    }

    public static native void nativeApplyBooster(Activity activity);
    public static native void nativeConfigureDxvkAndDriver(String cacheDir, String sdcardDir);

    private GamePerformanceBooster() {}

    /**
     * Applies full performance, driver, and system governor boost.
     * Delegates performance core binding, OEM boost, and DXVK configuration to native Rust.
     */
    public static synchronized void apply(Activity activity, Window window) {
        if (activity == null) return;
        boolean firstRun = !applied;
        applied = true;

        try {
            // 1. Native Rust affinity, process priority, and SoC/OEM governor boost
            if (firstRun) {
                if (nativeBoosterLoaded) {
                    try {
                        nativeApplyBooster(activity);
                        Log.i(TAG, "Applied native Rust CPU and governor performance booster");
                    } catch (Throwable t) {
                        Log.w(TAG, "nativeApplyBooster failed, using fallback: " + t.getMessage());
                        fallbackProcessPriority();
                        PerformanceCores.bindToPerformanceCores();
                    }
                } else {
                    fallbackProcessPriority();
                    PerformanceCores.bindToPerformanceCores();
                }
            }

            // 2. Native Rust driver environment setup (keep dxvk-v2 directory synchronized with GraphicsCache & DXVK)
            File cacheDir = new File(activity.getCacheDir(), "dxvk-v2");
            if (nativeDxvkLoaded) {
                try {
                    String rootDir = LauncherActivity.gameRoot(activity).getAbsolutePath();
                    nativeConfigureDxvkAndDriver(cacheDir.getAbsolutePath(), rootDir);
                    Log.i(TAG, "Configured native Rust graphics tuner and driver environment for: " + rootDir);
                } catch (Throwable t) {
                    Log.w(TAG, "nativeConfigureDxvkAndDriver failed: " + t.getMessage());
                }
            }

            // 3. Android platform frame scheduler & sustained performance hooks
            int targetFps = 60;
            try {
                targetFps = Integer.parseInt(GameOptions.get(activity, GameOptions.FPS));
                if (targetFps <= 0) targetFps = 120; // Uncapped maps to 120Hz display target
            } catch (Throwable ignored) {}
            AndroidGameManagerBridge.setGameplayMode(activity, targetFps);
            configureAndroidGameManager(activity);
            enableSustainedPerformance(activity, window);

            // 4. Dedicated GPU Clock Sustainer & DVFS Governor
            GpuClockGovernor.start(activity);

            Log.i(TAG, "GamePerformanceBooster optimizations successfully applied");
        } catch (Throwable t) {
            Log.w(TAG, "Error applying GamePerformanceBooster", t);
        }
    }

    private static void fallbackProcessPriority() {
        try {
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_DISPLAY);
        } catch (Throwable ignored) {}
    }

    /**
     * Configures Android 12+ GameManager (API 31+).
     */
    private static void configureAndroidGameManager(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                GameManager gm = context.getSystemService(GameManager.class);
                if (gm != null) {
                    int mode = gm.getGameMode();
                    Log.i(TAG, "Android GameManager mode: " + mode);
                }
            } catch (Throwable t) {
                Log.d(TAG, "GameManager check: " + t.getMessage());
            }
        }
    }

    /**
     * Enables sustained performance mode to prevent CPU throttling and frame hitching.
     */
    private static void enableSustainedPerformance(Context context, Window window) {
        if (window == null) return;

        try {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } catch (Throwable ignored) {}

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            try {
                PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
                if (pm != null && pm.isSustainedPerformanceModeSupported()) {
                    window.setSustainedPerformanceMode(false);
                    Log.i(TAG, "Sustained performance mode disabled to permit dynamic peak clocks");
                }
            } catch (Throwable ignored) {}
        }
    }
}
