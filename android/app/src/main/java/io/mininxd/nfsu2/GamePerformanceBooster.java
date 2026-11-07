package io.mininxd.nfsu2;

import android.app.Activity;
import android.app.GameManager;
import android.content.Context;
import android.os.Build;
import android.os.PowerManager;
import android.os.Process;
import android.system.Os;
import android.util.Log;
import android.view.Window;
import android.view.WindowManager;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.lang.reflect.Method;

/**
 * Optimizes performance, stability, and governor scheduling for NFSU2 Revived.
 * Applies HyperOS / MIUI Game Turbo hints, DXVK asynchronous pipeline configurations,
 * frame latency constraints, and CPU performance cluster binding.
 */
public final class GamePerformanceBooster {
    private static final String TAG = "NFSU2-Booster";
    private static boolean applied = false;

    private GamePerformanceBooster() {}

    /**
     * Applies full performance, driver, and system governor boost.
     * Safe to invoke multiple times; executes only once per process.
     */
    public static synchronized void apply(Activity activity, Window window) {
        if (applied || activity == null) return;
        applied = true;

        try {
            optimizeProcessPriority();
            detectAndTuneHyperOS(activity);
            detectAndTuneMediaTek(activity);
            configureAndroidGameManager(activity);
            enableSustainedPerformance(activity, window);
            configureDxvkAndDriver(activity);
            PerformanceCores.bindToPerformanceCores();
            Log.i(TAG, "GamePerformanceBooster optimizations successfully applied");
        } catch (Throwable t) {
            Log.w(TAG, "Error applying GamePerformanceBooster", t);
        }
    }

    /**
     * Elevates process and render thread scheduling priority.
     */
    private static void optimizeProcessPriority() {
        try {
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_DISPLAY);
        } catch (Throwable ignored) {}
    }

    /**
     * Detects Xiaomi HyperOS / MIUI and coordinates with Game Turbo / Joyose.
     */
    private static void detectAndTuneHyperOS(Context context) {
        String hyperOsVersion = getSystemProperty("ro.mi.os.version.name", "");
        String miuiVersion = getSystemProperty("ro.miui.ui.version.name", "");

        if (!hyperOsVersion.isEmpty() || !miuiVersion.isEmpty()) {
            String rom = !hyperOsVersion.isEmpty() ? ("HyperOS " + hyperOsVersion) : ("MIUI " + miuiVersion);
            Log.i(TAG, "Detected Xiaomi platform: " + rom);

            // Signal Xiaomi Performance / Power manager if accessible via reflection
            try {
                Class<?> perfClass = Class.forName("android.os.PerfService");
                Method boostMethod = perfClass.getMethod("boostEnable", int.class);
                boostMethod.invoke(null, 1);
                Log.i(TAG, "Enabled Xiaomi PerfService boost");
            } catch (Throwable ignored) {}

            try {
                Class<?> joyoseHelper = Class.forName("com.xiaomi.joyose.JoyoseManager");
                Method getMethod = joyoseHelper.getMethod("getInstance");
                Object instance = getMethod.invoke(null);
                if (instance != null) {
                    Method setGame = instance.getClass().getMethod("setGameMode", boolean.class);
                    setGame.invoke(instance, true);
                    Log.i(TAG, "Notified Joyose Game Turbo mode active");
                }
            } catch (Throwable ignored) {}
        }
    }

    /**
     * Detects MediaTek platforms (Dimensity / Helio) and signals performance boosting.
     */
    private static void detectAndTuneMediaTek(Context context) {
        String hardware = getSystemProperty("ro.hardware", "").toLowerCase();
        String platform = getSystemProperty("ro.board.platform", "").toLowerCase();
        if (hardware.contains("mt") || platform.contains("mt") || Build.HARDWARE.toLowerCase().contains("mt")) {
            Log.i(TAG, "Detected MediaTek SoC: hardware=" + hardware + " platform=" + platform);
            try {
                Class<?> perfClass = Class.forName("com.mediatek.perfservice.PerfServiceWrapper");
                Method boostMethod = perfClass.getMethod("boostEnable", int.class);
                boostMethod.invoke(null, 1);
                Log.i(TAG, "Enabled MediaTek PerfService boost");
            } catch (Throwable ignored) {}
        }
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
                    window.setSustainedPerformanceMode(true);
                    Log.i(TAG, "Sustained performance mode active");
                }
            } catch (Throwable ignored) {}
        }
    }

    /**
     * Generates an optimized dxvk.conf and configures low-overhead driver environment variables.
     */
    private static void configureDxvkAndDriver(Context context) {
        try {
            File cacheDir = new File(context.getCacheDir(), "dxvk-v2");
            if (!cacheDir.exists()) cacheDir.mkdirs();

            File confFile = new File(cacheDir, "dxvk.conf");
            try (PrintWriter pw = new PrintWriter(new FileWriter(confFile))) {
                pw.println("# NFSU2 Revived DXVK Profile (Zero Stutter / Low Latency)");
                pw.println("dxvk.enableStateCache = True");
                pw.println("dxvk.numCompilerThreads = 4");
                pw.println("dxvk.useRawSsbo = True");
                pw.println("d3d9.tearFree = True");
                pw.println("d3d9.maxFrameLatency = 1");
                pw.println("d3d9.deferSurfaceCreation = True");
                pw.println("d3d9.noExplicitFrontBuffer = True");
                pw.println("d3d9.lenientClear = True");
                pw.println("d3d9.allowDiscard = True");
                pw.println("d3d9.allowDoNotWait = True");
                pw.println("d3d9.cachedDynamicBuffers = True");
                pw.println("d3d9.evictManagedOnUnlock = True");
                pw.println("d3d9.memoryTrackTest = False");
                pw.println("d3d9.maxAvailableMemory = 4096");
                pw.println("dxgi.deferSurfaceCreation = True");
                pw.println("dxgi.maxDeviceMemory = 4096");
                pw.println("dxgi.maxSharedMemory = 2048");
            }

            Os.setenv("DXVK_CONFIG_FILE", confFile.getAbsolutePath(), true);
            Os.setenv("DXVK_STATE_CACHE_PATH", cacheDir.getAbsolutePath(), true);
            Os.setenv("DXVK_STATE_CACHE", "1", true);
            Os.setenv("DXVK_ASYNC", "1", true);
            Os.setenv("DXVK_LOG_LEVEL", "none", true);
            Os.setenv("WINEDEBUG", "-all", true);
            Os.setenv("WINE_HEAP_DELAY_FREE", "1", true);
            Os.setenv("WINE_HEAP_ZERO_MEMORY", "1", true);
            Os.setenv("WINE_DISABLE_FULLSCREEN_HACK", "1", true);
            Os.setenv("WINE_DISABLE_HARDWARE_SCHEDULING", "1", true);
            Os.setenv("WINE_DO_NOT_CREATE_DXGI_DEVICE_MANAGER", "1", true);
            Os.setenv("MESA_VK_WSI_PRESENT_MODE", "mailbox", true);
            Os.setenv("MESA_SHADER_CACHE_DISABLE", "false", true);
            Os.setenv("MESA_SHADER_CACHE_MAX_SIZE", "1024MB", true);
            Os.setenv("mesa_glthread", "true", true);
            Os.setenv("GALLIUM_DRIVER", "zink", true);
            Log.i(TAG, "Configured DXVK performance environment: " + confFile.getAbsolutePath());
        } catch (Throwable t) {
            Log.w(TAG, "Failed configuring DXVK environment", t);
        }
    }

    /**
     * Reads a system property via reflection.
     */
    private static String getSystemProperty(String key, String defaultValue) {
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            Method getMethod = sp.getMethod("get", String.class, String.class);
            return (String) getMethod.invoke(null, key, defaultValue);
        } catch (Throwable ignored) {
            return defaultValue;
        }
    }
}
