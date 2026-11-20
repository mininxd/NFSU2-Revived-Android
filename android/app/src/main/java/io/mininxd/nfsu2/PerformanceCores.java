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
    public static native long nativeGetPrimeCoresMask();
    public static native long nativeGetMidCoresMask();
    public static native long nativeGetLittleCoresMask();
    public static native int nativeGetCoreCount();
    public static native String nativeGetSummary();
    public static native void nativeBoostCallingThread();
    public static native float nativeCalculateSteering(float rawX, float centerX, float halfWidth, float scale);
    public static native float nativeCalculateSteeringProgressive(float rawX, float centerX, float halfWidth, float scale, float deadzone);

    public static long getPrimeCoresMask() {
        if (nativeLoaded) {
            try {
                return nativeGetPrimeCoresMask();
            } catch (Throwable ignored) {}
        }
        return detectPerformanceCoresMask();
    }

    public static long getMidCoresMask() {
        if (nativeLoaded) {
            try {
                return nativeGetMidCoresMask();
            } catch (Throwable ignored) {}
        }
        return 0L;
    }

    public static long getLittleCoresMask() {
        if (nativeLoaded) {
            try {
                return nativeGetLittleCoresMask();
            } catch (Throwable ignored) {}
        }
        return 0L;
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
        long threshold = (long) (highestFreq * 0.80);
        if (highestFreq > 0) {
            for (int i = 0; i < totalCores; i++) {
                if (freqs[i] >= threshold) {
                    mask |= (1L << i);
                }
            }
        }

        // Secondary detection: Parse /proc/cpuinfo (always permitted by SELinux on Android 14)
        if (mask == 0) {
            try (BufferedReader reader = new BufferedReader(new FileReader("/proc/cpuinfo"))) {
                String line;
                int currentCpu = -1;
                while ((line = reader.readLine()) != null) {
                    line = line.trim();
                    if (line.startsWith("processor")) {
                        int colon = line.indexOf(':');
                        if (colon >= 0) {
                            try {
                                currentCpu = Integer.parseInt(line.substring(colon + 1).trim());
                            } catch (Exception ignored) {}
                        }
                    } else if (line.startsWith("CPU part") && currentCpu >= 0 && currentCpu < totalCores) {
                        int colon = line.indexOf(':');
                        if (colon >= 0) {
                            String partStr = line.substring(colon + 1).trim();
                            try {
                                int part = Integer.decode(partStr);
                                boolean isLittle = (part == 0xd03 || part == 0xd05 || part == 0xd46 || part == 0xd80);
                                if (part >= 0xd08 && !isLittle) {
                                    mask |= (1L << currentCpu);
                                }
                            } catch (Exception ignored) {}
                        }
                    }
                }
            } catch (Exception ignored) {}
        }

        if (mask == 0) {
            // Universal fallback: enable all CPU cores to allow the kernel EAS/WALT governor
            // to dynamically schedule across all available cores without artificial throttling.
            mask = (1L << totalCores) - 1;
        }
        return mask;
    }
}
