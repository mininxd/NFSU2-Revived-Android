package io.mininxd.nfsu2;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Base64;
import android.util.Log;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import javax.net.ssl.SSLSocketFactory;

/**
 * Adhoc Free Roam Client for NFSU2 Revived.
 * Connects to Prometheus Adhoc infrastructure (adhoc.eahub.eu:27312 adhocctl, 27313 postoffice PDP)
 * and bridges multiplayer Free Roam datagrams (N2FR) across internet clients without port forwarding.
 */
public final class AdhocRoamClient {
    private static final String TAG = "AdhocRoam";
    private static final AdhocRoamClient INSTANCE = new AdhocRoamClient();

    private static volatile boolean libraryLoaded = false;

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

    public static native void nativeRoamConfigure(String room, String name, String peers, boolean ghostMode, int hubIp);
    public static native void nativeRoamReceive(int srcIp, int srcPort, byte[] data);
    public static native void nativeRoamTick();
    public static native void nativeRoamSay(int phrase);
    public static native void nativeRoamStick(float x, float y);

    public static void onNativeSend(int dstIp, int dstPort, byte[] data) {
        if (!INSTANCE.running.get() || data == null || data.length == 0) return;
        INSTANCE.handleNativeSend(dstIp, dstPort, data);
    }

    public static final String[][] QUICK_CHAT_GROUPS = {
        { "Hey!", "Nice car!", "Thanks!", "Bye!" },           // Group 0: Up / Hi
        { "Yes", "No", "Sorry!", "lol" },                     // Group 1: Right / Reply
        { "Follow me!", "Wait up!", "Let's go!", "Nice!" },   // Group 2: Down / Drive
        { "Race?", "Join my race!", "gg", "One more?" }       // Group 3: Left / Race
    };
    public static final String[] GROUP_NAMES = { "Hi (↑)", "Reply (→)", "Drive (↓)", "Race (←)" };

    private static volatile Context currentContext = null;

    // Protocol constants (from aemu_postoffice / packets_v1.h)
    private static final byte OPCODE_PING = 0;
    private static final byte OPCODE_LOGIN = 1;
    private static final byte OPCODE_CONNECT = 2;
    private static final byte OPCODE_DISCONNECT = 3;
    private static final byte OPCODE_CONNECT_BSSID = 6;
    private static final byte OPCODE_CHAT = 7;

    // Postoffice init types (from aemu_postoffice_packets.h)
    private static final int AEMU_POSTOFFICE_INIT_PDP = 0;
    private static final int DEFAULT_GAME_PORT = 47210;
    private static final int LOCAL_RELAY_PORT = 47211;
    private static final String PRODUCT_CODE = "NFSU2REVV"; // exactly 9 chars alphanumeric

    public static final class Peer {
        public final String name;
        public final byte[] mac;
        public final int ip;
        public final int virtualPort;
        public DatagramSocket socket;
        public Thread worker;
        public volatile long lastSeen;

        public Peer(String name, byte[] mac, int ip, int virtualPort) {
            this.name = name;
            this.mac = mac != null ? mac.clone() : new byte[6];
            this.ip = ip;
            this.virtualPort = virtualPort;
            this.lastSeen = System.currentTimeMillis();
        }

        public void close() {
            if (socket != null && !socket.isClosed()) {
                try { socket.close(); } catch (Exception ignored) {}
            }
            if (worker != null) {
                try { worker.interrupt(); } catch (Exception ignored) {}
            }
        }
    }

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean racingActive = new AtomicBoolean(false);
    private final AtomicBoolean adhocConnected = new AtomicBoolean(false);
    private final Map<String, Peer> peers = new ConcurrentHashMap<>();

    public boolean isConnected() {
        return running.get() && adhocConnected.get();
    }

    public boolean isRacing() {
        return racingActive.get();
    }

    public void setRacing(boolean active) {
        racingActive.set(active);
    }

    public static final int MODE_ADHOC = 1;
    public static final int MODE_WSS = 2;
    private volatile int activeMode = MODE_ADHOC;

    public String getActiveModeName() {
        return activeMode == MODE_WSS ? "WebSocket (WSS)" : "Prometheus Adhoc";
    }

    private Thread adhocctlThread;
    private Thread postofficeThread;
    private Thread wsThread;
    private Thread udpBridgeThread;
    private Thread roamTickThread;

    private Socket adhocctlSocket;
    private Socket postofficeSocket;
    private Socket wsSocket;
    private OutputStream wsOut;
    private volatile int myVirtualIp = 0;

    private DatagramSocket localUdpSocket;
    private BufferedOutputStream postofficeOut;

    private byte[] localMac = new byte[6];
    private String serverHost = GameOptions.DEFAULT_ROAM_SERVER;
    private String roomName = GameOptions.DEFAULT_ROAM_ROOM;
    private String playerName = "Racer";
    private volatile String connectionStatus = "disconnected";

    private AdhocRoamClient() {}

    public static AdhocRoamClient getInstance() {
        return INSTANCE;
    }

    public String getServerHost() {
        return serverHost;
    }

    public String getRoomName() {
        return roomName;
    }

    public String getConnectionStatus() {
        return connectionStatus;
    }

    private boolean ghostMode = true;

    private boolean isWsHost(String host) {
        if (host == null) return false;
        String h = host.toLowerCase().trim();
        // Default Prometheus Adhoc server (eahub.eu / adhoc.eahub.eu)
        if (h.equals("adhoc.eahub.eu") || h.equals("eahub.eu")) {
            return false;
        }
        if (h.startsWith("adhoc.") && !h.contains("nfsu2")) {
            return false;
        }
        // Custom servers connect directly via WebSocket virtual switch (/lan)
        return true;
    }

    public synchronized void start(Context context, String host, String room, String name) {
        start(context, host, room, name, GameOptions.isGhostModeEnabled(context));
    }

    public synchronized void start(Context context, String host, String room, String name, boolean ghostMode) {
        currentContext = context;
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

        running.set(true);
        peers.clear();
        connectionStatus = "connecting to " + serverHost + "...";

        Log.i(TAG, "Starting Adhoc Roam: Server=" + serverHost + " Room=" + roomName
                + " Name=" + playerName + " Ghost=" + ghostMode + " MAC=" + formatMac(localMac));

        startUdpBridgeThread();

        new Thread(() -> {
            if (!running.get()) return;
            boolean isWs = isWsHost(serverHost);
            activeMode = isWs ? MODE_WSS : MODE_ADHOC;
            Log.i(TAG, "Backend selected for " + serverHost + ": " + (isWs ? "WebSocket Virtual Switch (WSS)" : "Prometheus Adhoc"));
            if (isWs) {
                startWebSocketThread();
            } else {
                startAdhocctlThread();
                startPostofficeThread();
            }
            if (libraryLoaded) {
                try {
                    nativeRoamConfigure(roomName, playerName, "127.0.0.1:" + LOCAL_RELAY_PORT, ghostMode, isWs ? 0x0A4D00FD : 0);
                } catch (Throwable t) {
                    Log.d(TAG, "nativeRoamConfigure: " + t.getMessage());
                }
            }
        }, "BackendSelector").start();

        if (libraryLoaded) {
            try {
                nativeRoamConfigure(roomName, playerName, "127.0.0.1:" + LOCAL_RELAY_PORT, ghostMode, 0);
            } catch (Throwable t) {
                Log.d(TAG, "nativeRoamConfigure: " + t.getMessage());
            }
            startRoamTickThread();
        }
    }

    public synchronized void stop() {
        if (!running.getAndSet(false)) return;

        Log.i(TAG, "Stopping Adhoc Roam client");
        connectionStatus = "disconnected";

        closeQuietly(adhocctlSocket);
        closeQuietly(postofficeSocket);
        closeQuietly(wsSocket);
        synchronized (this) {
            wsOut = null;
        }
        if (localUdpSocket != null) {
            try { localUdpSocket.close(); } catch (Exception ignored) {}
        }

        interruptQuietly(adhocctlThread);
        interruptQuietly(postofficeThread);
        interruptQuietly(wsThread);
        interruptQuietly(udpBridgeThread);
        interruptQuietly(roamTickThread);

        for (Peer p : peers.values()) {
            p.close();
        }
        peers.clear();
        racingActive.set(false);
        adhocConnected.set(false);
    }

    public int getPeerCount() {
        return peers.size();
    }

    public Map<String, Peer> getPeers() {
        return peers;
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
        // Locally administered unicast MAC (bit 1 of byte 0 set, bit 0 clear)
        mac[0] = (byte) ((mac[0] & 0xFE) | 0x02);

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

    // ---- WebSocket Virtual Switch Client (WSS /lan) ---------------------------------------------------

    private void startWebSocketThread() {
        wsThread = new Thread(() -> {
            while (running.get()) {
                try {
                    String host = serverHost;
                    boolean useSsl = true;
                    int port = 443;
                    if (host.startsWith("wss://")) {
                        host = host.substring(6);
                        useSsl = true;
                    } else if (host.startsWith("ws://")) {
                        host = host.substring(5);
                        useSsl = false;
                        port = 80;
                    }
                    if (host.contains("/")) {
                        host = host.substring(0, host.indexOf("/"));
                    }
                    if (host.contains(":")) {
                        String[] parts = host.split(":");
                        host = parts[0];
                        try { port = Integer.parseInt(parts[1]); } catch (Exception ignored) {}
                    }

                    Log.i(TAG, "Connecting WebSocket switch " + (useSsl ? "wss://" : "ws://") + host + ":" + port + "/lan");
                    if (useSsl) {
                        SSLSocketFactory factory = (SSLSocketFactory) SSLSocketFactory.getDefault();
                        wsSocket = factory.createSocket(host, port);
                    } else {
                        wsSocket = new Socket(host, port);
                    }
                    wsSocket.setTcpNoDelay(true);
                    wsSocket.setSoTimeout(35000);

                    OutputStream rawOut = new BufferedOutputStream(wsSocket.getOutputStream());
                    InputStream rawIn = new BufferedInputStream(wsSocket.getInputStream());

                    synchronized (this) {
                        wsOut = rawOut;
                    }

                    // Handshake
                    String secKey = Base64.encodeToString(UUID.randomUUID().toString().substring(0, 16).getBytes(StandardCharsets.US_ASCII), Base64.NO_WRAP);
                    String handshake = "GET /lan HTTP/1.1\r\n" +
                            "Host: " + host + "\r\n" +
                            "Upgrade: websocket\r\n" +
                            "Connection: Upgrade\r\n" +
                            "Sec-WebSocket-Key: " + secKey + "\r\n" +
                            "Sec-WebSocket-Version: 13\r\n" +
                            "Origin: https://" + host + "\r\n\r\n";
                    rawOut.write(handshake.getBytes(StandardCharsets.US_ASCII));
                    rawOut.flush();

                    // Read HTTP response status
                    ByteArrayOutputStream headerBuf = new ByteArrayOutputStream();
                    int b;
                    while ((b = rawIn.read()) != -1) {
                        headerBuf.write(b);
                        byte[] arr = headerBuf.toByteArray();
                        if (arr.length >= 4 &&
                                arr[arr.length - 4] == '\r' && arr[arr.length - 3] == '\n' &&
                                arr[arr.length - 2] == '\r' && arr[arr.length - 1] == '\n') {
                            break;
                        }
                    }
                    String statusLine = headerBuf.toString(StandardCharsets.ISO_8859_1.name());
                    if (!statusLine.contains("101")) {
                        throw new IOException("WebSocket upgrade rejected: " + statusLine.split("\r\n")[0]);
                    }

                    Log.i(TAG, "WebSocket handshake 101 Switching Protocols OK");
                    Log.i(TAG, "connected to " + host);
                    connectionStatus = "connected to " + host;

                    long lastPingTime = System.currentTimeMillis();

                    // Frame receive loop
                    while (running.get()) {
                        int b0 = rawIn.read();
                        if (b0 == -1) break;
                        int b1 = rawIn.read();
                        if (b1 == -1) break;

                        int opcode = b0 & 0x0F;
                        boolean masked = (b1 & 0x80) != 0;
                        int payloadLen = b1 & 0x7F;

                        if (payloadLen == 126) {
                            payloadLen = ((rawIn.read() & 0xFF) << 8) | (rawIn.read() & 0xFF);
                        } else if (payloadLen == 127) {
                            long bigLen = 0;
                            for (int i = 0; i < 8; i++) {
                                bigLen = (bigLen << 8) | (rawIn.read() & 0xFF);
                            }
                            payloadLen = (int) bigLen;
                        }

                        byte[] maskKey = null;
                        if (masked) {
                            maskKey = new byte[4];
                            readBytes(rawIn, maskKey, 4);
                        }

                        byte[] payload = new byte[payloadLen];
                        readBytes(rawIn, payload, payloadLen);

                        if (masked && maskKey != null) {
                            for (int i = 0; i < payloadLen; i++) {
                                payload[i] ^= maskKey[i % 4];
                            }
                        }

                        if (opcode == 1) { // Text frame
                            handleWsTextMessage(new String(payload, StandardCharsets.UTF_8));
                        } else if (opcode == 2) { // Binary frame
                            handleWsBinaryMessage(payload);
                        } else if (opcode == 9) { // Ping
                            sendWsFrame(10, payload); // Pong
                        } else if (opcode == 8) { // Close
                            break;
                        }

                        if (System.currentTimeMillis() - lastPingTime > 15000) {
                            sendWsPing();
                            lastPingTime = System.currentTimeMillis();
                        }
                    }
                } catch (Exception e) {
                    if (running.get()) {
                        String errMsg = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
                        connectionStatus = "error: " + errMsg;
                        Log.w(TAG, "Connection error to " + host + ": " + errMsg + ", retrying in 4s...");
                        sleepQuietly(4000);
                    }
                } finally {
                    adhocConnected.set(false);
                    synchronized (this) {
                        wsOut = null;
                    }
                    closeQuietly(wsSocket);
                }
            }
        }, "WebSocketWorker");
        wsThread.setDaemon(true);
        wsThread.start();
    }

    private void handleWsTextMessage(String jsonStr) {
        try {
            JSONObject obj = new JSONObject(jsonStr);
            String type = obj.optString("t", "");
            if ("welcome".equals(type)) {
                myVirtualIp = obj.optInt("vip", 0);
                String vipStr = obj.optString("vipStr", "");
                Log.i(TAG, "WebSocket welcome: VIP=" + vipStr + " (" + myVirtualIp + ")");
                Log.i(TAG, "connected to " + serverHost);
                adhocConnected.set(true);
                connectionStatus = "connected to " + serverHost;

                // Send hello
                JSONObject hello = new JSONObject();
                hello.put("t", "hello");
                hello.put("name", playerName);
                sendWsFrame(1, hello.toString().getBytes(StandardCharsets.UTF_8));

                if (libraryLoaded) {
                    try {
                        nativeRoamConfigure(roomName, playerName, "127.0.0.1:" + LOCAL_RELAY_PORT, ghostMode, 0x0A4D00FD);
                    } catch (Throwable t) {
                        Log.d(TAG, "nativeRoamConfigure: " + t.getMessage());
                    }
                }
            } else if ("ping".equals(type)) {
                int id = obj.optInt("id", 0);
                JSONObject pong = new JSONObject();
                pong.put("t", "pong");
                pong.put("id", id);
                sendWsFrame(1, pong.toString().getBytes(StandardCharsets.UTF_8));
            } else if ("peers".equals(type)) {
                JSONArray arr = obj.optJSONArray("peers");
                if (arr != null) {
                    Log.i(TAG, "WebSocket active server peers: " + arr.length());
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "handleWsTextMessage: " + e.getMessage());
        }
    }

    private void handleWsBinaryMessage(byte[] data) {
        if (data == null || data.length < 4) return;
        int kind = data[0] & 0xFF;

        if (kind == 1) { // NET_KIND_UDP
            if (data.length >= 16) {
                int srcVip = ((data[4] & 0xFF) << 24) | ((data[5] & 0xFF) << 16) | ((data[6] & 0xFF) << 8) | (data[7] & 0xFF);
                int sport = ((data[12] & 0xFF) << 8) | (data[13] & 0xFF);
                if (sport == 0) sport = DEFAULT_GAME_PORT;
                if (srcVip != myVirtualIp) {
                    byte[] payload = Arrays.copyOfRange(data, 16, data.length);
                    if (srcVip != 0x0A4D00FD) {
                        trackWsPeer(srcVip);
                    }
                    if (libraryLoaded) {
                        nativeRoamReceive(srcVip, sport, payload);
                    }
                }
            }
        } else if (kind == 32) { // NET_KIND_BUNDLE
            int at = 4;
            while (at + 4 <= data.length) {
                int k = data[at] & 0xFF;
                int flen = ((data[at + 2] & 0xFF) << 8) | (data[at + 3] & 0xFF);
                int f = at + 4 + 4 * k;
                int end = f + flen;
                if (flen >= 16 && end <= data.length && (data[f] & 0xFF) == 1) {
                    int srcVip = ((data[f + 4] & 0xFF) << 24) | ((data[f + 5] & 0xFF) << 16) | ((data[f + 6] & 0xFF) << 8) | (data[f + 7] & 0xFF);
                    int sport = ((data[f + 12] & 0xFF) << 8) | (data[f + 13] & 0xFF);
                    if (sport == 0) sport = DEFAULT_GAME_PORT;
                    if (srcVip != myVirtualIp) {
                        byte[] payload = Arrays.copyOfRange(data, f + 16, end);
                        if (srcVip != 0x0A4D00FD) {
                            trackWsPeer(srcVip);
                        }
                        if (libraryLoaded) {
                            nativeRoamReceive(srcVip, sport, payload);
                        }
                    }
                }
                at = end;
            }
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

    private void handleNativeSend(int dstIp, int dstPort, byte[] data) {
        if (activeMode == MODE_WSS) {
            sendWebSocketDgram(dstIp, dstPort, data);
        } else {
            if (dstIp == -1 || dstIp == 0xFFFFFFFF) {
                broadcastToAdhocPeers(data);
            } else {
                for (Peer p : peers.values()) {
                    if (p.ip == dstIp) {
                        sendPdpPacket(p.mac, data);
                        return;
                    }
                }
                broadcastToAdhocPeers(data);
            }
        }
    }

    public synchronized void sendWebSocketDgram(int dstVip, int dport, byte[] payload) {
        if (!running.get() || wsOut == null || payload == null || payload.length == 0 || myVirtualIp == 0) return;
        try {
            ByteBuffer buf = ByteBuffer.allocate(16 + payload.length);
            buf.put((byte) 1); // NET_KIND_UDP
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.putInt(myVirtualIp);
            buf.putInt(dstVip);
            buf.putShort((short) DEFAULT_GAME_PORT);
            buf.putShort((short) (dport != 0 ? dport : DEFAULT_GAME_PORT));
            buf.put(payload);

            sendWsFrame(2, buf.array());
        } catch (Exception e) {
            Log.d(TAG, "sendWebSocketDgram failed: " + e.getMessage());
        }
    }

    private synchronized void sendWsFrame(int opcode, byte[] payload) throws IOException {
        if (wsOut == null || payload == null) return;
        byte[] mask = new byte[4];
        new SecureRandom().nextBytes(mask);
        int len = payload.length;

        ByteArrayOutputStream baos = new ByteArrayOutputStream(len + 14);
        baos.write(0x80 | (opcode & 0x0F));
        if (len < 126) {
            baos.write(0x80 | len);
        } else if (len <= 65535) {
            baos.write(0x80 | 126);
            baos.write((len >> 8) & 0xFF);
            baos.write(len & 0xFF);
        } else {
            baos.write(0x80 | 127);
            for (int i = 7; i >= 0; i--) {
                baos.write((int) ((len >> (i * 8)) & 0xFF));
            }
        }
        baos.write(mask);
        for (int i = 0; i < len; i++) {
            baos.write(payload[i] ^ mask[i % 4]);
        }
        wsOut.write(baos.toByteArray());
        wsOut.flush();
    }

    private void sendWsPing() {
        try {
            JSONObject ping = new JSONObject();
            ping.put("t", "ping");
            ping.put("id", (int) (System.currentTimeMillis() % 100000));
            sendWsFrame(1, ping.toString().getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {}
    }

    // ---- Adhocctl Thread (TCP 27312) -------------------------------------------------------------------

    private void startAdhocctlThread() {
        adhocctlThread = new Thread(() -> {
            while (running.get()) {
                try {
                    Log.i(TAG, "Connecting to adhocctl " + serverHost + ":" + GameOptions.ADHOCCTL_PORT);
                    adhocctlSocket = new Socket(serverHost, GameOptions.ADHOCCTL_PORT);
                    adhocctlSocket.setSoTimeout(45000); // 45s read timeout
                    adhocctlSocket.setTcpNoDelay(true);

                    OutputStream out = new BufferedOutputStream(adhocctlSocket.getOutputStream());
                    InputStream in = new BufferedInputStream(adhocctlSocket.getInputStream());

                    // Send OPCODE_LOGIN (1)
                    // Struct SceNetAdhocctlLoginPacketC2S: opcode(1) + mac(6) + name(128) + game(9) = 144 bytes
                    ByteBuffer loginBuf = ByteBuffer.allocate(144).order(ByteOrder.LITTLE_ENDIAN);
                    loginBuf.put(OPCODE_LOGIN);
                    loginBuf.put(localMac);
                    byte[] nameBytes = playerName.getBytes(StandardCharsets.UTF_8);
                    byte[] nameField = new byte[128];
                    System.arraycopy(nameBytes, 0, nameField, 0, Math.min(nameBytes.length, 127));
                    loginBuf.put(nameField);
                    byte[] gameField = PRODUCT_CODE.getBytes(StandardCharsets.US_ASCII);
                    loginBuf.put(gameField, 0, 9);

                    out.write(loginBuf.array());
                    out.flush();

                    // Send OPCODE_CONNECT (2)
                    // Struct SceNetAdhocctlConnectPacketC2S: opcode(1) + group(8) = 9 bytes
                    ByteBuffer connBuf = ByteBuffer.allocate(9).order(ByteOrder.LITTLE_ENDIAN);
                    connBuf.put(OPCODE_CONNECT);
                    byte[] groupField = new byte[8];
                    String adhocGroup = (roomName != null && !roomName.isEmpty()) ? roomName : "FREEROAM";
                    byte[] groupBytes = adhocGroup.getBytes(StandardCharsets.US_ASCII);
                    System.arraycopy(groupBytes, 0, groupField, 0, Math.min(groupBytes.length, 8));
                    connBuf.put(groupField);

                    out.write(connBuf.array());
                    out.flush();

                    Log.i(TAG, "Adhocctl login and join room '" + adhocGroup + "' sent successfully");

                    long lastPing = System.currentTimeMillis();

                    // Receiver loop
                    while (running.get()) {
                        if (System.currentTimeMillis() - lastPing >= 15000) {
                            // Send OPCODE_PING (0)
                            out.write(OPCODE_PING);
                            out.flush();
                            lastPing = System.currentTimeMillis();
                        }

                        int opcode = in.read();
                        if (opcode == -1) break; // EOF

                        handleAdhocctlPacket((byte) opcode, in);
                    }
                } catch (Exception e) {
                    if (running.get()) {
                        String errMsg = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
                        connectionStatus = "error: " + errMsg;
                        Log.w(TAG, "Adhocctl connection error to " + serverHost + ": " + errMsg + ", retrying in 5s...");
                        sleepQuietly(5000);
                    }
                } finally {
                    adhocConnected.set(false);
                    closeQuietly(adhocctlSocket);
                }
            }
        }, "AdhocctlWorker");
        adhocctlThread.setDaemon(true);
        adhocctlThread.start();
    }

    private void handleAdhocctlPacket(byte opcode, InputStream in) throws Exception {
        switch (opcode) {
            case OPCODE_CONNECT_BSSID: {
                // SceNetAdhocctlConnectBSSIDPacketS2C: base(1 already read) + mac(6) = 6 remaining bytes
                byte[] bssid = readFully(in, 6);
                adhocConnected.set(true);
                Log.i(TAG, "Joined room group BSSID: " + formatMac(bssid));
                Log.i(TAG, "connected to " + serverHost);
                connectionStatus = "connected to " + serverHost;
                break;
            }
            case OPCODE_CONNECT: {
                // SceNetAdhocctlConnectPacketS2C: base(1) + name(128) + mac(6) + ip(4) = 138 remaining bytes
                byte[] nameBuf = readFully(in, 128);
                byte[] peerMac = readFully(in, 6);
                byte[] ipBuf = readFully(in, 4);

                int nullIdx = 0;
                while (nullIdx < nameBuf.length && nameBuf[nullIdx] != 0) nullIdx++;
                String peerName = new String(nameBuf, 0, nullIdx, StandardCharsets.UTF_8);
                int peerIp = ByteBuffer.wrap(ipBuf).order(ByteOrder.LITTLE_ENDIAN).getInt();

                if (!Arrays.equals(peerMac, localMac)) {
                    getOrCreatePeer(peerName, peerMac, peerIp);
                }
                break;
            }
            case OPCODE_DISCONNECT: {
                // SceNetAdhocctlDisconnectPacketS2C: base(1) + ip(4) = 4 remaining bytes
                byte[] ipBuf = readFully(in, 4);
                int peerIp = ByteBuffer.wrap(ipBuf).order(ByteOrder.LITTLE_ENDIAN).getInt();

                for (Map.Entry<String, Peer> entry : peers.entrySet()) {
                    if (entry.getValue().ip == peerIp) {
                        Peer removed = peers.remove(entry.getKey());
                        if (removed != null) {
                            removed.close();
                            Log.i(TAG, "Adhoc Peer Left: " + removed.name + " [" + entry.getKey() + "]");
                        }
                        break;
                    }
                }
                break;
            }
            case OPCODE_CHAT: {
                // SceNetAdhocctlChatPacketS2C: msg(64) + name(128) = 192 bytes
                byte[] msgBuf = readFully(in, 64);
                byte[] nameBuf = readFully(in, 128);
                int n1 = 0, n2 = 0;
                while (n1 < msgBuf.length && msgBuf[n1] != 0) n1++;
                while (n2 < nameBuf.length && nameBuf[n2] != 0) n2++;
                String msg = new String(msgBuf, 0, n1, StandardCharsets.UTF_8);
                String sender = new String(nameBuf, 0, n2, StandardCharsets.UTF_8);
                Log.i(TAG, "Adhoc Chat from " + sender + ": " + msg);
                break;
            }
            default:
                Log.d(TAG, "Unknown adhocctl opcode: " + opcode);
                break;
        }
    }

    // ---- Postoffice Thread (TCP 27313) -----------------------------------------------------------------

    private void startPostofficeThread() {
        postofficeThread = new Thread(() -> {
            while (running.get()) {
                try {
                    Log.i(TAG, "Connecting to postoffice " + serverHost + ":" + GameOptions.POSTOFFICE_PORT);
                    postofficeSocket = new Socket(serverHost, GameOptions.POSTOFFICE_PORT);
                    postofficeSocket.setTcpNoDelay(true);

                    synchronized (this) {
                        postofficeOut = new BufferedOutputStream(postofficeSocket.getOutputStream());
                    }
                    InputStream in = new BufferedInputStream(postofficeSocket.getInputStream());

                    // Send aemu_postoffice_init:
                    // int32 init_type = 0 (PDP)
                    // char src_addr[8] (6 bytes MAC + 2 pad)
                    // uint16 sport = 47210
                    // char dst_addr[8] (0)
                    // uint16 dport = 0
                    // Total = 4 + 8 + 2 + 8 + 2 = 24 bytes
                    ByteBuffer initBuf = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN);
                    initBuf.putInt(AEMU_POSTOFFICE_INIT_PDP);
                    initBuf.put(localMac);
                    initBuf.putShort((short) 0); // padding
                    initBuf.putShort((short) DEFAULT_GAME_PORT);
                    initBuf.put(new byte[8]); // dst_addr
                    initBuf.putShort((short) 0); // dport

                    synchronized (this) {
                        postofficeOut.write(initBuf.array());
                        postofficeOut.flush();
                    }

                    Log.i(TAG, "Postoffice PDP registered on port " + DEFAULT_GAME_PORT);

                    // Postoffice datagram receive loop
                    byte[] header = new byte[14];
                    while (running.get()) {
                        // struct aemu_postoffice_pdp: char addr[8] + uint16 port + uint32 size = 14 bytes
                        readBytes(in, header, 14);

                        ByteBuffer hb = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
                        byte[] srcMac = new byte[6];
                        hb.get(srcMac);
                        hb.getShort(); // pad
                        int srcPort = hb.getShort() & 0xFFFF;
                        int size = hb.getInt();

                        if (size <= 0 || size > 65535) {
                            Log.w(TAG, "Invalid PDP size: " + size);
                            break;
                        }

                        byte[] payload = readFully(in, size);

                        // Drop echoed packets from ourselves
                        if (Arrays.equals(srcMac, localMac)) {
                            continue;
                        }

                        // Forward payload locally to the game via native bridge
                        Peer p = getOrCreatePeer("Player", srcMac, 0);
                        if (libraryLoaded) {
                            int srcIp = p.ip != 0 ? p.ip : (((srcMac[2] & 0xFF) << 24) | ((srcMac[3] & 0xFF) << 16) | ((srcMac[4] & 0xFF) << 8) | (srcMac[5] & 0xFF));
                            nativeRoamReceive(srcIp, DEFAULT_GAME_PORT, payload);
                        } else {
                            forwardToLocalGame(srcMac, payload);
                        }
                    }
                } catch (Exception e) {
                    if (running.get()) {
                        Log.w(TAG, "Postoffice connection error: " + e.getMessage() + ", reconnecting in 5s...");
                        sleepQuietly(5000);
                    }
                } finally {
                    synchronized (this) {
                        postofficeOut = null;
                        closeQuietly(postofficeSocket);
                    }
                }
            }
        }, "PostofficeWorker");
        postofficeThread.setDaemon(true);
        postofficeThread.start();
    }

    private final java.util.concurrent.atomic.AtomicInteger nextVirtualPort = new java.util.concurrent.atomic.AtomicInteger(47212);

    private synchronized Peer getOrCreatePeer(String name, byte[] mac, int ip) {
        String key = formatMac(mac);
        Peer existing = peers.get(key);
        if (existing != null) {
            existing.lastSeen = System.currentTimeMillis();
            return existing;
        }

        int vPort = nextVirtualPort.getAndIncrement();
        if (vPort > 47290) {
            nextVirtualPort.set(47212);
            vPort = 47212;
        }

        final Peer peer = new Peer(name, mac, ip, vPort);
        try {
            peer.socket = new DatagramSocket(vPort, InetAddress.getByName("127.0.0.1"));
            peer.socket.setBroadcast(false);

            peer.worker = new Thread(() -> {
                byte[] buf = new byte[2048];
                while (running.get() && peer.socket != null && !peer.socket.isClosed()) {
                    try {
                        DatagramPacket pkt = new DatagramPacket(buf, buf.length);
                        peer.socket.receive(pkt);
                        if (pkt.getLength() > 0) {
                            byte[] data = Arrays.copyOf(pkt.getData(), pkt.getLength());
                            if (activeMode == MODE_WSS) {
                                sendWebSocketDgram(peer.ip, DEFAULT_GAME_PORT, data);
                            } else {
                                sendPdpPacket(peer.mac, data);
                            }
                        }
                    } catch (Exception e) {
                        break;
                    }
                }
            }, "PeerWorker-" + vPort);
            peer.worker.setDaemon(true);
            peer.worker.start();
        } catch (Exception e) {
            Log.e(TAG, "Failed creating peer socket on port " + vPort + ": " + e.getMessage());
        }

        peers.put(key, peer);
        Log.i(TAG, "Registered peer " + name + " [" + key + "] on virtual port " + vPort + ", total peers=" + peers.size());
        return peer;
    }

    public synchronized void sendPdpPacket(byte[] dstMac, byte[] data) {
        if (!running.get() || postofficeOut == null || data == null || data.length == 0 || dstMac == null) return;
        try {
            ByteBuffer pdpHeader = ByteBuffer.allocate(14).order(ByteOrder.LITTLE_ENDIAN);
            pdpHeader.put(dstMac);
            if (dstMac.length == 6) {
                pdpHeader.putShort((short) 0); // padding
            }
            pdpHeader.putShort((short) DEFAULT_GAME_PORT);
            pdpHeader.putInt(data.length);

            postofficeOut.write(pdpHeader.array());
            postofficeOut.write(data);
            postofficeOut.flush();
        } catch (Exception e) {
            Log.d(TAG, "Failed sending PDP packet: " + e.getMessage());
        }
    }

    private void forwardToLocalGame(byte[] srcMac, byte[] payload) {
        try {
            Peer peer = getOrCreatePeer("Player", srcMac, 0);
            if (peer != null && peer.socket != null && !peer.socket.isClosed()) {
                InetAddress loopback = InetAddress.getByName("127.0.0.1");
                DatagramPacket packet = new DatagramPacket(payload, payload.length, loopback, DEFAULT_GAME_PORT);
                peer.socket.send(packet);
            }
        } catch (Exception e) {
            Log.d(TAG, "Failed forwarding PDP payload to local game: " + e.getMessage());
        }
    }

    // ---- Local UDP Relay (Bridges local game packets -> Postoffice) ------------------------------------

    private void startUdpBridgeThread() {
        udpBridgeThread = new Thread(() -> {
            try {
                localUdpSocket = new DatagramSocket(LOCAL_RELAY_PORT);
                localUdpSocket.setBroadcast(true);
                byte[] buffer = new byte[2048];

                while (running.get()) {
                    DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                    localUdpSocket.receive(packet);

                    if (packet.getLength() > 0) {
                        byte[] data = Arrays.copyOf(packet.getData(), packet.getLength());
                        broadcastToAdhocPeers(data);
                    }
                }
            } catch (Exception e) {
                if (running.get()) {
                    Log.d(TAG, "Local UDP relay terminated: " + e.getMessage());
                }
            } finally {
                if (localUdpSocket != null && !localUdpSocket.isClosed()) {
                    localUdpSocket.close();
                }
            }
        }, "AdhocUdpBridge");
        udpBridgeThread.setDaemon(true);
        udpBridgeThread.start();
    }

    private void startRoamTickThread() {
        if (!libraryLoaded) return;
        roamTickThread = new Thread(() -> {
            while (running.get()) {
                try {
                    nativeRoamTick();
                    Thread.sleep(16); // ~60 Hz simulation step
                } catch (InterruptedException e) {
                    break;
                } catch (UnsatisfiedLinkError e) {
                    Log.e(TAG, "nativeRoamTick unavailable, stopping tick worker");
                    break;
                } catch (Throwable t) {
                    try { Thread.sleep(50); } catch (Exception ignored) { break; }
                }
            }
        }, "RoamTickWorker");
        roamTickThread.setDaemon(true);
        roamTickThread.start();
    }

    public synchronized void broadcastToAdhocPeers(byte[] data) {
        if (!running.get() || data == null || data.length == 0) return;

        if (activeMode == MODE_WSS) {
            // Send to Roam Hub (10.77.0.253) and broadcast to players
            sendWebSocketDgram(0x0A4D00FD, DEFAULT_GAME_PORT, data);
            sendWebSocketDgram(0xFFFFFFFF, DEFAULT_GAME_PORT, data);
            return;
        }

        if (postofficeOut == null) return;

        try {
            // 1. Send to postoffice PDP as broadcast destination (FF:FF:FF:FF:FF:FF)
            byte[] broadcastMac = new byte[] { (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF };
            sendPdpPacket(broadcastMac, data);

            // 2. ALSO send directly to each known active peer in the room
            for (Peer p : peers.values()) {
                if (p.mac != null && p.mac.length == 6 && !Arrays.equals(p.mac, localMac)) {
                    sendPdpPacket(p.mac, data);
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "Failed to broadcast to adhoc peers: " + e.getMessage());
        }
    }

    // ---- Helper Utilities ------------------------------------------------------------------------------

    private static byte[] readFully(InputStream in, int length) throws Exception {
        byte[] buf = new byte[length];
        readBytes(in, buf, length);
        return buf;
    }

    private static void readBytes(InputStream in, byte[] buf, int length) throws Exception {
        int read = 0;
        while (read < length) {
            int chunk = in.read(buf, read, length - read);
            if (chunk < 0) throw new java.io.EOFException("Unexpected end of stream");
            read += chunk;
        }
    }

    private static void sleepQuietly(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException ignored) {}
    }

    private static void closeQuietly(Socket s) {
        if (s != null) {
            try { s.close(); } catch (Exception ignored) {}
        }
    }

    private static void interruptQuietly(Thread t) {
        if (t != null) {
            try { t.interrupt(); } catch (Exception ignored) {}
        }
    }

    // ---- Quick Chat UI & Input --------------------------------------------------------------------------

    public static void handleRightStick(float x, float y) {
        try {
            nativeRoamStick(x, y);
        } catch (Throwable ignored) {}
    }

    public static void showQuickChatDialog(Context context) {
        if (context == null) context = currentContext;
        if (context == null) return;
        final Context ctx = context;
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                String[] categories = {
                    "1. Hi (↑)",
                    "2. Reply (→)",
                    "3. Drive (↓)",
                    "4. Race (←)"
                };
                new AlertDialog.Builder(ctx)
                    .setTitle("Quick Chat")
                    .setItems(categories, (dialog, which) -> showGroupPhrasesDialog(ctx, which))
                    .setNegativeButton("Cancel", null)
                    .show();
            } catch (Exception e) {
                Log.d(TAG, "QuickChat dialog: " + e.getMessage());
            }
        });
    }

    private static void showGroupPhrasesDialog(Context context, int groupIndex) {
        if (groupIndex < 0 || groupIndex >= QUICK_CHAT_GROUPS.length) return;
        String[] phrases = QUICK_CHAT_GROUPS[groupIndex];
        new AlertDialog.Builder(context)
            .setTitle(GROUP_NAMES[groupIndex])
            .setItems(phrases, (dialog, which) -> {
                int phraseIndex = groupIndex * 4 + which;
                sendQuickChat(phraseIndex);
            })
            .setNegativeButton("Back", (dialog, which) -> showQuickChatDialog(context))
            .show();
    }

    public static void sendQuickChat(int phraseIndex) {
        try {
            nativeRoamSay(phraseIndex);
            Log.i(TAG, "Sent Quick Chat phrase: " + phraseIndex);
        } catch (Throwable t) {
            Log.w(TAG, "Failed sending quick chat: " + t.getMessage());
        }
    }

    // Called from native C layer (roam_android.c)
    public static void onQuickChatReceived(String json) {
        if (json == null || json.isEmpty()) return;
        try {
            org.json.JSONObject obj = new org.json.JSONObject(json);
            if (obj.has("group")) {
                int group = obj.getInt("group");
                if (group >= 0 && group < GROUP_NAMES.length) {
                    StringBuilder sb = new StringBuilder();
                    sb.append("💬 Chat ").append(GROUP_NAMES[group]).append(":\n");
                    String[] phrases = QUICK_CHAT_GROUPS[group];
                    String[] arrows = { "↑", "→", "↓", "←" };
                    for (int i = 0; i < 4; i++) {
                        sb.append(arrows[i]).append(" ").append(phrases[i]).append(i < 3 ? "  |  " : "");
                    }
                    showToast(sb.toString());
                }
            } else if (obj.has("p")) {
                int p = obj.getInt("p");
                String name = obj.optString("name", "Racer");
                boolean isMe = obj.optInt("me", 0) == 1;
                int group = p / 4;
                int idx = p % 4;
                if (group >= 0 && group < QUICK_CHAT_GROUPS.length && idx >= 0 && idx < 4) {
                    String phraseText = QUICK_CHAT_GROUPS[group][idx];
                    String display = (isMe ? "You" : name) + ": \"" + phraseText + "\"";
                    showToast("💬 " + display);
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "QuickChat json parse: " + e.getMessage());
        }
    }

    private static void showToast(String text) {
        Log.i(TAG, text);
        Context ctx = currentContext;
        if (ctx != null) {
            new Handler(Looper.getMainLooper()).post(() -> {
                try {
                    Toast.makeText(ctx, text, Toast.LENGTH_SHORT).show();
                } catch (Exception ignored) {}
            });
        }
    }
}
