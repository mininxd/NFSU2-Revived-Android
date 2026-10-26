package io.mininxd.nfsu2.net;

import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.SecureRandom;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Transparent Local TCP Bridge for NFSU2 Online Mode.
 * DirtySock connects locally to 127.0.0.1:20920 (Redirector), 20922 (Lobby), and 13505 (EA Messenger).
 * This bridge wraps the TCP stream into virtual LAN TCP frames (NET_KIND_LAN_TCP = 4)
 * and routes them over the /lan WebSocket connection to the server's virtual host at 10.77.0.1.
 */
public final class LocalOnlineBridge {
    private static final String TAG = "LocalOnlineBridge";

    public static final int VHOST_VIP = 0x0A4D0001; // 10.77.0.1
    public static final int PORT_REDIRECTOR = 20920;
    public static final int PORT_REDIRECTOR_SSL = 20921; // in case guest connects to 20921
    public static final int PORT_LOBBY = 20922;
    public static final int PORT_BUDDY = 13505;

    public static final byte TCP_OP_SYN = 1;
    public static final byte TCP_OP_SYNACK = 2;
    public static final byte TCP_OP_RST = 3;
    public static final byte TCP_OP_DATA = 4;
    public static final byte TCP_OP_FIN = 5;

    private static final byte[] TARGET_IP_BYTES = "10.77.0.1".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    private static final byte[] REPLACEMENT_IP_BYTES = "127.0.0.1".getBytes(java.nio.charset.StandardCharsets.US_ASCII);

    private final VirtualSwitchClient wsClient;
    private final AtomicBoolean running = new AtomicBoolean(false);

    private ServerSocket server20920;
    private ServerSocket server20921;
    private ServerSocket server20922;
    private ServerSocket server13505;

    private Thread thread20920;
    private Thread thread20921;
    private Thread thread20922;
    private Thread thread13505;

    private final Map<Integer, VirtualConn> connections = new ConcurrentHashMap<>();
    private final SecureRandom random = new SecureRandom();

    public static final class VirtualConn {
        public final int cid;
        public final int targetPort;
        public final int localPort;
        public final Socket localSocket;
        public final CountDownLatch synAckLatch = new CountDownLatch(1);
        public volatile boolean established = false;
        public volatile OutputStream localOut;
        public Thread readerThread;

        public VirtualConn(int cid, int targetPort, int localPort, Socket localSocket) {
            this.cid = cid;
            this.targetPort = targetPort;
            this.localPort = localPort;
            this.localSocket = localSocket;
        }

        public void close() {
            established = false;
            if (localSocket != null && !localSocket.isClosed()) {
                try { localSocket.close(); } catch (Exception ignored) {}
            }
            if (readerThread != null) {
                try { readerThread.interrupt(); } catch (Exception ignored) {}
            }
        }
    }

    public LocalOnlineBridge(VirtualSwitchClient wsClient) {
        this.wsClient = wsClient;
    }

    public synchronized void start() {
        if (running.getAndSet(true)) return;

        connections.clear();
        thread20920 = startListener(PORT_REDIRECTOR, PORT_REDIRECTOR, s -> server20920 = s);
        thread20921 = startListener(PORT_REDIRECTOR_SSL, PORT_REDIRECTOR, s -> server20921 = s);
        thread20922 = startListener(PORT_LOBBY, PORT_LOBBY, s -> server20922 = s);
        thread13505 = startListener(PORT_BUDDY, PORT_BUDDY, s -> server13505 = s);
        Log.i(TAG, "Local Online Bridge started (ports 20920, 20921, 20922, 13505)");
    }

    public synchronized void stop() {
        if (!running.getAndSet(false)) return;

        closeQuietly(server20920);
        closeQuietly(server20921);
        closeQuietly(server20922);
        closeQuietly(server13505);

        interruptQuietly(thread20920);
        interruptQuietly(thread20921);
        interruptQuietly(thread20922);
        interruptQuietly(thread13505);

        for (VirtualConn conn : connections.values()) {
            conn.close();
        }
        connections.clear();
        Log.i(TAG, "Local Online Bridge stopped");
    }

    private interface ServerSocketConsumer {
        void accept(ServerSocket s);
    }

    private Thread startListener(int listenPort, int targetPort, ServerSocketConsumer consumer) {
        Thread t = new Thread(() -> {
            ServerSocket server = null;
            try {
                server = new ServerSocket(listenPort, 50, InetAddress.getByName("127.0.0.1"));
                consumer.accept(server);
                Log.i(TAG, "Listening on 127.0.0.1:" + listenPort + " -> 10.77.0.1:" + targetPort);

                while (running.get() && !server.isClosed()) {
                    Socket client = server.accept();
                    client.setTcpNoDelay(true);
                    handleLocalClient(client, targetPort);
                }
            } catch (Exception e) {
                if (running.get()) {
                    Log.d(TAG, "Listener on port " + listenPort + " stopped: " + e.getMessage());
                }
            } finally {
                closeQuietly(server);
            }
        }, "OnlineListener-" + listenPort);
        t.setDaemon(true);
        t.start();
        return t;
    }

    private void handleLocalClient(Socket localSocket, int targetPort) {
        new Thread(() -> {
            int cid;
            do {
                cid = random.nextInt();
            } while (cid == 0 || connections.containsKey(cid));

            int localPort = localSocket.getPort();
            VirtualConn conn = new VirtualConn(cid, targetPort, localPort, localSocket);
            connections.put(cid, conn);

            try {
                conn.localOut = localSocket.getOutputStream();
                InputStream localIn = localSocket.getInputStream();

                // Send TCP SYN (op 1) to 10.77.0.1:targetPort
                Log.i(TAG, "Connecting TCP stream to 10.77.0.1:" + targetPort + " (cid=" + cid + ", lport=" + localPort + ")");
                wsClient.sendTcpFrame(VHOST_VIP, localPort, targetPort, TCP_OP_SYN, 64, cid, new byte[0]);

                // Wait for SYNACK from virtual host (up to 6s)
                if (!conn.synAckLatch.await(6000, TimeUnit.MILLISECONDS) || !conn.established) {
                    Log.w(TAG, "TCP SYN to 10.77.0.1:" + targetPort + " timed out or was refused");
                    wsClient.sendTcpFrame(VHOST_VIP, localPort, targetPort, TCP_OP_RST, 64, cid, new byte[0]);
                    conn.close();
                    connections.remove(cid);
                    return;
                }

                Log.i(TAG, "TCP connection established with 10.77.0.1:" + targetPort + " (cid=" + cid + ")");

                // Read from local socket and forward DATA (op 4) to 10.77.0.1
                byte[] buf = new byte[1440];
                int n;
                while (running.get() && (n = localIn.read(buf)) != -1) {
                    if (n > 0) {
                        byte[] payload = new byte[n];
                        System.arraycopy(buf, 0, payload, 0, n);
                        wsClient.sendTcpFrame(VHOST_VIP, localPort, targetPort, TCP_OP_DATA, 64, cid, payload);
                    }
                }

                // Send FIN (op 5) when local socket closes
                wsClient.sendTcpFrame(VHOST_VIP, localPort, targetPort, TCP_OP_FIN, 64, cid, new byte[0]);
            } catch (Exception e) {
                if (running.get()) {
                    Log.d(TAG, "Local client " + cid + " stream closed: " + e.getMessage());
                }
                wsClient.sendTcpFrame(VHOST_VIP, localPort, targetPort, TCP_OP_RST, 64, cid, new byte[0]);
            } finally {
                conn.close();
                connections.remove(cid);
            }
        }, "VirtualTcpClient-" + cidOrUnknown(targetPort)).start();
    }

    private static String cidOrUnknown(int port) {
        return "port" + port;
    }

    /**
     * Called when a NET_KIND_LAN_TCP (kind == 4) frame is received from the WebSocket.
     */
    public void handleServerTcpFrame(byte[] data) {
        if (data == null || data.length < 24) return;
        // Frame layout:
        // [0] kind (4)
        // [4..7] srcVip (10.77.0.1)
        // [8..11] dstVip (myVirtualIp)
        // [12..13] sport (e.g. 20920)
        // [14..15] dport (localEphemeralPort)
        // [16] op (1=SYN, 2=SYNACK, 3=RST, 4=DATA, 5=FIN)
        // [17] ttl
        // [18..19] pad
        // [20..23] cid (little endian)
        // [24..] payload

        int sport = ((data[12] & 0xFF) << 8) | (data[13] & 0xFF);
        int dport = ((data[14] & 0xFF) << 8) | (data[15] & 0xFF);
        int op = data[16] & 0xFF;
        int cid = ByteBuffer.wrap(data, 20, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();

        VirtualConn conn = connections.get(cid);
        if (conn == null) return;

        if (op == TCP_OP_SYNACK) {
            conn.established = true;
            conn.synAckLatch.countDown();
        } else if (op == TCP_OP_DATA) {
            if (data.length > 24 && conn.localOut != null) {
                int payloadLen = data.length - 24;
                byte[] payload = new byte[payloadLen];
                System.arraycopy(data, 24, payload, 0, payloadLen);

                // Rewrite "10.77.0.1" -> "127.0.0.1" in incoming server messages
                rewriteServerAddresses(payload);

                try {
                    conn.localOut.write(payload);
                    conn.localOut.flush();
                } catch (Exception e) {
                    conn.close();
                    connections.remove(cid);
                }
            }
        } else if (op == TCP_OP_FIN) {
            conn.established = false;
            conn.synAckLatch.countDown();
            try {
                if (conn.localSocket != null && !conn.localSocket.isOutputShutdown()) {
                    conn.localSocket.shutdownOutput();
                }
            } catch (Exception ignored) {}
        } else if (op == TCP_OP_RST) {
            conn.established = false;
            conn.synAckLatch.countDown();
            conn.close();
            connections.remove(cid);
        }
    }

    /**
     * Replaces occurrences of "10.77.0.1" with "127.0.0.1" in place (both are 9 bytes).
     */
    private static void rewriteServerAddresses(byte[] bytes) {
        if (bytes == null || bytes.length < 9) return;
        int targetLen = TARGET_IP_BYTES.length;
        int limit = bytes.length - targetLen;
        for (int i = 0; i <= limit; i++) {
            boolean match = true;
            for (int j = 0; j < targetLen; j++) {
                if (bytes[i + j] != TARGET_IP_BYTES[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                System.arraycopy(REPLACEMENT_IP_BYTES, 0, bytes, i, targetLen);
                i += targetLen - 1;
            }
        }
    }

    private static void closeQuietly(ServerSocket s) {
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
