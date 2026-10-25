package io.mininxd.nfsu2;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Adhoc Free Roam Client for NFSU2 Revived.
 * Connects to Prometheus Adhoc infrastructure (adhoc.eahub.eu:27312 adhocctl, 27313 postoffice PDP)
 * and bridges multiplayer Free Roam datagrams (N2FR) across internet clients without port forwarding.
 */
public final class AdhocRoamClient {
    private static final String TAG = "AdhocRoam";
    private static final AdhocRoamClient INSTANCE = new AdhocRoamClient();

    static {
        try {
            System.loadLibrary("nfsu2_roam");
            Log.i(TAG, "Native free roam companion (libnfsu2_roam) loaded successfully");
        } catch (Throwable t) {
            Log.d(TAG, "libnfsu2_roam not loaded: " + t.getMessage());
        }
    }

    public static native void nativeRoamConfigure(String room, String name, String peers, boolean ghostMode);
    public static native void nativeRoamTick();
    public static native void nativeRoamSay(int phrase);
    public static native void nativeRoamStick(float x, float y);

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

    private Thread adhocctlThread;
    private Thread postofficeThread;
    private Thread udpBridgeThread;
    private Thread roamTickThread;

    private Socket adhocctlSocket;
    private Socket postofficeSocket;
    private DatagramSocket localUdpSocket;
    private BufferedOutputStream postofficeOut;

    private byte[] localMac = new byte[6];
    private String serverHost = GameOptions.DEFAULT_ROAM_SERVER;
    private String roomName = GameOptions.DEFAULT_ROAM_ROOM;
    private String playerName = "Racer";

    private AdhocRoamClient() {}

    public static AdhocRoamClient getInstance() {
        return INSTANCE;
    }

    private boolean ghostMode = true;

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

        this.serverHost = (host != null && !host.trim().isEmpty()) ? host.trim() : GameOptions.DEFAULT_ROAM_SERVER;
        this.roomName = (room != null && !room.trim().isEmpty()) ? room.trim() : GameOptions.DEFAULT_ROAM_ROOM;
        if (name == null || name.trim().isEmpty() || "Racer".equalsIgnoreCase(name)) {
            name = GameOptions.detectSavegamePlayerName();
        }
        this.playerName = (name != null && !name.trim().isEmpty()) ? name.trim() : "Racer";
        this.localMac = getOrCreateVirtualMac(context);

        running.set(true);
        peers.clear();

        Log.i(TAG, "Starting Adhoc Roam: Server=" + serverHost + " Room=" + roomName
                + " Name=" + playerName + " Ghost=" + ghostMode + " MAC=" + formatMac(localMac));

        startAdhocctlThread();
        startPostofficeThread();
        startUdpBridgeThread();

        try {
            nativeRoamConfigure(roomName, playerName, "127.0.0.1:" + LOCAL_RELAY_PORT, ghostMode);
        } catch (Throwable t) {
            Log.d(TAG, "nativeRoamConfigure: " + t.getMessage());
        }
        startRoamTickThread();
    }

    public synchronized void stop() {
        if (!running.getAndSet(false)) return;

        Log.i(TAG, "Stopping Adhoc Roam client");

        closeQuietly(adhocctlSocket);
        closeQuietly(postofficeSocket);
        if (localUdpSocket != null) {
            try { localUdpSocket.close(); } catch (Exception ignored) {}
        }

        interruptQuietly(adhocctlThread);
        interruptQuietly(postofficeThread);
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
                    byte[] groupBytes = roomName.getBytes(StandardCharsets.US_ASCII);
                    System.arraycopy(groupBytes, 0, groupField, 0, Math.min(groupBytes.length, 8));
                    connBuf.put(groupField);

                    out.write(connBuf.array());
                    out.flush();

                    Log.i(TAG, "Adhocctl login and join room '" + roomName + "' sent successfully");

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
                        Log.w(TAG, "Adhocctl connection error: " + e.getMessage() + ", reconnecting in 5s...");
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

                        // Forward payload locally to the game via peer's virtual socket
                        forwardToLocalGame(srcMac, payload);
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
                            sendPdpPacket(peer.mac, data);
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
        roamTickThread = new Thread(() -> {
            while (running.get()) {
                try {
                    nativeRoamTick();
                    Thread.sleep(16); // ~60 Hz simulation step
                } catch (InterruptedException e) {
                    break;
                } catch (Throwable t) {
                    try { Thread.sleep(100); } catch (Exception ignored) {}
                }
            }
        }, "RoamTickWorker");
        roamTickThread.setDaemon(true);
        roamTickThread.start();
    }

    public synchronized void broadcastToAdhocPeers(byte[] data) {
        if (!running.get() || postofficeOut == null || data == null || data.length == 0) return;

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
