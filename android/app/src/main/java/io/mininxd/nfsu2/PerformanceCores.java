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
    public static native String nativeGetSummary();

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

        if (mask == 0) {
            // Fallback: upper half of cores
            int start = Math.max(1, totalCores / 2);
            for (int i = start; i < totalCores; i++) {
                mask |= (1L << i);
            }
        }
        return mask;
    }
}
