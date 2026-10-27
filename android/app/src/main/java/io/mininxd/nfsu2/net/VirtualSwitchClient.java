package io.mininxd.nfsu2.net;

import android.util.Base64;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;

import javax.net.ssl.SSLSocketFactory;

/**
 * Handles RFC 6455 WebSocket client communication with the NFSU2 /lan virtual switch.
 * Encapsulates and decapsulates:
 * - NET_KIND_UDP (1): Free Roam peer datagrams
 * - NET_KIND_LAN_TCP (4): Online Mode TCP virtual host stream
 * - NET_KIND_BUNDLE (32): Batched multi-frame packets
 */
public final class VirtualSwitchClient {
    private static final String TAG = "VirtualSwitchClient";

    public static final int DEFAULT_GAME_PORT = 47210;
    public static final int KIND_UDP = 1;
    public static final int KIND_LAN_UDP = 3;
    public static final int KIND_TCP = 4;
    public static final int KIND_LAN_ICMP = 5;
    public static final int KIND_BUNDLE = 32;

    public interface SwitchListener {
        void onConnected(int virtualIp);
        void onDisconnected(String reason);
        void onPeersCountUpdated(int totalPeers);
        void onDatagramReceived(int srcVip, int sport, byte[] payload);
    }

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private Thread wsThread;
    private Socket wsSocket;
    private OutputStream wsOut;

    private volatile int myVirtualIp = 0;
    private String serverHost;
    private String playerName;
    private SwitchListener listener;
    private LocalOnlineBridge onlineBridge;

    public VirtualSwitchClient() {}

    public void setListener(SwitchListener listener) {
        this.listener = listener;
    }

    public void setOnlineBridge(LocalOnlineBridge onlineBridge) {
        this.onlineBridge = onlineBridge;
    }

    public boolean isConnected() {
        return connected.get();
    }

    public int getVirtualIp() {
        return myVirtualIp;
    }

    public synchronized void start(String serverHost, String playerName) {
        this.serverHost = serverHost;
        this.playerName = playerName;
        if (running.getAndSet(true)) return;

        wsThread = new Thread(this::runWebSocketLoop, "SwitchWorker");
        wsThread.setDaemon(true);
        wsThread.start();
    }

    public synchronized void stop() {
        if (!running.getAndSet(false)) return;
        connected.set(false);
        synchronized (this) {
            wsOut = null;
        }
        closeQuietly(wsSocket);
        if (wsThread != null) {
            try { wsThread.interrupt(); } catch (Exception ignored) {}
        }
    }

    private void runWebSocketLoop() {
        while (running.get()) {
            try {
                String host = serverHost;
                boolean useSsl = true;
                int port = 443;
                boolean explicitPort = false;
                if (host.startsWith("wss://")) {
                    host = host.substring(6);
                    useSsl = true;
                    explicitPort = true;
                } else if (host.startsWith("ws://")) {
                    host = host.substring(5);
                    useSsl = false;
                    port = 80;
                    explicitPort = true;
                } else if (host.startsWith("https://")) {
                    host = host.substring(8);
                    useSsl = true;
                    explicitPort = true;
                } else if (host.startsWith("http://")) {
                    host = host.substring(7);
                    useSsl = false;
                    port = 80;
                    explicitPort = true;
                }
                if (host.contains("/")) {
                    host = host.substring(0, host.indexOf("/"));
                }
                if (host.contains(":")) {
                    String[] parts = host.split(":");
                    host = parts[0];
                    try {
                        port = Integer.parseInt(parts[1]);
                        explicitPort = true;
                    } catch (Exception ignored) {}
                }

                int[] tryPorts = explicitPort ? new int[] { port } : (useSsl ? new int[] { 443, 80 } : new int[] { 80, 443 });
                boolean[] trySsl = explicitPort ? new boolean[] { useSsl } : (useSsl ? new boolean[] { true, false } : new boolean[] { false, true });

                boolean connectedEver = false;
                for (int attempt = 0; attempt < tryPorts.length && running.get(); attempt++) {
                    int curPort = tryPorts[attempt];
                    boolean curSsl = trySsl[attempt];
                    try {
                        Log.i(TAG, "Connecting to /lan switch " + (curSsl ? "wss://" : "ws://") + host + ":" + curPort);
                        NetLog.step(TAG, "Connecting to /lan switch: " + (curSsl ? "wss://" : "ws://") + host + ":" + curPort
                                + " (attempt " + (attempt + 1) + "/" + tryPorts.length + ")");
                        if (curSsl) {
                            SSLSocketFactory factory = (SSLSocketFactory) SSLSocketFactory.getDefault();
                            wsSocket = factory.createSocket(host, curPort);
                        } else {
                            wsSocket = new Socket(host, curPort);
                        }
                        wsSocket.setTcpNoDelay(true);
                        wsSocket.setSoTimeout(35000);

                        OutputStream rawOut = new BufferedOutputStream(wsSocket.getOutputStream());
                        InputStream rawIn = new BufferedInputStream(wsSocket.getInputStream());

                        synchronized (this) {
                            wsOut = rawOut;
                        }

                        // RFC 6455 Handshake
                        String secKey = Base64.encodeToString(UUID.randomUUID().toString().substring(0, 16).getBytes(StandardCharsets.US_ASCII), Base64.NO_WRAP);
                        String origin = (curSsl ? "https://" : "http://") + host + (curPort == 80 || curPort == 443 ? "" : (":" + curPort));
                        String hostHeader = host + (curPort == 80 || curPort == 443 ? "" : (":" + curPort));
                        String handshake = "GET /lan HTTP/1.1\r\n" +
                                "Host: " + hostHeader + "\r\n" +
                                "Upgrade: websocket\r\n" +
                                "Connection: Upgrade\r\n" +
                                "Sec-WebSocket-Key: " + secKey + "\r\n" +
                                "Sec-WebSocket-Version: 13\r\n" +
                                "Origin: " + origin + "\r\n\r\n";
                        NetLog.step(TAG, "Sending WebSocket Upgrade GET /lan (Host=" + hostHeader + ", Origin=" + origin + ")");
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
                            NetLog.fail(TAG, "WebSocket upgrade rejected by server: " + statusLine.split("\r\n")[0]);
                            throw new IOException("WebSocket upgrade rejected: " + statusLine.split("\r\n")[0]);
                        }

                        Log.i(TAG, "WebSocket connected successfully (101 Switching Protocols)");
                        NetLog.success(TAG, "WebSocket upgrade accepted (101 Switching Protocols) on " + host + ":" + curPort);
                        connectedEver = true;
                        long lastPingTime = System.currentTimeMillis();

                        // Receive loop
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
                                handleTextMessage(new String(payload, StandardCharsets.UTF_8));
                            } else if (opcode == 2) { // Binary frame
                                handleBinaryMessage(payload);
                            } else if (opcode == 9) { // Ping
                                sendWsFrame(10, payload); // Pong
                            } else if (opcode == 8) { // Close
                                break;
                            }

                            if (System.currentTimeMillis() - lastPingTime > 2500) {
                                sendPing();
                                lastPingTime = System.currentTimeMillis();
                            }
                        }
                    } catch (Exception e) {
                        if (running.get()) {
                            String msg = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
                            Log.w(TAG, "Switch connection attempt failed (" + curPort + "): " + msg);
                            NetLog.fail(TAG, "Connection attempt failed (" + curPort + "): " + msg, e);
                            if (attempt == tryPorts.length - 1 && listener != null) {
                                listener.onDisconnected(msg);
                            }
                        }
                    } finally {
                        connected.set(false);
                        synchronized (this) {
                            wsOut = null;
                        }
                        closeQuietly(wsSocket);
                    }

                    if (connectedEver) {
                        break;
                    }
                }
            } catch (Exception e) {
                if (running.get()) {
                    String msg = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
                    Log.w(TAG, "Switch error: " + msg);
                    NetLog.fail(TAG, "Switch loop error: " + msg, e);
                    if (listener != null) listener.onDisconnected(msg);
                }
            }
            if (running.get()) {
                sleepQuietly(4000);
            }
        }
    }

    private void handleTextMessage(String jsonStr) {
        try {
            JSONObject obj = new JSONObject(jsonStr);
            String type = obj.optString("t", "");
            if ("welcome".equals(type)) {
                myVirtualIp = obj.optInt("vip", 0);
                String vipStr = obj.optString("vipStr", "");
                Log.i(TAG, "Switch welcome: VIP=" + vipStr + " (" + myVirtualIp + ")");
                NetLog.success(TAG, "Switch welcome: VIP=" + vipStr + " (" + myVirtualIp + "), token=" + obj.optString("token", "none"));
                connected.set(true);

                // Send hello packet with lan=0, bundle=0 for immediate Free Roam datagrams
                JSONObject hello = new JSONObject();
                hello.put("t", "hello");
                hello.put("name", playerName != null ? playerName : "Racer");
                hello.put("bundle", 0);
                hello.put("deltas", 1);
                hello.put("lan", 0);
                NetLog.step(TAG, "Sending hello to switch: bundle=0, lan=0, name=" + playerName);
                sendWsFrame(1, hello.toString().getBytes(StandardCharsets.UTF_8));

                if (listener != null) listener.onConnected(myVirtualIp);
            } else if ("ping".equals(type)) {
                int id = obj.optInt("id", 0);
                JSONObject pong = new JSONObject();
                pong.put("t", "pong");
                pong.put("id", id);
                sendWsFrame(1, pong.toString().getBytes(StandardCharsets.UTF_8));
            } else if ("peers".equals(type)) {
                JSONArray arr = obj.optJSONArray("peers");
                if (arr != null && listener != null) {
                    listener.onPeersCountUpdated(arr.length());
                }
            } else if ("delta".equals(type)) {
                JSONArray added = obj.optJSONArray("peers");
                JSONArray gone = obj.optJSONArray("gone");
                int addedCount = added != null ? added.length() : 0;
                int goneCount = gone != null ? gone.length() : 0;
                if (listener != null) {
                    listener.onPeersCountUpdated(addedCount - goneCount);
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "handleTextMessage: " + e.getMessage());
        }
    }

    private void handleBinaryMessage(byte[] data) {
        if (data == null || data.length < 4) return;
        int kind = data[0] & 0xFF;

        if (kind == KIND_UDP) {
            parseUdpFrame(data);
        } else if (kind == KIND_LAN_UDP) {
            parseLanUdpFrame(data);
        } else if (kind == KIND_TCP) {
            if (onlineBridge != null) {
                onlineBridge.handleServerTcpFrame(data);
            }
        } else if (kind == KIND_BUNDLE) {
            int at = 4;
            while (at + 4 <= data.length) {
                int k = data[at] & 0xFF;
                int flen = ((data[at + 2] & 0xFF) << 8) | (data[at + 3] & 0xFF);
                int f = at + 4 + 4 * k;
                int end = f + flen;
                if (flen >= 16 && end <= data.length) {
                    byte[] frame = Arrays.copyOfRange(data, f, end);
                    int frameKind = frame[0] & 0xFF;
                    if (frameKind == KIND_UDP) {
                        parseUdpFrame(frame);
                    } else if (frameKind == KIND_LAN_UDP) {
                        parseLanUdpFrame(frame);
                    } else if (frameKind == KIND_TCP) {
                        if (onlineBridge != null) {
                            onlineBridge.handleServerTcpFrame(frame);
                        }
                    }
                }
                at = end;
            }
        }
    }

    private void parseUdpFrame(byte[] frame) {
        if (frame.length < 16) return;
        int srcVip = ((frame[4] & 0xFF) << 24) | ((frame[5] & 0xFF) << 16) | ((frame[6] & 0xFF) << 8) | (frame[7] & 0xFF);
        int sport = ((frame[12] & 0xFF) << 8) | (frame[13] & 0xFF);
        if (sport == 0) sport = DEFAULT_GAME_PORT;
        if (sport == 9999) return;
        if (srcVip != myVirtualIp) {
            byte[] payload = Arrays.copyOfRange(frame, 16, frame.length);
            if (listener != null) {
                listener.onDatagramReceived(srcVip, sport, payload);
            }
        }
    }

    private void parseLanUdpFrame(byte[] frame) {
        if (frame.length < 24) return;
        int srcVip = ((frame[4] & 0xFF) << 24) | ((frame[5] & 0xFF) << 16) | ((frame[6] & 0xFF) << 8) | (frame[7] & 0xFF);
        int sport = ((frame[12] & 0xFF) << 8) | (frame[13] & 0xFF);
        if (sport == 0) sport = DEFAULT_GAME_PORT;
        if (sport == 9999) return;
        if (srcVip != myVirtualIp) {
            byte[] payload = Arrays.copyOfRange(frame, 24, frame.length);
            if (listener != null) {
                listener.onDatagramReceived(srcVip, sport, payload);
            }
        }
    }

    public synchronized void sendDgram(int dstVip, int dport, byte[] payload) {
        if (!running.get() || wsOut == null || payload == null || payload.length == 0 || myVirtualIp == 0) return;
        try {
            ByteBuffer buf = ByteBuffer.allocate(16 + payload.length);
            buf.put((byte) KIND_UDP);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.putInt(myVirtualIp);
            buf.putInt(dstVip);
            buf.putShort((short) DEFAULT_GAME_PORT);
            buf.putShort((short) (dport != 0 ? dport : DEFAULT_GAME_PORT));
            buf.put(payload);

            sendWsFrame(2, buf.array());
            NetLog.i(TAG, "Sent switch UDP dgram to " + NetLog.ipStr(dstVip) + ":" + (dport != 0 ? dport : DEFAULT_GAME_PORT)
                    + " (len=" + payload.length + ")");
        } catch (Exception e) {
            Log.d(TAG, "sendDgram failed: " + e.getMessage());
            NetLog.fail(TAG, "sendDgram to " + NetLog.ipStr(dstVip) + " failed: " + e.getMessage(), e);
        }
    }

    public synchronized void sendTcpFrame(int dstVip, int sport, int dport, int op, int ttl, int cid, byte[] data) {
        if (!running.get() || wsOut == null || myVirtualIp == 0) return;
        try {
            int payloadLen = (data != null) ? data.length : 0;
            ByteBuffer buf = ByteBuffer.allocate(24 + payloadLen).order(ByteOrder.BIG_ENDIAN);
            buf.put((byte) KIND_TCP);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.putInt(myVirtualIp);
            buf.putInt(dstVip);
            buf.putShort((short) sport);
            buf.putShort((short) dport);

            // LAN header: [16] op, [17] ttl, [18..19] 0, [20..23] cid (little-endian)
            buf.put((byte) op);
            buf.put((byte) ttl);
            buf.putShort((short) 0);
            buf.order(ByteOrder.LITTLE_ENDIAN);
            buf.putInt(cid);
            buf.order(ByteOrder.BIG_ENDIAN);

            if (payloadLen > 0) {
                buf.put(data);
            }

            sendWsFrame(2, buf.array());
            NetLog.i(TAG, "Sent TCP frame: op=" + op + ", dst=" + NetLog.ipStr(dstVip) + ":" + dport
                    + ", cid=" + cid + ", len=" + payloadLen + (payloadLen > 0 ? ", hex=" + NetLog.hexPreview(data, 16) : ""));
        } catch (Exception e) {
            Log.d(TAG, "sendTcpFrame failed: " + e.getMessage());
            NetLog.fail(TAG, "sendTcpFrame failed: " + e.getMessage(), e);
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

    private void sendPing() {
        try {
            JSONObject ping = new JSONObject();
            ping.put("t", "ping");
            ping.put("id", (int) (System.currentTimeMillis() % 100000));
            sendWsFrame(1, ping.toString().getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {}
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
}
