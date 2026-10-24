package com.nfsu2.androidevolved;

/**
 * JNI native bridge matching compiled symbols in libnfsu2_diagnostics.so.
 */
public final class GpuProbeActivity {
    static {
        System.loadLibrary("nfsu2_diagnostics");
    }

    public static native String nativeGpuReport(String hooks, String temp, String directory, String library);
}
