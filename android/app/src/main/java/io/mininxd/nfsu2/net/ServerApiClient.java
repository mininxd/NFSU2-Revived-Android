package io.mininxd.nfsu2.net;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import io.mininxd.nfsu2.GameOptions;

/**
 * Handles HTTP REST calls to the NFSU2 web/server-rs instance:
 * - /players: synchronized player statistics (total, online, roam, room-specific, crews)
 * - /vault: cloud save synchronization (GET/PUT/DELETE backup saves)
 */
public final class ServerApiClient {
    private static final String TAG = "ServerApiClient";

    public interface StatsCallback {
        void onStatsUpdated(int total, int roam, int online, int room);
    }

    public interface VaultCallback<T> {
        void onSuccess(T result);
        void onError(String message);
    }

    public static final long MIN_POLL_INTERVAL_MS = 5000;
    private final AtomicLong lastPollTime = new AtomicLong(0);
    private final AtomicBoolean isPolling = new AtomicBoolean(false);

    private volatile int totalServerPlayers = 0;
    private volatile int totalRoamPlayers = 0;
    private volatile int totalOnlinePlayers = 0;
    private volatile int roomRoamPlayers = 0;

    public int getTotalServerPlayers() { return totalServerPlayers; }
    public int getTotalRoamPlayers() { return totalRoamPlayers; }
    public int getTotalOnlinePlayers() { return totalOnlinePlayers; }
    public int getRoomRoamPlayers() { return roomRoamPlayers; }
    public void setRoomRoamPlayers(int count) { if (count >= 0) this.roomRoamPlayers = count; }

    public static String getHttpBaseUrl(String hostInput) {
        if (hostInput == null || hostInput.trim().isEmpty()) {
            hostInput = GameOptions.DEFAULT_ROAM_SERVER;
        }
        String host = hostInput.trim();
        boolean useSsl = true;
        int port = 443;
        if (host.startsWith("wss://")) {
            host = host.substring(6);
            useSsl = true;
        } else if (host.startsWith("ws://")) {
            host = host.substring(5);
            useSsl = false;
            port = 80;
        } else if (host.startsWith("https://")) {
            host = host.substring(8);
            useSsl = true;
        } else if (host.startsWith("http://")) {
            host = host.substring(7);
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
        boolean isDefaultPort = (useSsl && port == 443) || (!useSsl && port == 80);
        return (useSsl ? "https://" : "http://") + host + (isDefaultPort ? "" : ":" + port);
    }

    /**
     * FNV-1a room hash matching nfsu2-web / roam_core.c rc_room_hash.
     * Empty string returns 0 (the public open world).
     */
    public static int roomHash(String name) {
        if (name == null || name.trim().isEmpty()) return 0;
        int h = 0x811c9dc5;
        int kept = 0;
        byte[] bytes = name.getBytes(StandardCharsets.UTF_8);
        for (byte b : bytes) {
            int ub = b & 0xFF;
            if (ub == 0x20 || (ub >= 0x09 && ub <= 0x0D)) continue;
            if (ub >= 0x41 && ub <= 0x5A) ub += 0x20;
            h = (h ^ ub) * 0x01000193;
            kept++;
        }
        if (kept == 0) return 0;
        return (h == 0) ? 1 : h;
    }

    public static boolean isWsHost(String host) {
        if (host == null) return false;
        String h = host.toLowerCase().trim();
        if (h.equals("adhoc.eahub.eu") || h.equals("eahub.eu")) {
            return false;
        }
        if (h.startsWith("adhoc.") && !h.contains("nfsu2")) {
            return false;
        }
        return true;
    }

    /**
     * Poll /players endpoint and update state.
     * Enforces a minimum interval of 5000 ms between network requests to prevent spam.
     */
    public boolean pollServerPlayers(String host, String room, StatsCallback callback) {
        return pollServerPlayers(host, room, false, callback);
    }

    public boolean pollServerPlayers(String host, String room, boolean force, StatsCallback callback) {
        long now = System.currentTimeMillis();
        if (!force && (now - lastPollTime.get() < MIN_POLL_INTERVAL_MS)) {
            if (callback != null) {
                callback.onStatsUpdated(totalServerPlayers, totalRoamPlayers, totalOnlinePlayers, roomRoamPlayers);
            }
            return false;
        }

        if (!isPolling.compareAndSet(false, true)) {
            if (callback != null) {
                callback.onStatsUpdated(totalServerPlayers, totalRoamPlayers, totalOnlinePlayers, roomRoamPlayers);
            }
            return false;
        }

        HttpURLConnection conn = null;
        try {
            now = System.currentTimeMillis();
            if (!force && (now - lastPollTime.get() < MIN_POLL_INTERVAL_MS)) {
                if (callback != null) {
                    callback.onStatsUpdated(totalServerPlayers, totalRoamPlayers, totalOnlinePlayers, roomRoamPlayers);
                }
                return false;
            }
            lastPollTime.set(now);

            String base = getHttpBaseUrl(host);
            int hash = roomHash(room);
            String query = (hash != 0 ? "?room=" + (hash & 0xFFFFFFFFL) : "");

            boolean hasExplicitScheme = host.startsWith("http://") || host.startsWith("https://")
                    || host.startsWith("ws://") || host.startsWith("wss://");

            String[] tryBases;
            if (!hasExplicitScheme && base.startsWith("https://")) {
                tryBases = new String[] { base, base.replaceFirst("^https://", "http://") };
            } else {
                tryBases = new String[] { base };
            }

            for (String tryBase : tryBases) {
                try {
                    String endpoint = tryBase + "/players" + query;
                    URL url = new URL(endpoint);
                    conn = (HttpURLConnection) url.openConnection();
                    conn.setConnectTimeout(4000);
                    conn.setReadTimeout(4000);
                    conn.setRequestProperty("User-Agent", "NFSU2-Revived-Android");
                    conn.setRequestProperty("Accept", "application/json");

                    int code = conn.getResponseCode();
                    if (code == 200) {
                        InputStream in = conn.getInputStream();
                        ByteArrayOutputStream baos = new ByteArrayOutputStream();
                        byte[] buf = new byte[1024];
                        int n;
                        while ((n = in.read(buf)) != -1) {
                            baos.write(buf, 0, n);
                        }
                        in.close();

                        JSONObject obj = new JSONObject(baos.toString(StandardCharsets.UTF_8.name()));
                        int p = obj.optInt("players", -1);
                        int r = obj.optInt("roam", -1);
                        int on = obj.optInt("online", -1);
                        int rm = obj.has("room") ? obj.optInt("room", -1) : r;

                        if (p >= 0) totalServerPlayers = p;
                        if (r >= 0) totalRoamPlayers = r;
                        if (on >= 0) totalOnlinePlayers = on;
                        if (rm >= 0) roomRoamPlayers = rm;

                        int activeRoomOrRoam = (rm >= 0 && hash != 0) ? rm : (r >= 0 ? r : p);
                        Log.i(TAG, "player_count: " + activeRoomOrRoam
                                + " (roam=" + totalRoamPlayers + ", online=" + totalOnlinePlayers + ", total=" + totalServerPlayers + ", room=" + roomRoamPlayers + ")");

                        if (callback != null) {
                            callback.onStatsUpdated(totalServerPlayers, totalRoamPlayers, totalOnlinePlayers, roomRoamPlayers);
                        }
                        return true;
                    }
                } catch (Exception ex) {
                    // Try next fallback URL if available
                } finally {
                    if (conn != null) {
                        try { conn.disconnect(); } catch (Exception ignored) {}
                        conn = null;
                    }
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "pollServerPlayers failed: " + e.getMessage());
        } finally {
            isPolling.set(false);
            if (conn != null) {
                try { conn.disconnect(); } catch (Exception ignored) {}
            }
        }
        return false;
    }

    public void queryServerPlayers(Context context, Runnable onDone) {
        if (!GameOptions.isDisplayLogsEnabled(context)) {
            if (onDone != null) onDone.run();
            return;
        }
        String server = GameOptions.onlineServer(context);
        if (!isWsHost(server)) {
            if (onDone != null) onDone.run();
            return;
        }
        new Thread(() -> {
            pollServerPlayers(server, GameOptions.roamRoom(context), null);
            if (onDone != null) onDone.run();
        }, "AsyncServerQuery").start();
    }

    // ---- Vault Cloud Save API --------------------------------------------------------------------------

    /**
     * Download savegame backup from /vault with Bearer token authentication.
     */
    public void restoreSave(String host, String token, VaultCallback<byte[]> callback) {
        new Thread(() -> {
            HttpURLConnection conn = null;
            try {
                String base = getHttpBaseUrl(host);
                URL url = new URL(base + "/vault");
                conn = (HttpURLConnection) url.openConnection();
                conn.setRequestMethod("GET");
                conn.setConnectTimeout(8000);
                conn.setReadTimeout(10000);
                conn.setRequestProperty("User-Agent", "NFSU2-Revived-Android");
                if (token != null && !token.trim().isEmpty()) {
                    conn.setRequestProperty("Authorization", "Bearer " + token.trim());
                }

                int code = conn.getResponseCode();
                if (code == 200) {
                    InputStream in = conn.getInputStream();
                    ByteArrayOutputStream baos = new ByteArrayOutputStream();
                    byte[] buf = new byte[4096];
                    int n;
                    while ((n = in.read(buf)) != -1) baos.write(buf, 0, n);
                    in.close();
                    if (callback != null) callback.onSuccess(baos.toByteArray());
                } else if (code == 404) {
                    if (callback != null) callback.onError("No cloud save found for this keyphrase");
                } else {
                    if (callback != null) callback.onError("Server returned status " + code);
                }
            } catch (Exception e) {
                if (callback != null) callback.onError(e.getMessage());
            } finally {
                if (conn != null) conn.disconnect();
            }
        }, "VaultRestore").start();
    }

    /**
     * Upload savegame backup to /vault with Bearer token authentication.
     */
    public void backupSave(String host, String token, byte[] data, VaultCallback<Boolean> callback) {
        new Thread(() -> {
            HttpURLConnection conn = null;
            try {
                String base = getHttpBaseUrl(host);
                URL url = new URL(base + "/vault");
                conn = (HttpURLConnection) url.openConnection();
                conn.setRequestMethod("PUT");
                conn.setDoOutput(true);
                conn.setConnectTimeout(8000);
                conn.setReadTimeout(10000);
                conn.setRequestProperty("User-Agent", "NFSU2-Revived-Android");
                conn.setRequestProperty("Content-Type", "application/octet-stream");
                if (token != null && !token.trim().isEmpty()) {
                    conn.setRequestProperty("Authorization", "Bearer " + token.trim());
                }

                OutputStream out = conn.getOutputStream();
                out.write(data);
                out.flush();
                out.close();

                int code = conn.getResponseCode();
                if (code >= 200 && code < 300) {
                    if (callback != null) callback.onSuccess(true);
                } else {
                    if (callback != null) callback.onError("Server rejected backup with status " + code);
                }
            } catch (Exception e) {
                if (callback != null) callback.onError(e.getMessage());
            } finally {
                if (conn != null) conn.disconnect();
            }
        }, "VaultBackup").start();
    }
}
