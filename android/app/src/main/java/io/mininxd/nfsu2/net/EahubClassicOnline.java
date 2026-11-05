package io.mininxd.nfsu2.net;

import android.content.Context;
import android.util.Log;

import io.mininxd.nfsu2.GameOptions;

/**
 * Classic EA Nation / eahub.eu online mode handler.
 *
 * eahub.eu runs the original DirtySock EA Nation server stack (FESL + theater),
 * which NFSU2's built-in networking talks to directly over plain TCP — no WebSocket,
 * no LocalOnlineBridge, no SSL wrapping needed.
 *
 * Protocol summary:
 *   - gethostbyname("*.ea.com")  → redirected to eahub.eu by guest_network.inc
 *   - Redirector on port 20920   (plain TCP — the '*' SSL prefix is stripped at load time)
 *   - Lobby    on port 20922     (plain TCP)
 *   - Buddy    on port 13505     (plain TCP)
 *
 * This class:
 *   1. Validates that eahub.eu is reachable before the game starts, so users
 *      get a clear error instead of a silent hang inside the game.
 *   2. Logs a structured connection report to NetLog so the diagnostic ZIP
 *      always contains online-mode context.
 *   3. Is the single place where "classic vs WebSocket" routing is decided —
 *      GameActivity delegates to {@link #shouldUseLocalBridge(String)} instead
 *      of calling isWsHost() and isEahubClassic() separately.
 */
public final class EahubClassicOnline {
    private static final String TAG = "EahubClassic";

    /** Port the classic Redirector listens on (plain TCP, SSL asterisk stripped). */
    public static final int PORT_REDIRECTOR = 20920;
    /** Port the classic Lobby listens on. */
    public static final int PORT_LOBBY = 20922;
    /** Port the classic EA Buddy/messenger listens on. */
    public static final int PORT_BUDDY = 13505;

    /** Connection probe timeout in milliseconds. */
    private static final int PROBE_TIMEOUT_MS = 4000;

    private EahubClassicOnline() {}

    /**
     * Returns true if the given server host should be routed through
     * {@link LocalOnlineBridge} (the WebSocket nfsu2-web path).
     *
     * eahub.eu and any *.eahub.eu subdomain always use the classic TCP path —
     * they must NOT go through LocalOnlineBridge.
     */
    public static boolean shouldUseLocalBridge(String server) {
        if (server == null || server.trim().isEmpty()) return false;
        if (GameOptions.isEahubClassic(server)) return false;
        return ServerApiClient.isWsHost(server);
    }

    /**
     * Probes eahub.eu reachability on the redirector port in a background thread.
     * Logs the result to NetLog; calls {@code onResult} on completion.
     *
     * @param host     the online server host (typically "eahub.eu")
     * @param onResult callback receiving {@code true} if reachable, {@code false} otherwise
     */
    public static void probeAsync(String host, java.util.function.Consumer<Boolean> onResult) {
        new Thread(() -> {
            boolean ok = probePort(host, PORT_REDIRECTOR);
            if (!ok) {
                // The lobby port is a lighter check — try it as fallback
                ok = probePort(host, PORT_LOBBY);
            }
            if (onResult != null) onResult.accept(ok);
        }, "EahubProbe").start();
    }

    /**
     * Synchronous TCP reachability probe. Opens a plain TCP socket to {@code host:port}
     * with {@link #PROBE_TIMEOUT_MS} timeout. Returns true on successful connect.
     */
    public static boolean probePort(String host, int port) {
        try (java.net.Socket sock = new java.net.Socket()) {
            sock.connect(new java.net.InetSocketAddress(host, port), PROBE_TIMEOUT_MS);
            NetLog.success(TAG, "Classic probe OK: " + host + ":" + port);
            return true;
        } catch (Exception e) {
            NetLog.fail(TAG, "Classic probe failed: " + host + ":" + port + " — " + e.getMessage());
            return false;
        }
    }

    /**
     * Logs a startup summary to NetLog so diagnostic ZIPs always contain
     * which online mode was selected and what server is in use.
     *
     * @param context   Android context
     * @param server    resolved online server string
     * @param isClassic whether the classic (eahub) path was chosen
     */
    public static void logStartup(Context context, String server, boolean isClassic) {
        String mode = isClassic ? "classic-eahub (direct TCP)" : "nfsu2-web (WebSocket bridge)";
        String roam = GameOptions.roamServer(context);
        NetLog.step(TAG, "Online mode: " + mode
                + " | server=" + server
                + " | roam=" + roam
                + " | redirector_port=" + PORT_REDIRECTOR
                + " | lobby_port=" + PORT_LOBBY
                + " | buddy_port=" + PORT_BUDDY);
        Log.i("NFSU2", "Online startup [" + mode + "] server=" + server);
    }
}
