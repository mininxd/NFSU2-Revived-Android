package io.mininxd.nfsu2.net;

import android.util.Log;

/**
 * JNI Bridge to libnfsu2_roam native free-roam engine.
 */
public final class RoamNativeBridge {
    private static final String TAG = "RoamNativeBridge";
    private static volatile boolean libraryLoaded = false;

    public interface NativeSendListener {
        void onNativeSend(int dstIp, int dstPort, byte[] data);
    }

    private static volatile NativeSendListener sendListener = null;

    static {
        try {
            System.loadLibrary("nfsu2_roam");
            libraryLoaded = true;
            Log.i(TAG, "Native free roam companion (libnfsu2_roam) loaded successfully");
        } catch (Throwable t) {
            libraryLoaded = false;
            Log.w(TAG, "libnfsu2_roam not loaded: " + t.getMessage());
        }
    }

    public static boolean isLoaded() {
        return libraryLoaded;
    }

    public static void setSendListener(NativeSendListener listener) {
        sendListener = listener;
    }

    public static native void nativeRoamConfigure(String room, String name, String peers, boolean ghostMode, int hubIp);
    public static native void nativeRoamReceive(int srcIp, int srcPort, byte[] data);
    public static native void nativeRoamTick();
    public static native void nativeRoamSay(int phrase);
    public static native void nativeRoamStick(float x, float y);
    public static native void nativeRoamHorn(boolean held);

    /**
     * Called from C roam_android.c when the engine wants to transmit a packet.
     */
    public static void onNativeSend(int dstIp, int dstPort, byte[] data) {
        NativeSendListener l = sendListener;
        if (l != null && data != null && data.length > 0) {
            l.onNativeSend(dstIp, dstPort, data);
        }
    }

    /**
     * Called from C roam_android.c when a quick chat phrase or group opens.
     */
    public static void onQuickChatReceived(String json) {
        QuickChatManager.onQuickChatReceived(json);
    }
}
