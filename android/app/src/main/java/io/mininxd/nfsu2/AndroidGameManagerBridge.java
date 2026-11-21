package io.mininxd.nfsu2;

import android.app.Activity;
import android.app.GameManager;
import android.app.GameState;
import android.os.Build;
import android.util.Log;
import android.view.Window;
import android.view.WindowManager;

/**
 * Dedicated Android Game Development Kit (AGDK) platform bridge.
 * Integrates directly with Android 12/13/14 GameManager and GameState APIs
 * to signal active gameplay mode, prevent compositor down-throttling to 30Hz,
 * and request high-refresh display scheduling.
 * Powered by pure Rust perf_cores native AGDK integration.
 */
public final class AndroidGameManagerBridge {
    private static final String TAG = "NFSU2-GameManager";
    private static volatile boolean initialized = false;
    private static boolean nativeLoaded = false;

    static {
        try {
            System.loadLibrary("perf_cores");
            nativeLoaded = true;
        } catch (Throwable t) {
            Log.d(TAG, "libperf_cores not loaded for GameManager: " + t.getMessage());
        }
    }

    public static native void nativeSetGameplayMode(Activity activity, int targetFps);
    public static native void nativeSetLoadingMode(Activity activity);

    private AndroidGameManagerBridge() {}

    /**
     * Initializes the Android Game State pipeline and requests active gameplay performance.
     *
     * @param activity  Target game activity.
     * @param targetFps Target frame rate (e.g. 60).
     */
    public static void setGameplayMode(Activity activity, int targetFps) {
        if (activity == null) return;

        if (nativeLoaded) {
            try {
                nativeSetGameplayMode(activity, targetFps);
                initialized = true;
                return;
            } catch (Throwable t) {
                Log.d(TAG, "nativeSetGameplayMode failed, using Java fallback: " + t.getMessage());
            }
        }

        // Java fallback
        try {
            Window window = activity.getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        } catch (Throwable ignored) {}

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            try {
                GameManager gm = activity.getSystemService(GameManager.class);
                if (gm != null) {
                    int fps = (targetFps > 0) ? targetFps : 60;
                    GameState gameState = new GameState(
                            false,
                            GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE,
                            GameState.MODE_NONE,
                            fps
                    );
                    gm.setGameState(gameState);
                    Log.i(TAG, "Notified Android GameManager of active gameplay at " + fps + " FPS target");
                }
            } catch (Throwable t) {
                Log.w(TAG, "Failed to apply GameState: " + t.getMessage());
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                GameManager gm = activity.getSystemService(GameManager.class);
                if (gm != null) {
                    int mode = gm.getGameMode();
                    Log.i(TAG, "Android 12 GameManager active mode: " + mode);
                }
            } catch (Throwable t) {
                Log.d(TAG, "GameManager check: " + t.getMessage());
            }
        }

        initialized = true;
    }

    /**
     * Notifies Android when the game is loading assets/tracks to optimize disk I/O.
     */
    public static void setLoadingMode(Activity activity) {
        if (activity == null) return;

        if (nativeLoaded) {
            try {
                nativeSetLoadingMode(activity);
                return;
            } catch (Throwable ignored) {}
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;

        try {
            GameManager gm = activity.getSystemService(GameManager.class);
            if (gm != null) {
                GameState gameState = new GameState(true, GameState.MODE_CONTENT);
                gm.setGameState(gameState);
                Log.i(TAG, "Notified Android GameManager of loading state");
            }
        } catch (Throwable ignored) {}
    }

    public static boolean isInitialized() {
        return initialized;
    }
}
