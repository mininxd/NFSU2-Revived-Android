package io.mininxd.nfsu2;

import android.util.Log;

/**
 * Asynchronous graphics cache & compiler manager connected to the Rust graphics_cache engine.
 */
public final class GraphicsCacheManager {
    private static final String TAG = "NFSU2-GfxCache";
    private static boolean nativeLoaded = false;

    static {
        try {
            System.loadLibrary("graphics_cache");
            nativeLoaded = true;
            Log.i(TAG, "Loaded Rust libgraphics_cache.so");
        } catch (Throwable t) {
            Log.d(TAG, "Native graphics_cache library not present: " + t.getMessage());
        }
    }

    public static native int nativeGetStatus(long pipelineHash);
    public static native long nativeGetCompletedCount();
    public static native void nativeSetCrowdSuppression(boolean suppressed);
    public static native boolean nativeIsCrowdSuppressed();
    public static native void nativeSetContrastMode(int mode);
    public static native int nativeGetContrastMode();

    public static void setContrastMode(int mode) {
        if (nativeLoaded) {
            try {
                nativeSetContrastMode(mode);
            } catch (Throwable ignored) {}
        }
    }

    public static int getContrastMode() {
        if (nativeLoaded) {
            try {
                return nativeGetContrastMode();
            } catch (Throwable ignored) {}
        }
        return 1;
    }

    public static void setCrowdSuppression(boolean suppressed) {
        if (nativeLoaded) {
            try {
                nativeSetCrowdSuppression(suppressed);
            } catch (Throwable ignored) {}
        }
    }

    public static boolean isCrowdSuppressed() {
        if (nativeLoaded) {
            try {
                return nativeIsCrowdSuppressed();
            } catch (Throwable ignored) {}
        }
        return true;
    }

    public static boolean isAvailable() {
        return nativeLoaded;
    }
}
