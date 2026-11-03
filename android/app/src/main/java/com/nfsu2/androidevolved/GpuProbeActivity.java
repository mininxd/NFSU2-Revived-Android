package com.nfsu2.androidevolved;

import android.util.Log;

/**
 * JNI native bridge matching compiled symbols in libnfsu2_diagnostics.so.
 */
public final class GpuProbeActivity {
    static {
        try {
            System.loadLibrary("nfsu2_diagnostics");
        } catch (Throwable t) {
            Log.w("NFSU2", "libnfsu2_diagnostics not available: " + t.getMessage());
        }
    }

    public static native String nativeGpuReport(String hooks, String temp, String directory, String library);
}
