package io.mininxd.nfsu2;

import android.app.Activity;
import android.os.Process;
import android.util.Log;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * All CPU Cores Manager for NFSU2 Android.
 *
 * Enforces execution across ALL CPU cores on the device (cores 0 through N-1)
 * for the entire game process, emulated guest threads, Vulkan/DXVK workers,
 * audio system, and driver threads without restricting to a cluster subset.
 */
public final class AllCpuCoresManager {
    private static final String TAG = "NFSU2-AllCores";
    private static boolean nativeLoaded = false;
    private static final AtomicBoolean sInitialized = new AtomicBoolean(false);
    private static ScheduledExecutorService sWatcherService;

    static {
        try {
            System.loadLibrary("perf_cores");
            nativeLoaded = true;
            Log.i(TAG, "Loaded libperf_cores.so for AllCpuCoresManager");
        } catch (Throwable t1) {
            try {
                System.loadLibrary("nfsu2_roam");
                nativeLoaded = true;
                Log.i(TAG, "Loaded libnfsu2_roam.so fallback for AllCpuCoresManager");
            } catch (Throwable t2) {
                Log.w(TAG, "Native core libraries not loaded, using Java enforcement fallback");
            }
        }
    }

    public static native int nativeEnableAllCores();
    public static native long nativeGetAllCoresMask();
    public static native int nativeGetTotalCoreCount();
    public static native boolean nativeStartCpuBoost(int mode);
    public static native void nativeStopCpuBoost();
    public static native void nativeSetCpuBoostMode(int mode);
    public static native float nativeGetCpuBoostMflops();
    public static native int nativeGetCpuBoostActiveThreads();

    /**
     * Returns the total number of hardware CPU cores available on the SoC.
     */
    public static int getTotalCoreCount() {
        if (nativeLoaded) {
            try {
                int count = nativeGetTotalCoreCount();
                if (count > 0) return count;
            } catch (Throwable ignored) {}
        }
        int runtimeCount = Runtime.getRuntime().availableProcessors();
        return Math.max(1, runtimeCount);
    }

    /**
     * Returns a 64-bit affinity bitmask with all cores set to 1.
     */
    public static long getAllCoresMask() {
        if (nativeLoaded) {
            try {
                long mask = nativeGetAllCoresMask();
                if (mask > 0) return mask;
            } catch (Throwable ignored) {}
        }
        int total = getTotalCoreCount();
        return total >= 64 ? -1L : (1L << total) - 1L;
    }

    /**
     * Enables and enforces ALL CPU cores across the entire game process and all threads.
     * Starts a watchdog to ensure new threads also utilize all cores.
     */
    public static void enableAllCores() {
        // Boost priority of current calling thread
        try {
            Process.setThreadPriority(Process.myTid(), Process.THREAD_PRIORITY_URGENT_DISPLAY);
        } catch (Throwable ignored) {}

        // Native enforcement via Rust all_cores engine
        if (nativeLoaded) {
            try {
                nativeEnableAllCores();
                nativeStartCpuBoost(2); // Start aggressive native CPU governor boost
                Log.i(TAG, "All CPU cores enabled and native booster active (Total cores: " + getTotalCoreCount() + ", Mask: 0x" + Long.toHexString(getAllCoresMask()) + ")");
            } catch (Throwable t) {
                Log.w(TAG, "nativeEnableAllCores / boost failed: " + t.getMessage());
            }
        }

        // Java watchdog supervisor
        if (sInitialized.compareAndSet(false, true)) {
            startJavaCoreSupervisor();
        }
    }

    /**
     * Initializes all cores enforcement from Activity lifecycle.
     */
    public static void init(Activity activity) {
        enableAllCores();
    }

    private static void startJavaCoreSupervisor() {
        try {
            sWatcherService = Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "AllCoresWatcher");
                t.setDaemon(true);
                return t;
            });
            sWatcherService.scheduleWithFixedDelay(AllCpuCoresManager::scanAndEnforceAllCores, 1, 2, TimeUnit.SECONDS);
        } catch (Throwable t) {
            Log.w(TAG, "Failed starting Java core supervisor: " + t.getMessage());
        }
    }

    private static void scanAndEnforceAllCores() {
        if (nativeLoaded) {
            try {
                nativeEnableAllCores();
            } catch (Throwable ignored) {}
        }
    }

    public static float getCpuBoostThroughputMflops() {
        if (nativeLoaded) {
            try {
                return nativeGetCpuBoostMflops();
            } catch (Throwable ignored) {}
        }
        return 0.0f;
    }

    public static int getCpuBoostActiveThreads() {
        if (nativeLoaded) {
            try {
                return nativeGetCpuBoostActiveThreads();
            } catch (Throwable ignored) {}
        }
        return 0;
    }

    public static void setBoostMode(int mode) {
        if (nativeLoaded) {
            try {
                nativeSetCpuBoostMode(mode);
            } catch (Throwable ignored) {}
        }
    }
}
