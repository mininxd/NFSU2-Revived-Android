package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

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
        public final long lastSeen;

        public Peer(String name, byte[] mac, int ip) {
            this.name = name;
            this.mac = mac != null ? mac.clone() : new byte[6];
            this.ip = ip;
            this.lastSeen = System.currentTimeMillis();
        }
    }

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final Map<String, Peer> peers = new ConcurrentHashMap<>();

    private Thread adhocctlThread;
    private Thread postofficeThread;
    private Thread udpBridgeThread;

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

    public synchronized void start(Context context, String host, String room, String name) {
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
                + " Name=" + playerName + " MAC=" + formatMac(localMac));

        startAdhocctlThread();
        startPostofficeThread();
        startUdpBridgeThread();
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

        peers.clear();
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

                Peer peer = new Peer(peerName, peerMac, peerIp);
                String macKey = formatMac(peerMac);
                peers.put(macKey, peer);

                Log.i(TAG, "Adhoc Peer Joined: " + peerName + " [" + macKey + "] total=" + peers.size());
                break;
            }
            case OPCODE_DISCONNECT: {
                // SceNetAdhocctlDisconnectPacketS2C: base(1) + ip(4) = 4 remaining bytes
                byte[] ipBuf = readFully(in, 4);
                int peerIp = ByteBuffer.wrap(ipBuf).order(ByteOrder.LITTLE_ENDIAN).getInt();

                for (Map.Entry<String, Peer> entry : peers.entrySet()) {
                    if (entry.getValue().ip == peerIp) {
                        peers.remove(entry.getKey());
                        Log.i(TAG, "Adhoc Peer Left: " + entry.getValue().name + " [" + entry.getKey() + "]");
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

                        // Forward payload locally to the game via UDP (127.0.0.1:47210)
                        forwardToLocalGame(payload);
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

    private void forwardToLocalGame(byte[] payload) {
        try {
            if (localUdpSocket != null && !localUdpSocket.isClosed()) {
                InetAddress loopback = InetAddress.getByName("127.0.0.1");
                DatagramPacket packet = new DatagramPacket(payload, payload.length, loopback, DEFAULT_GAME_PORT);
                localUdpSocket.send(packet);
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

    public synchronized void broadcastToAdhocPeers(byte[] data) {
        if (!running.get() || postofficeOut == null || data == null || data.length == 0) return;

        try {
            // Send to postoffice PDP as broadcast destination (FF:FF:FF:FF:FF:FF)
            // or iterate active peers
            byte[] broadcastMac = new byte[] { (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF };

            ByteBuffer pdpHeader = ByteBuffer.allocate(14).order(ByteOrder.LITTLE_ENDIAN);
            pdpHeader.put(broadcastMac);
            pdpHeader.putShort((short) 0); // padding
            pdpHeader.putShort((short) DEFAULT_GAME_PORT);
            pdpHeader.putInt(data.length);

            postofficeOut.write(pdpHeader.array());
            postofficeOut.write(data);
            postofficeOut.flush();
        } catch (Exception e) {
            Log.d(TAG, "Failed to send packet to postoffice: " + e.getMessage());
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
}
