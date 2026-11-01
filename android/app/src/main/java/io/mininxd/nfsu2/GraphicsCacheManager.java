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

    public static boolean isAvailable() {
        return nativeLoaded;
    }
}
