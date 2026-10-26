package io.mininxd.nfsu2.net;

import android.content.Context;
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
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import io.mininxd.nfsu2.GameOptions;

/**
 * Handles communication with Prometheus Adhoc infrastructure (adhocctl:27312 and postoffice:27313).
 */
public final class AdhocPostofficeClient {
    private static final String TAG = "AdhocPostoffice";

    private static final byte OPCODE_PING = 0;
    private static final byte OPCODE_LOGIN = 1;
    private static final byte OPCODE_CONNECT = 2;
    private static final byte OPCODE_DISCONNECT = 3;
    private static final byte OPCODE_CONNECT_BSSID = 6;
    private static final byte OPCODE_CHAT = 7;

    private static final int AEMU_POSTOFFICE_INIT_PDP = 0;
    private static final int DEFAULT_GAME_PORT = 47210;
    private static final int LOCAL_RELAY_PORT = 47211;
    private static final String PRODUCT_CODE = "NFSU2REVV";

    public interface PostofficeListener {
        void onConnected(String bssid);
        void onDisconnected(String reason);
        void onPeerJoined(Peer peer);
        void onPeerLeft(int peerIp);
        void onDatagramReceived(byte[] srcMac, byte[] payload);
    }

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean connected = new AtomicBoolean(false);

    private Thread adhocctlThread;
    private Thread postofficeThread;
    private Thread udpBridgeThread;

    private Socket adhocctlSocket;
    private Socket postofficeSocket;
    private DatagramSocket localUdpSocket;
    private BufferedOutputStream postofficeOut;

    private String serverHost;
    private String roomName;
    private String playerName;
    private byte[] localMac;

    private final Map<String, Peer> peers = new ConcurrentHashMap<>();
    private final AtomicInteger nextVirtualPort = new AtomicInteger(47212);
    private PostofficeListener listener;

    public AdhocPostofficeClient() {}

    public void setListener(PostofficeListener listener) {
        this.listener = listener;
    }

    public boolean isConnected() {
        return connected.get();
    }

    public Map<String, Peer> getPeers() {
        return peers;
    }

    public synchronized void start(String serverHost, String roomName, String playerName, byte[] localMac) {
        this.serverHost = serverHost;
        this.roomName = roomName;
        this.playerName = playerName;
        this.localMac = localMac != null ? localMac.clone() : new byte[6];

        if (running.getAndSet(true)) return;

        startUdpBridgeThread();
        startAdhocctlThread();
        startPostofficeThread();
    }

    public synchronized void stop() {
        if (!running.getAndSet(false)) return;
        connected.set(false);

        closeQuietly(adhocctlSocket);
        closeQuietly(postofficeSocket);
        synchronized (this) {
            postofficeOut = null;
        }
        if (localUdpSocket != null) {
            try { localUdpSocket.close(); } catch (Exception ignored) {}
        }

        interruptQuietly(adhocctlThread);
        interruptQuietly(postofficeThread);
        interruptQuietly(udpBridgeThread);

        for (Peer p : peers.values()) {
            p.close();
        }
        peers.clear();
    }

    private void startAdhocctlThread() {
        adhocctlThread = new Thread(() -> {
            while (running.get()) {
                try {
                    Log.i(TAG, "Connecting to adhocctl " + serverHost + ":" + GameOptions.ADHOCCTL_PORT);
                    adhocctlSocket = new Socket(serverHost, GameOptions.ADHOCCTL_PORT);
                    adhocctlSocket.setSoTimeout(45000);
                    adhocctlSocket.setTcpNoDelay(true);

                    OutputStream out = new BufferedOutputStream(adhocctlSocket.getOutputStream());
                    InputStream in = new BufferedInputStream(adhocctlSocket.getInputStream());

                    // Send OPCODE_LOGIN (1)
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

                    while (running.get()) {
                        if (System.currentTimeMillis() - lastPing >= 15000) {
                            out.write(OPCODE_PING);
                            out.flush();
                            lastPing = System.currentTimeMillis();
                        }

                        int opcode = in.read();
                        if (opcode == -1) break;

                        handleAdhocctlPacket((byte) opcode, in);
                    }
                } catch (Exception e) {
                    if (running.get()) {
                        String errMsg = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
                        Log.w(TAG, "Adhocctl connection error to " + serverHost + ": " + errMsg + ", retrying in 5s...");
                        if (listener != null) listener.onDisconnected(errMsg);
                        sleepQuietly(5000);
                    }
                } finally {
                    connected.set(false);
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
                byte[] bssid = readFully(in, 6);
                connected.set(true);
                String bssidStr = formatMac(bssid);
                Log.i(TAG, "Joined room group BSSID: " + bssidStr);
                if (listener != null) listener.onConnected(bssidStr);
                break;
            }
            case OPCODE_CONNECT: {
                byte[] nameBuf = readFully(in, 128);
                byte[] peerMac = readFully(in, 6);
                byte[] ipBuf = readFully(in, 4);

                int nullIdx = 0;
                while (nullIdx < nameBuf.length && nameBuf[nullIdx] != 0) nullIdx++;
                String peerName = new String(nameBuf, 0, nullIdx, StandardCharsets.UTF_8);
                int peerIp = ByteBuffer.wrap(ipBuf).order(ByteOrder.LITTLE_ENDIAN).getInt();

                if (!Arrays.equals(peerMac, localMac)) {
                    Peer p = getOrCreatePeer(peerName, peerMac, peerIp);
                    if (listener != null) listener.onPeerJoined(p);
                }
                break;
            }
            case OPCODE_DISCONNECT: {
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
                if (listener != null) listener.onPeerLeft(peerIp);
                break;
            }
            case OPCODE_CHAT: {
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
                break;
        }
    }

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

                    ByteBuffer initBuf = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN);
                    initBuf.putInt(AEMU_POSTOFFICE_INIT_PDP);
                    initBuf.put(localMac);
                    initBuf.putShort((short) 0);
                    initBuf.putShort((short) DEFAULT_GAME_PORT);
                    initBuf.put(new byte[8]);
                    initBuf.putShort((short) 0);

                    synchronized (this) {
                        postofficeOut.write(initBuf.array());
                        postofficeOut.flush();
                    }

                    Log.i(TAG, "Postoffice PDP registered on port " + DEFAULT_GAME_PORT);

                    byte[] header = new byte[14];
                    while (running.get()) {
                        readBytes(in, header, 14);

                        ByteBuffer hb = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
                        byte[] srcMac = new byte[6];
                        hb.get(srcMac);
                        hb.getShort();
                        int srcPort = hb.getShort() & 0xFFFF;
                        int size = hb.getInt();

                        if (size <= 0 || size > 65535) break;

                        byte[] payload = readFully(in, size);
                        if (Arrays.equals(srcMac, localMac)) continue;

                        Peer p = getOrCreatePeer("Player", srcMac, 0);
                        if (listener != null) {
                            listener.onDatagramReceived(srcMac, payload);
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

    public synchronized Peer getOrCreatePeer(String name, byte[] mac, int ip) {
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
        return peer;
    }

    public synchronized void sendPdpPacket(byte[] dstMac, byte[] data) {
        if (!running.get() || postofficeOut == null || data == null || data.length == 0 || dstMac == null) return;
        try {
            ByteBuffer pdpHeader = ByteBuffer.allocate(14).order(ByteOrder.LITTLE_ENDIAN);
            pdpHeader.put(dstMac);
            if (dstMac.length == 6) {
                pdpHeader.putShort((short) 0);
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

    public synchronized void broadcastToPeers(byte[] data) {
        if (!running.get() || postofficeOut == null || data == null || data.length == 0) return;
        try {
            byte[] broadcastMac = new byte[] { (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF, (byte)0xFF };
            sendPdpPacket(broadcastMac, data);

            for (Peer p : peers.values()) {
                if (p.mac != null && p.mac.length == 6 && !Arrays.equals(p.mac, localMac)) {
                    sendPdpPacket(p.mac, data);
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "Failed to broadcast to adhoc peers: " + e.getMessage());
        }
    }

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
                        broadcastToPeers(data);
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

    private static String formatMac(byte[] mac) {
        if (mac == null || mac.length < 6) return "00:00:00:00:00:00";
        return String.format("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

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
