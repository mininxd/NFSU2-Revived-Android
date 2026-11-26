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

    public interface LanUdpSendListener {
        void onLanUdpSend(int dstVip, int sport, int dport, byte[] data);
    }

    public interface LanUdpBindListener {
        void onLanUdpBind(int port);
    }

    private static volatile NativeSendListener sendListener = null;
    private static volatile LanUdpSendListener lanUdpSendListener = null;
    private static volatile LanUdpBindListener lanUdpBindListener = null;

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

    public static void setLanUdpSendListener(LanUdpSendListener listener) {
        lanUdpSendListener = listener;
    }

    public static void setLanUdpBindListener(LanUdpBindListener listener) {
        lanUdpBindListener = listener;
    }

    public static native void nativeSetGameDataDir(String path);
    public static native void nativeRoamConfigure(String room, String name, String peers, boolean ghostMode, int hubIp);
    public static native void nativeRoamReceive(int srcIp, int srcPort, byte[] data);
    public static native void nativeLanUdpReceive(int srcVip, int dstVip, int sport, int dport, byte[] data);
    public static native void nativeSetSwitchStatus(boolean connected, int myVip);
    public static native void nativeRoamTick();
    public static native void nativeRoamSay(int phrase);
    public static native void nativeRoamStick(float x, float y);
    public static native void nativeGamepadAxes(int[] deviceIds, int[] axisModes);
    public static native void nativeRoamHorn(boolean held);
    public static native void nativeSetFrameLimit(int fps);
    public static native int[] nativeGetActionKeys();
    public static native long nativeGetFrameCount();
    public static native float nativeGetCarSpeed();
    public static native float nativeGetCarSpeedKmH();
    public static native int nativeGetCarNos();
    public static native boolean nativeIsNosActive();

    public static void setGameDataDir(String path) {
        if (path == null || path.trim().isEmpty()) return;
        try {
            nativeSetGameDataDir(path.trim());
        } catch (Throwable t) {
            Log.w(TAG, "nativeSetGameDataDir failed: " + t.getMessage());
        }
    }

    public static float getCarSpeedKmH() {
        try {
            return nativeGetCarSpeedKmH();
        } catch (Throwable t) {
            return 0.0f;
        }
    }

    public static int getCarNos() {
        try {
            return nativeGetCarNos();
        } catch (Throwable t) {
            return 0;
        }
    }

    public static boolean isNosActive() {
        try {
            return nativeIsNosActive();
        } catch (Throwable t) {
            return false;
        }
    }

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
     * Called from C roam_android.c when the guest transmits an Online/LAN UDP datagram.
     */
    public static void onNativeLanUdpSend(int dstVip, int sport, int dport, byte[] data) {
        LanUdpSendListener l = lanUdpSendListener;
        if (l != null && data != null && data.length > 0) {
            l.onLanUdpSend(dstVip, sport, dport, data);
        }
    }

    /**
     * Called from C roam_android.c when the guest binds a LAN/Online UDP port.
     */
    public static void onNativeLanUdpBind(int port) {
        LanUdpBindListener l = lanUdpBindListener;
        if (l != null && port > 0) {
            l.onLanUdpBind(port);
        }
    }

    /**
     * Called from C roam_android.c when a quick chat phrase or group opens.
     */
    public static void onQuickChatReceived(String json) {
        QuickChatManager.onQuickChatReceived(json);
    }
}
