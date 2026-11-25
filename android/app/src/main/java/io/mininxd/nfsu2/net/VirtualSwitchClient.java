package io.mininxd.nfsu2.net;

import android.content.Context;
import android.content.SharedPreferences;
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
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
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

    private static volatile boolean nativeLoaded = false;
    static {
        try {
            System.loadLibrary("roam_rs");
            nativeLoaded = true;
        } catch (Throwable ignored) {}
    }

    private static native byte[] nativeBuildWsFrame(int opcode, byte[] payload, byte[] mask);
    private static native byte[] nativeBuildUdpFrame(int srcVip, int dstVip, int dport, byte[] payload);
    private static native byte[] nativeParseUdpFrame(byte[] frame, int[] outInfo);

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

    private static final long UDP_ONLINE_PORT_MASK = 0x80000000L | (1L << (3658 % 31));

    private static String sLastToken = "";
    private static String sLastVipStr = "";
    private static String sLastHost = "";
    private static long sLastDisconnectedMs = 0L;

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private final Set<Integer> livePeerVips = Collections.synchronizedSet(new HashSet<>());
    private volatile boolean wasResumed = false;
    private Thread wsThread;
    private Socket wsSocket;
    private OutputStream wsOut;

    private Context context;
    private volatile int myVirtualIp = 0;
    private String serverHost;
    private String playerName;
    private SwitchListener listener;
    private LocalOnlineBridge onlineBridge;

    public VirtualSwitchClient() {}

    public void setContext(Context context) {
        this.context = context != null ? context.getApplicationContext() : null;
        if (this.context != null) {
            SharedPreferences prefs = this.context.getSharedPreferences("nfsu2_switch", Context.MODE_PRIVATE);
            if (sLastToken.isEmpty()) sLastToken = prefs.getString("switch_token", "");
            if (sLastVipStr.isEmpty()) sLastVipStr = prefs.getString("switch_vip_str", "");
            if (sLastHost.isEmpty()) sLastHost = prefs.getString("switch_host", "");
        }
    }

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
                            Socket underlying = new Socket();
                            underlying.connect(new java.net.InetSocketAddress(host, curPort), 6000);
                            wsSocket = factory.createSocket(underlying, host, curPort, true);
                        } else {
                            wsSocket = new Socket();
                            wsSocket.connect(new java.net.InetSocketAddress(host, curPort), 6000);
                        }
                        wsSocket.setTcpNoDelay(true);
                        wsSocket.setSoTimeout(5000);

                        OutputStream rawOut = new BufferedOutputStream(wsSocket.getOutputStream());
                        InputStream rawIn = new BufferedInputStream(wsSocket.getInputStream());

                        synchronized (this) {
                            wsOut = rawOut;
                        }

                        // RFC 6455 Handshake: resume only if same host disconnected recently; otherwise /lan?queue
                        long nowMs = System.currentTimeMillis();
                        boolean isResume = (sLastToken != null && !sLastToken.isEmpty()
                                && host.equalsIgnoreCase(sLastHost)
                                && sLastDisconnectedMs > 0L
                                && (nowMs - sLastDisconnectedMs) < 55000L);
                        wasResumed = isResume;
                        String secKey = Base64.encodeToString(UUID.randomUUID().toString().substring(0, 16).getBytes(StandardCharsets.US_ASCII), Base64.NO_WRAP);
                        String origin = (curSsl ? "https://" : "http://") + host + (curPort == 80 || curPort == 443 ? "" : (":" + curPort));
                        String hostHeader = host + (curPort == 80 || curPort == 443 ? "" : (":" + curPort));
                        String path = isResume ? "/lan?resume" : "/lan?queue";
                        String handshake = "GET " + path + " HTTP/1.1\r\n" +
                                "Host: " + hostHeader + "\r\n" +
                                "Upgrade: websocket\r\n" +
                                "Connection: Upgrade\r\n" +
                                "Sec-WebSocket-Key: " + secKey + "\r\n" +
                                "Sec-WebSocket-Version: 13\r\n" +
                                "Origin: " + origin + "\r\n\r\n";
                        NetLog.step(TAG, "Sending WebSocket Upgrade GET " + path + " (Host=" + hostHeader + ", Origin=" + origin + ")");
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
                            if (wasResumed) {
                                sLastToken = "";
                                sLastVipStr = "";
                                if (context != null) {
                                    context.getSharedPreferences("nfsu2_switch", Context.MODE_PRIVATE).edit()
                                            .remove("switch_token")
                                            .remove("switch_vip_str")
                                            .apply();
                                }
                            }
                            NetLog.fail(TAG, "WebSocket upgrade rejected by server: " + statusLine.split("\r\n")[0]);
                            throw new IOException("WebSocket upgrade rejected: " + statusLine.split("\r\n")[0]);
                        }

                        Log.i(TAG, "WebSocket connected successfully (101 Switching Protocols)");
                        NetLog.success(TAG, "WebSocket upgrade accepted (101 Switching Protocols) on " + host + ":" + curPort);
                        connectedEver = true;
                        long lastPingTime = System.currentTimeMillis();

                        if (isResume) {
                            JSONObject hello = new JSONObject();
                            hello.put("t", "hello");
                            hello.put("name", playerName != null ? playerName : "Racer");
                            hello.put("bundle", 0);
                            hello.put("compact", 0);
                            hello.put("deltas", 1);
                            hello.put("lan", 1);
                            hello.put("udp", UDP_ONLINE_PORT_MASK);
                            if (sLastVipStr != null && !sLastVipStr.isEmpty()) {
                                hello.put("vip", sLastVipStr);
                            }
                            hello.put("token", sLastToken);
                            NetLog.step(TAG, "Sending resume hello to switch: vip=" + sLastVipStr + ", token=" + sLastToken + ", name=" + playerName);
                            sendWsFrame(1, hello.toString().getBytes(StandardCharsets.UTF_8));
                        }

                        // Receive loop
                        while (running.get()) {
                            int b0;
                            try {
                                b0 = rawIn.read();
                            } catch (SocketTimeoutException timeout) {
                                if (System.currentTimeMillis() - lastPingTime > 2500) {
                                    sendPing();
                                    lastPingTime = System.currentTimeMillis();
                                }
                                continue;
                            }
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
                        if (wasResumed) {
                            sLastToken = "";
                            sLastVipStr = "";
                            if (context != null) {
                                context.getSharedPreferences("nfsu2_switch", Context.MODE_PRIVATE).edit()
                                        .remove("switch_token")
                                        .remove("switch_vip_str")
                                        .apply();
                            }
                        }
                        if (running.get()) {
                            String msg = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
                            Log.w(TAG, "Switch connection attempt failed (" + curPort + "): " + msg);
                            NetLog.fail(TAG, "Connection attempt failed (" + curPort + "): " + msg, e);
                            if (attempt == tryPorts.length - 1 && listener != null) {
                                listener.onDisconnected(msg);
                            }
                        }
                    } finally {
                        if (connected.getAndSet(false)) {
                            sLastDisconnectedMs = System.currentTimeMillis();
                        }
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
                String token = obj.optString("token", "");
                if (token != null && !token.isEmpty()) sLastToken = token;
                if (vipStr != null && !vipStr.isEmpty()) sLastVipStr = vipStr;
                sLastHost = io.mininxd.nfsu2.GameOptions.extractHost(serverHost);
                sLastDisconnectedMs = 0L;
                livePeerVips.clear();
                if (context != null) {
                    try {
                        context.getSharedPreferences("nfsu2_switch", Context.MODE_PRIVATE).edit()
                                .putString("switch_token", sLastToken)
                                .putString("switch_vip_str", sLastVipStr)
                                .putString("switch_host", sLastHost)
                                .apply();
                    } catch (Exception ignored) {}
                }
                Log.i(TAG, "Switch welcome: VIP=" + vipStr + " (" + myVirtualIp + ")");
                NetLog.success(TAG, "Switch welcome: VIP=" + vipStr + " (" + myVirtualIp + "), token=" + obj.optString("token", "none"));
                connected.set(true);

                if (!wasResumed) {
                    JSONObject hello = new JSONObject();
                    hello.put("t", "hello");
                    hello.put("name", playerName != null ? playerName : "Racer");
                    hello.put("bundle", 0);
                    hello.put("compact", 0);
                    hello.put("deltas", 1);
                    hello.put("lan", 1);
                    hello.put("udp", UDP_ONLINE_PORT_MASK);
                    if (sLastToken != null && !sLastToken.isEmpty() && sLastVipStr != null && !sLastVipStr.isEmpty()) {
                        hello.put("token", sLastToken);
                        hello.put("vip", sLastVipStr);
                    }
                    NetLog.step(TAG, "Sending hello to switch: bundle=0, compact=0, lan=1, name=" + playerName);
                    sendWsFrame(1, hello.toString().getBytes(StandardCharsets.UTF_8));
                }

                if (listener != null) listener.onConnected(myVirtualIp);
            } else if ("queue".equals(type)) {
                int pos = obj.optInt("pos", obj.optInt("place", 0));
                NetLog.step(TAG, "Waiting in /lan switch queue" + (pos > 0 ? (" (position " + pos + ")") : ""));
            } else if ("ping".equals(type)) {
                int id = obj.optInt("id", 0);
                JSONObject pong = new JSONObject();
                pong.put("t", "pong");
                pong.put("id", id);
                sendWsFrame(1, pong.toString().getBytes(StandardCharsets.UTF_8));
            } else if ("peers".equals(type)) {
                JSONArray arr = obj.optJSONArray("peers");
                if (arr != null) {
                    synchronized (livePeerVips) {
                        livePeerVips.clear();
                        for (int i = 0; i < arr.length(); i++) {
                            JSONObject p = arr.optJSONObject(i);
                            if (p != null && !p.optBoolean("server", false)) {
                                int vip = p.optInt("vip", 0);
                                if (vip != 0 && vip != myVirtualIp) {
                                    livePeerVips.add(vip);
                                }
                            }
                        }
                    }
                    if (listener != null) {
                        listener.onPeersCountUpdated(livePeerVips.size());
                    }
                }
            } else if ("delta".equals(type)) {
                JSONArray added = obj.optJSONArray("peers");
                JSONArray gone = obj.optJSONArray("gone");
                synchronized (livePeerVips) {
                    if (added != null) {
                        for (int i = 0; i < added.length(); i++) {
                            JSONObject p = added.optJSONObject(i);
                            if (p != null && !p.optBoolean("server", false)) {
                                int vip = p.optInt("vip", 0);
                                if (vip != 0 && vip != myVirtualIp) {
                                    livePeerVips.add(vip);
                                }
                            }
                        }
                    }
                    if (gone != null) {
                        for (int i = 0; i < gone.length(); i++) {
                            int vip = gone.optInt(i, 0);
                            if (vip != 0) {
                                livePeerVips.remove(vip);
                            }
                        }
                    }
                }
                if (listener != null) {
                    listener.onPeersCountUpdated(livePeerVips.size());
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
            int destPort = (dport != 0 ? dport : DEFAULT_GAME_PORT);
            if (nativeLoaded) {
                byte[] frame = nativeBuildUdpFrame(myVirtualIp, dstVip, destPort, payload);
                if (frame != null && frame.length > 0) {
                    sendWsFrame(2, frame);
                    NetLog.i(TAG, "Sent switch UDP dgram to " + NetLog.ipStr(dstVip) + ":" + destPort
                            + " (len=" + payload.length + ")");
                    return;
                }
            }
            ByteBuffer buf = ByteBuffer.allocate(16 + payload.length);
            buf.put((byte) KIND_UDP);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.put((byte) 0);
            buf.putInt(myVirtualIp);
            buf.putInt(dstVip);
            buf.putShort((short) DEFAULT_GAME_PORT);
            buf.putShort((short) destPort);
            buf.put(payload);

            sendWsFrame(2, buf.array());
            NetLog.i(TAG, "Sent switch UDP dgram to " + NetLog.ipStr(dstVip) + ":" + destPort
                    + " (len=" + payload.length + ")");
        } catch (Exception e) {
            Log.d(TAG, "sendDgram failed: " + e.getMessage());
            NetLog.fail(TAG, "sendDgram to " + NetLog.ipStr(dstVip) + " failed: " + e.getMessage(), e);
        }
    }

    public synchronized void sendTcpFrame(int dstVip, int sport, int dport, int op, int ttl, int cid, byte[] data) {
        if (!running.get() || wsOut == null || myVirtualIp == 0) return;
        try {
            byte[] frame = LocalOnlineBridge.buildTcpFrame(myVirtualIp, dstVip, sport, dport, op, ttl, cid, data);
            sendWsFrame(2, frame);
            int payloadLen = (data != null) ? data.length : 0;
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

        if (nativeLoaded) {
            try {
                byte[] raw = nativeBuildWsFrame(opcode, payload, mask);
                if (raw != null && raw.length > 0) {
                    wsOut.write(raw);
                    wsOut.flush();
                    return;
                }
            } catch (Throwable ignored) {}
        }

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
