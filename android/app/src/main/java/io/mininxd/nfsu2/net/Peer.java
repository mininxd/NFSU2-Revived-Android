package io.mininxd.nfsu2.net;

import java.net.DatagramSocket;

/**
 * Representation of a connected peer in the Free Roam network.
 */
public final class Peer {
    public final String name;
    public final byte[] mac;
    public final int ip;
    public final int virtualPort;
    public DatagramSocket socket;
    public Thread worker;
    public volatile long lastSeen;

    public Peer(String name, byte[] mac, int ip, int virtualPort) {
        this.name = (name != null && !name.trim().isEmpty()) ? name.trim() : "Player";
        this.mac = mac != null ? mac.clone() : new byte[6];
        this.ip = ip;
        this.virtualPort = virtualPort;
        this.lastSeen = System.currentTimeMillis();
    }

    public String getMacString() {
        if (mac == null || mac.length < 6) return "00:00:00:00:00:00";
        return String.format("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
