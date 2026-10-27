package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.security.SecureRandom;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

import io.mininxd.nfsu2.net.AdhocPostofficeClient;
import io.mininxd.nfsu2.net.LocalOnlineBridge;
import io.mininxd.nfsu2.net.NetLog;
import io.mininxd.nfsu2.net.QuickChatManager;
import io.mininxd.nfsu2.net.RoamNativeBridge;
import io.mininxd.nfsu2.net.ServerApiClient;
import io.mininxd.nfsu2.net.VirtualSwitchClient;

/**
 * Adhoc Free Roam Client Facade for NFSU2 Revived.
 * Delegates networking to modular subclients in io.mininxd.nfsu2.net:
 * - ServerApiClient (HTTP /players polling, FNV-1a room hash, /vault cloud save)
 * - VirtualSwitchClient (WSS /lan WebSocket switch)
 * - LocalOnlineBridge (Online mode virtual host bridge at 10.77.0.1)
 * - AdhocPostofficeClient (Prometheus adhocctl 27312 and postoffice 27313)
 * - QuickChatManager (Quick Chat dialogs, toasts, radial phrases)
 * - RoamNativeBridge (libnfsu2_roam JNI interface)
 */
public final class AdhocRoamClient {
    private static final String TAG = "AdhocRoam";
    private static final AdhocRoamClient INSTANCE = new AdhocRoamClient();

    public static final int MODE_ADHOC = 1;
    public static final int MODE_WSS = 2;
    public static final int DEFAULT_GAME_PORT = 47210;
    public static final int LOCAL_RELAY_PORT = 47211;

    public static final String[][] QUICK_CHAT_GROUPS = QuickChatManager.QUICK_CHAT_GROUPS;
    public static final String[] GROUP_NAMES = QuickChatManager.GROUP_NAMES;

    public static class Peer extends io.mininxd.nfsu2.net.Peer {
        public Peer(String name, byte[] mac, int ip, int virtualPort) {
            super(name, mac, ip, virtualPort);
        }
    }

    // Native JNI Method Declarations delegating to RoamNativeBridge
    public static void nativeRoamConfigure(String room, String name, String peers, boolean ghostMode, int hubIp) {
        if (RoamNativeBridge.isLoaded()) {
            try {
                RoamNativeBridge.nativeRoamConfigure(room, name, peers, ghostMode, hubIp);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamConfigure: " + t.getMessage());
            }
        }
    }

    public static void nativeRoamReceive(int srcIp, int srcPort, byte[] data) {
        if (RoamNativeBridge.isLoaded()) {
            try {
                RoamNativeBridge.nativeRoamReceive(srcIp, srcPort, data);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamReceive: " + t.getMessage());
            }
        }
    }

    public static void nativeRoamTick() {
        if (RoamNativeBridge.isLoaded()) {
            try {
                RoamNativeBridge.nativeRoamTick();
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamTick: " + t.getMessage());
            }
        }
    }

    public static void nativeRoamSay(int phrase) {
        if (RoamNativeBridge.isLoaded()) {
            try {
                RoamNativeBridge.nativeRoamSay(phrase);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamSay: " + t.getMessage());
            }
        }
    }

    public static void nativeRoamStick(float x, float y) {
        if (RoamNativeBridge.isLoaded()) {
            try {
                RoamNativeBridge.nativeRoamStick(x, y);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamStick: " + t.getMessage());
            }
        }
    }

    public static void nativeRoamHorn(boolean held) {
        if (RoamNativeBridge.isLoaded()) {
            try {
                RoamNativeBridge.nativeRoamHorn(held);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamHorn: " + t.getMessage());
            }
        }
    }

    static {
        try {
            System.loadLibrary("nfsu2_roam");
        } catch (Throwable t) {
            Log.d(TAG, "libnfsu2_roam not loaded directly: " + t.getMessage());
        }
        RoamNativeBridge.setSendListener((dstIp, dstPort, data) -> INSTANCE.handleNativeSend(dstIp, dstPort, data));
    }

    public static void onNativeSend(int dstIp, int dstPort, byte[] data) {
        if (!INSTANCE.running.get() || data == null || data.length == 0) return;
        INSTANCE.handleNativeSend(dstIp, dstPort, data);
    }

    public static void onQuickChatReceived(String json) {
        QuickChatManager.onQuickChatReceived(json);
    }

    public static void setHorn(boolean held) {
        try {
            nativeRoamHorn(held);
        } catch (Throwable t) {
            Log.d(TAG, "nativeRoamHorn: " + t.getMessage());
        }
    }

    public static void handleRightStick(float x, float y) {
        QuickChatManager.handleRightStick(x, y);
    }

    public static void showQuickChatDialog(Context context) {
        QuickChatManager.showQuickChatDialog(context);
    }

    public static void sendQuickChat(int phraseIndex) {
        QuickChatManager.sendQuickChat(phraseIndex);
    }

    public static int roomHash(String name) {
        return ServerApiClient.roomHash(name);
    }

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean racingActive = new AtomicBoolean(false);
    private final AtomicBoolean adhocConnected = new AtomicBoolean(false);

    private final ServerApiClient apiClient = new ServerApiClient();
    private final VirtualSwitchClient switchClient = new VirtualSwitchClient();
    private final LocalOnlineBridge onlineBridge = new LocalOnlineBridge(switchClient);
    private final AdhocPostofficeClient postofficeClient = new AdhocPostofficeClient();

    private final Map<String, Peer> peers = new ConcurrentHashMap<>();

    private volatile int activeMode = MODE_ADHOC;
    private Context appContext;
    private String serverHost = GameOptions.DEFAULT_ROAM_SERVER;
    private String roomName = GameOptions.DEFAULT_ROAM_ROOM;
    private String playerName = "Racer";
    private volatile String connectionStatus = "disconnected";
    private byte[] localMac = new byte[6];
    private boolean ghostMode = true;

    private Thread playersPollerThread;
    private Thread roamTickThread;

    private AdhocRoamClient() {
        switchClient.setOnlineBridge(onlineBridge);
        switchClient.setListener(new VirtualSwitchClient.SwitchListener() {
            @Override
            public void onConnected(int virtualIp) {
                adhocConnected.set(true);
                connectionStatus = "connected to " + serverHost;
                Log.i(TAG, "connected to " + serverHost + " (VIP: " + virtualIp + ")");
                NetLog.success(TAG, "Connected to switch: VIP=" + NetLog.ipStr(virtualIp));
                configureNativeRoam(0x0A4D00FD); // Hub at 10.77.0.253
                if (isDisplayLogsEnabled()) {
                    new Thread(() -> apiClient.pollServerPlayers(serverHost, roomName, null), "InitialPoller").start();
                }
            }

            @Override
            public void onDisconnected(String reason) {
                adhocConnected.set(false);
                connectionStatus = "error: " + reason;
                NetLog.fail(TAG, "Switch disconnected: " + reason);
            }

            @Override
            public void onPeersCountUpdated(int totalPeers) {
                if (totalPeers >= 0) {
                    apiClient.setRoomRoamPlayers(totalPeers);
                    NetLog.i(TAG, "Total peers count: " + totalPeers);
                }
            }

            @Override
            public void onDatagramReceived(int srcVip, int sport, byte[] payload) {
                if (sport == 9999) {
                    // Ignore broadcast LAN announcements from server-rs to prevent buffer and JNI saturation
                    return;
                }
                if (srcVip != 0x0A4D00FD) {
                    trackWsPeer(srcVip);
                }
                NetLog.i(TAG, "Datagram received from " + NetLog.ipStr(srcVip) + ":" + sport
                        + " (len=" + (payload != null ? payload.length : 0) + ", hex=" + NetLog.hexPreview(payload, 16) + ")");
                if (RoamNativeBridge.isLoaded()) {
                    try {
                        nativeRoamReceive(srcVip, sport, payload);
                    } catch (Throwable t) {
                        Log.d(TAG, "nativeRoamReceive: " + t.getMessage());
                        NetLog.fail(TAG, "nativeRoamReceive failed: " + t.getMessage(), t);
                    }
                }
            }
        });

        postofficeClient.setListener(new AdhocPostofficeClient.PostofficeListener() {
            @Override
            public void onConnected(String bssid) {
                adhocConnected.set(true);
                connectionStatus = "connected to " + serverHost;
            }

            @Override
            public void onDisconnected(String reason) {
                adhocConnected.set(false);
                connectionStatus = "error: " + reason;
            }

            @Override
            public void onPeerJoined(io.mininxd.nfsu2.net.Peer peer) {
                peers.put(peer.getMacString(), new Peer(peer.name, peer.mac, peer.ip, peer.virtualPort));
            }

            @Override
            public void onPeerLeft(int peerIp) {
                for (Map.Entry<String, Peer> entry : peers.entrySet()) {
                    if (entry.getValue().ip == peerIp) {
                        peers.remove(entry.getKey());
                        break;
                    }
                }
            }

            @Override
            public void onDatagramReceived(byte[] srcMac, byte[] payload) {
                String key = formatMac(srcMac);
                Peer p = peers.get(key);
                int srcIp = (p != null && p.ip != 0) ? p.ip :
                        (((srcMac[2] & 0xFF) << 24) | ((srcMac[3] & 0xFF) << 16) | ((srcMac[4] & 0xFF) << 8) | (srcMac[5] & 0xFF));
                if (RoamNativeBridge.isLoaded()) {
                    try {
                        nativeRoamReceive(srcIp, DEFAULT_GAME_PORT, payload);
                    } catch (Throwable t) {
                        Log.d(TAG, "nativeRoamReceive: " + t.getMessage());
                    }
                }
            }
        });
    }

    public static AdhocRoamClient getInstance() {
        return INSTANCE;
    }

    public boolean isConnected() { return running.get() && adhocConnected.get(); }
    public boolean isRacing() { return racingActive.get(); }
    public void setRacing(boolean active) { racingActive.set(active); }
    public String getServerHost() { return serverHost; }
    public String getRoomName() { return roomName; }
    public String getConnectionStatus() { return connectionStatus; }

    public int getTotalServerPlayers() { return apiClient.getTotalServerPlayers(); }
    public int getTotalRoamPlayers() { return apiClient.getTotalRoamPlayers(); }
    public int getTotalOnlinePlayers() { return apiClient.getTotalOnlinePlayers(); }
    public int getRoomRoamPlayers() { return apiClient.getRoomRoamPlayers(); }

    public int getPeerCount() { return peers.size(); }
    public Map<String, Peer> getPeers() { return peers; }

    public String getActiveModeName() {
        return activeMode == MODE_WSS ? "WebSocket (WSS)" : "Prometheus Adhoc";
    }

    public synchronized void start(Context context, String host, String room, String name) {
        start(context, host, room, name, GameOptions.isGhostModeEnabled(context));
    }

    public synchronized void start(Context context, String host, String room, String name, boolean ghostMode) {
        QuickChatManager.setContext(context);
        this.appContext = context != null ? context.getApplicationContext() : null;
        this.ghostMode = ghostMode;
        if (running.get()) {
            if (host.equals(serverHost) && room.equals(roomName) && name.equals(playerName)) {
                return;
            }
            stop();
        }

        this.serverHost = (host != null && !host.trim().isEmpty()) ? host.trim() : GameOptions.roamServer(context);
        this.roomName = (room != null) ? room.trim() : GameOptions.roamRoom(context);
        if (name == null || name.trim().isEmpty() || "Racer".equalsIgnoreCase(name)) {
            name = GameOptions.detectSavegamePlayerName();
        }
        this.playerName = (name != null && !name.trim().isEmpty()) ? name.trim() : "Racer";
        this.localMac = getOrCreateVirtualMac(context);

        NetLog.init(context);
        NetLog.step(TAG, "Starting Adhoc Roam: Server=" + serverHost + ", Room=" + roomName
                + ", Name=" + playerName + ", Ghost=" + ghostMode + ", MAC=" + formatMac(localMac));

        running.set(true);
        peers.clear();
        connectionStatus = "connecting to " + serverHost + "...";

        Log.i(TAG, "Starting Adhoc Roam: Server=" + serverHost + " Room=" + roomName
                + " Name=" + playerName + " Ghost=" + ghostMode + " MAC=" + formatMac(localMac));

        boolean isWs = ServerApiClient.isWsHost(serverHost);
        activeMode = isWs ? MODE_WSS : MODE_ADHOC;
        NetLog.step(TAG, "Backend selected: " + (isWs ? "WebSocket Virtual Switch (WSS)" : "Prometheus Adhoc"));
        configureNativeRoam(isWs ? 0x0A4D00FD : 0);

        new Thread(() -> {
            if (!running.get()) return;
            Log.i(TAG, "Backend selected for " + serverHost + ": " + (isWs ? "WebSocket Virtual Switch (WSS)" : "Prometheus Adhoc"));

            if (isWs) {
                onlineBridge.start();
                switchClient.start(serverHost, playerName);
                startPlayersPollerThread();
            } else {
                postofficeClient.start(serverHost, roomName, playerName, localMac);
            }
        }, "BackendSelector").start();

        startRoamTickThread();
    }

    public synchronized void stop() {
        if (!running.getAndSet(false)) return;

        Log.i(TAG, "Stopping Adhoc Roam client");
        NetLog.step(TAG, "Stopping Adhoc Roam client");
        connectionStatus = "disconnected";

        switchClient.stop();
        onlineBridge.stop();
        postofficeClient.stop();

        if (playersPollerThread != null) {
            try { playersPollerThread.interrupt(); } catch (Exception ignored) {}
        }
        if (roamTickThread != null) {
            try { roamTickThread.interrupt(); } catch (Exception ignored) {}
        }

        for (Peer p : peers.values()) {
            p.close();
        }
        peers.clear();
        racingActive.set(false);
        adhocConnected.set(false);
    }

    private void configureNativeRoam(int hubIp) {
        if (RoamNativeBridge.isLoaded()) {
            try {
                String peerList = (hubIp != 0) ? "" : ("127.0.0.1:" + LOCAL_RELAY_PORT);
                NetLog.step(TAG, "nativeRoamConfigure: Room=" + roomName + ", Name=" + playerName
                        + ", Peers='" + peerList + "', Hub=" + (hubIp != 0 ? NetLog.ipStr(hubIp) : "none"));
                nativeRoamConfigure(roomName, playerName, peerList, ghostMode, hubIp);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamConfigure: " + t.getMessage());
                NetLog.fail(TAG, "nativeRoamConfigure error: " + t.getMessage(), t);
            }
        }
    }

    private void startRoamTickThread() {
        if (!RoamNativeBridge.isLoaded()) return;
        roamTickThread = new Thread(() -> {
            while (running.get()) {
                try {
                    nativeRoamTick();
                    Thread.sleep(16);
                } catch (InterruptedException e) {
                    break;
                } catch (Throwable t) {
                    try { Thread.sleep(50); } catch (Exception ignored) { break; }
                }
            }
        }, "RoamTickWorker");
        roamTickThread.setDaemon(true);
        roamTickThread.start();
    }

    public boolean isDisplayLogsEnabled() {
        return appContext != null && GameOptions.isDisplayLogsEnabled(appContext);
    }

    private void startPlayersPollerThread() {
        playersPollerThread = new Thread(() -> {
            while (running.get()) {
                if (isDisplayLogsEnabled()) {
                    pollServerPlayers();
                }
                for (int i = 0; i < 50 && running.get(); i++) {
                    try { Thread.sleep(100); } catch (InterruptedException ignored) {}
                }
            }
        }, "PlayersPoller");
        playersPollerThread.setDaemon(true);
        playersPollerThread.start();
    }

    public void pollServerPlayers() {
        if (!isDisplayLogsEnabled()) return;
        apiClient.pollServerPlayers(serverHost, roomName, null);
    }

    public void queryServerPlayers(Context context, Runnable onDone) {
        if (!GameOptions.isDisplayLogsEnabled(context)) {
            if (onDone != null) onDone.run();
            return;
        }
        apiClient.queryServerPlayers(context, onDone);
    }

    private void handleNativeSend(int dstIp, int dstPort, byte[] data) {
        NetLog.i(TAG, "Native sending to " + NetLog.ipStr(dstIp) + ":" + dstPort
                + " (len=" + (data != null ? data.length : 0) + ", hex=" + NetLog.hexPreview(data, 16) + ")");
        if (activeMode == MODE_WSS) {
            sendWebSocketDgram(dstIp, dstPort, data);
        } else {
            postofficeClient.broadcastToPeers(data);
        }
    }

    public synchronized void sendWebSocketDgram(int dstVip, int dport, byte[] payload) {
        switchClient.sendDgram(dstVip, dport, payload);
    }

    public synchronized void broadcastToAdhocPeers(byte[] data) {
        if (!running.get() || data == null || data.length == 0) return;
        if (activeMode == MODE_WSS) {
            switchClient.sendDgram(0x0A4D00FD, DEFAULT_GAME_PORT, data);
            switchClient.sendDgram(0xFFFFFFFF, DEFAULT_GAME_PORT, data);
        } else {
            postofficeClient.broadcastToPeers(data);
        }
    }

    private void trackWsPeer(int srcVip) {
        String key = String.format("%02X:%02X:%02X:%02X:%02X:%02X",
                0x02, 0x77, (srcVip >> 24) & 0xFF, (srcVip >> 16) & 0xFF, (srcVip >> 8) & 0xFF, srcVip & 0xFF);
        Peer existing = peers.get(key);
        if (existing != null) {
            existing.lastSeen = System.currentTimeMillis();
            return;
        }
        byte[] mac = new byte[] { 0x02, 0x77, (byte) (srcVip >> 24), (byte) (srcVip >> 16), (byte) (srcVip >> 8), (byte) srcVip };
        Peer p = new Peer("Player", mac, srcVip, 0);
        peers.put(key, p);
    }

    private byte[] getOrCreateVirtualMac(Context context) {
        SharedPreferences prefs = context.getSharedPreferences("nfsu2_adhoc", Context.MODE_PRIVATE);
        String saved = prefs.getString("virtual_mac", null);
        if (saved != null && saved.length() == 12) {
            try {
                byte[] mac = new byte[6];
                for (int i = 0; i < 6; i++) {
                    mac[i] = (byte) Integer.parseInt(saved.substring(i * 2, i * 2 + 2), 16);
                }
                return mac;
            } catch (Exception ignored) {}
        }

        byte[] mac = new byte[6];
        new SecureRandom().nextBytes(mac);
        mac[0] = (byte) ((mac[0] & 0xFE) | 0x02); // Locally administered unicast

        StringBuilder sb = new StringBuilder();
        for (byte b : mac) sb.append(String.format("%02X", b));
        prefs.edit().putString("virtual_mac", sb.toString()).apply();
        return mac;
    }

    private static String formatMac(byte[] mac) {
        if (mac == null || mac.length < 6) return "00:00:00:00:00:00";
        return String.format("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}
