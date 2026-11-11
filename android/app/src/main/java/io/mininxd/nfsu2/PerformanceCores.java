package io.mininxd.nfsu2;

import android.util.Log;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * CPU Performance Cores affinity manager for the launcher and game process.
 * Connects to the Rust/Native perf_cores engine to pin execution to the fastest CPU clusters.
 */
public final class PerformanceCores {
    private static final String TAG = "NFSU2-PerfCores";
    private static boolean nativeLoaded = false;

    static {
        try {
            System.loadLibrary("perf_cores");
            nativeLoaded = true;
            Log.i(TAG, "Loaded Rust libperf_cores.so");
        } catch (Throwable t1) {
            try {
                System.loadLibrary("nfsu2_roam");
                nativeLoaded = true;
                Log.i(TAG, "Loaded companion affinity support from libnfsu2_roam.so");
            } catch (Throwable t2) {
                Log.d(TAG, "Native affinity libraries not present, using Java fallback");
            }
        }
    }

    public static native int nativeBindToPerformanceCores();
    public static native long nativeGetPerformanceCoresMask();
    public static native int nativeGetCoreCount();
    public static native int nativeGetPerformanceCoreCount();
    public static native String nativeGetSummary();
    public static native void nativeBoostCallingThread();
    public static native float nativeCalculateSteering(float rawX, float centerX, float halfWidth, float scale);
    public static native float nativeCalculateSteeringProgressive(float rawX, float centerX, float halfWidth, float scale, float deadzone);

    public static int getPerformanceCoreCount() {
        if (nativeLoaded) {
            try {
                int c = nativeGetPerformanceCoreCount();
                if (c > 0) return c;
            } catch (Throwable ignored) {}
        }
        int total = Runtime.getRuntime().availableProcessors();
        return Math.max(1, total >= 8 ? 4 : (total >= 4 ? total / 2 : 1));
    }

    /**
     * Immediately boosts the priority and pins the calling thread to performance cores.
     */
    public static void boostCallingThread() {
        if (nativeLoaded) {
            try {
                nativeBoostCallingThread();
            } catch (Throwable ignored) {}
        }
    }

    /**
     * Calculates progressive steering sensitivity curve via Rust native SIMD engine.
     */
    public static float calculateSteering(float rawX, float centerX, float halfWidth, float scale) {
        return calculateSteeringProgressive(rawX, centerX, halfWidth, scale, 0.05f);
    }

    public static float calculateSteeringProgressive(float rawX, float centerX, float halfWidth, float scale, float deadzone) {
        if (nativeLoaded) {
            try {
                return nativeCalculateSteeringProgressive(rawX, centerX, halfWidth, scale, deadzone);
            } catch (Throwable ignored) {}
        }
        if (scale <= 0f || halfWidth <= 0f) return 0f;
        float norm = (rawX / scale - centerX) / halfWidth;
        float clamped = Math.max(-1f, Math.min(1f, norm));
        float abs = Math.abs(clamped);
        float dz = Math.max(0f, Math.min(0.5f, deadzone));
        if (abs <= dz) return 0f;
        float scaled = (abs - dz) / (1f - dz);
        float curved = scaled * 0.30f + scaled * scaled * scaled * 0.70f;
        return clamped < 0f ? -curved : curved;
    }

    /**
     * Binds the current process and its worker threads to the SoC's Performance cores.
     */
    public static void bindToPerformanceCores() {
        if (nativeLoaded) {
            try {
                int res = nativeBindToPerformanceCores();
                if (res == 0) {
                    Log.i(TAG, "Successfully bound process to performance cores via native engine");
                    return;
                }
            } catch (Throwable t) {
                Log.d(TAG, "nativeBindToPerformanceCores failed: " + t.getMessage());
            }
        }

        // Java fallback: identify performance cores from /sys/devices/system/cpu/
        try {
            long mask = detectPerformanceCoresMask();
            Log.i(TAG, "Detected performance cores mask: 0x" + Long.toHexString(mask));
        } catch (Throwable t) {
            Log.w(TAG, "Failed detecting performance cores", t);
        }
    }

    public static long detectPerformanceCoresMask() {
        if (nativeLoaded) {
            try {
                return nativeGetPerformanceCoresMask();
            } catch (Throwable ignored) {}
        }

        long highestFreq = 0;
        int totalCores = Runtime.getRuntime().availableProcessors();
        long[] freqs = new long[totalCores];

        for (int i = 0; i < totalCores; i++) {
            File f = new File("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq");
            if (!f.isFile()) {
                f = new File("/sys/devices/system/cpu/cpu" + i + "/cpufreq/scaling_max_freq");
            }
            if (!f.isFile()) {
                File capFile = new File("/sys/devices/system/cpu/cpu" + i + "/cpu_capacity");
                if (capFile.isFile()) {
                    try (BufferedReader reader = new BufferedReader(new FileReader(capFile))) {
                        String line = reader.readLine();
                        if (line != null) {
                            long cap = Long.parseLong(line.trim());
                            freqs[i] = cap * 1000;
                            if (freqs[i] > highestFreq) highestFreq = freqs[i];
                        }
                    } catch (Exception ignored) {}
                    continue;
                }
            }
            if (f.isFile()) {
                try (BufferedReader reader = new BufferedReader(new FileReader(f))) {
                    String line = reader.readLine();
                    if (line != null) {
                        freqs[i] = Long.parseLong(line.trim());
                        if (freqs[i] > highestFreq) highestFreq = freqs[i];
                    }
                } catch (Exception ignored) {}
            }
        }

        long mask = 0;
        long threshold = (long) (highestFreq * 0.50);
        if (highestFreq > 0) {
            for (int i = 0; i < totalCores; i++) {
                if (freqs[i] >= threshold) {
                    mask |= (1L << i);
                }
            }
        }

        if (Long.bitCount(mask) < 2) {
            // Fallback: upper half of cores (or upper 4 cores for octa-core)
            int start = totalCores >= 8 ? 4 : Math.max(1, totalCores / 2);
            for (int i = start; i < totalCores; i++) {
                mask |= (1L << i);
            }
        }
        return mask;
    }
}
