package com.nfsu2.androidevolved;

/**
 * JNI native bridge matching compiled symbols in libnfsu2_android.so.
 */
public final class GameActivity {
    static {
        System.loadLibrary("nfsu2_android");
    }

    public static native void nativeKey(int scan, boolean down);
    public static native boolean nativeDrivingControls();
    public static native boolean nativeGamepadUsed();
    public static native void nativeTypeText(String value, boolean replace);
    public static native void nativePointer(float x, float y, int phase);
    public static native void nativeLanguage(String language);
    public static native void nativeResolution(int width, int height);
    public static native void nativeFrameLimit(int cap);
    public static native void nativeWidescreen(boolean enabled);
    public static native void nativeOnlineServer(String host);
    public static native void nativeGamepadConnected(boolean connected);
    public static native void nativeGamepadDevices(int[] ids);
    public static native void nativeMinimapTop(boolean top);
    public static native void nativeBackBuffers(int count);
    public static native void nativeCpuTextures(boolean enabled);
    public static native void nativeGpuDriver(String hooks, String temp, String directory, String library);
}
