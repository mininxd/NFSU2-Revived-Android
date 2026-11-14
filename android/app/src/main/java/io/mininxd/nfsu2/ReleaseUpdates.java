package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import javax.net.ssl.HttpsURLConnection;

/** Optional official GitHub updates supporting Stable (Latest) and Beta (Pre-release) channels. */
final class ReleaseUpdates {
    private static final String REPO = "https://github.com/mininxd/NFSU2-Revived-Android/";
    private static final String API_LATEST = "https://api.github.com/repos/mininxd/NFSU2-Revived-Android/releases/latest";
    private static final String API_RELEASES = "https://api.github.com/repos/mininxd/NFSU2-Revived-Android/releases?per_page=15";

    static final class Result {
        final String tag, url, error;
        final boolean isPrerelease;
        Result(String tag, String url, String error) {
            this(tag, url, false, error);
        }
        Result(String tag, String url, boolean isPrerelease, String error) {
            this.tag = tag;
            this.url = url;
            this.isPrerelease = isPrerelease;
            this.error = error;
        }
    }

    static SharedPreferences prefs(Context context) { return context.getSharedPreferences("nfsu2_updates", 0); }

    static void clearCache(Context context) {
        prefs(context).edit().remove("last_check").remove("latest_tag").remove("latest_url").remove("last_channel").apply();
    }

    static String installed(Context context) {
        try { return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName; }
        catch (Exception ignored) { return "0.0.0"; }
    }

    static void check(Context context, boolean force, Consumer<Result> callback) {
        Context app = context.getApplicationContext();
        Handler main = new Handler(Looper.getMainLooper());
        Thread worker = new Thread(() -> {
            Result result;
            try { result = fetch(app, force); }
            catch (Exception error) { result = new Result(null, null, "Could not query GitHub. Check your connection and try again later."); }
            Result value = result;
            main.post(() -> callback.accept(value));
        }, "GitHubReleaseCheck");
        worker.setDaemon(true); worker.start();
    }

    /**
     * The version whose "what's new" notice the player has not closed yet, or null. An update is
     * detected when versionCode grew since the last launch; a fresh install only records the
     * version, and the first launch of a build that predates this record counts as an update when
     * Android says the package was updated after its install. The notice stays pending until
     * {@link #whatsNewSeen} (so a rotation that recreates the launcher shows it again).
     */
    static String pendingWhatsNew(Context context) {
        android.content.pm.PackageInfo info;
        try { info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0); }
        catch (Exception ignored) { return null; }
        long current = info.getLongVersionCode();
        SharedPreferences prefs = prefs(context);
        long previous = prefs.getLong("seen_version_code", 0);
        if (previous != current) {
            boolean updated = previous != 0 ? current > previous : info.lastUpdateTime > info.firstInstallTime;
            SharedPreferences.Editor edit = prefs.edit().putLong("seen_version_code", current);
            if (updated) edit.putString("whats_new", installed(context)); else edit.remove("whats_new");
            edit.apply();
        }
        String pending = prefs.getString("whats_new", "");
        return pending.isEmpty() ? null : pending;
    }

    static void whatsNewSeen(Context context) { prefs(context).edit().remove("whats_new").apply(); }

    /** Release notes of a version (GitHub release v{version}) as plain text, or null if unavailable. */
    static void releaseNotes(Context context, String version, Consumer<String> callback) {
        Handler main = new Handler(Looper.getMainLooper());
        Thread worker = new Thread(() -> {
            String notes = null;
            try {
                String tag = (version != null && version.startsWith("v")) ? version : ("v" + version);
                HttpsURLConnection connection = (HttpsURLConnection) new URL(
                        "https://api.github.com/repos/mininxd/NFSU2-Revived-Android/releases/tags/" + Uri.encode(tag)).openConnection();
                connection.setConnectTimeout(6000); connection.setReadTimeout(6000);
                connection.setRequestProperty("Accept", "application/vnd.github+json");
                connection.setRequestProperty("User-Agent", "NFSU2-Android/" + version);
                try {
                    if (connection.getResponseCode() == 200) {
                        ByteArrayOutputStream body = new ByteArrayOutputStream();
                        try (InputStream input = connection.getInputStream()) {
                            byte[] buffer = new byte[8192]; int n;
                            while ((n = input.read(buffer)) != -1 && body.size() < 512 * 1024) body.write(buffer, 0, n);
                        }
                        notes = plainText(new JSONObject(new String(body.toByteArray(), StandardCharsets.UTF_8)).optString("body"));
                    }
                } finally { connection.disconnect(); }
            } catch (Exception ignored) { }
            String value = notes;
            main.post(() -> callback.accept(value));
        }, "GitHubReleaseNotes");
        worker.setDaemon(true); worker.start();
    }

    /** Markdown of a release body to readable dialog text. */
    static String plainText(String markdown) {
        if (markdown == null || markdown.trim().isEmpty()) return null;
        return markdown.replace("\r", "")
                .replaceAll("(?m)^#{1,6}\\s*", "")
                .replaceAll("(?m)^>\\s?", "")
                .replaceAll("(?m)^(\\s*)[-*]\\s+", "$1• ")
                .replace("**", "").replace("`", "")
                .replaceAll("\\[([^\\]]+)\\]\\([^)]+\\)", "$1")
                .replaceAll("\n{3,}", "\n\n").trim();
    }

    private static Result fetch(Context context, boolean force) throws Exception {
        SharedPreferences prefs = prefs(context);
        String channel = GameOptions.updateChannel(context);
        boolean isBeta = GameOptions.isBetaChannel(context);
        long age = System.currentTimeMillis() - prefs.getLong("last_check", 0);
        String cachedChannel = prefs.getString("last_channel", "");
        if (!force && channel.equalsIgnoreCase(cachedChannel) && age >= 0 && age < TimeUnit.HOURS.toMillis(6)) {
            String tag = prefs.getString("latest_tag", "");
            String url = prefs.getString("latest_url", REPO + "releases");
            boolean isPre = prefs.getBoolean("latest_is_prerelease", false);
            return new Result(tag, url.startsWith(REPO + "releases") ? url : REPO + "releases", isPre, null);
        }

        String apiUrl = isBeta ? API_RELEASES : API_LATEST;
        HttpsURLConnection connection = (HttpsURLConnection) new URL(apiUrl).openConnection();
        connection.setConnectTimeout(6000); connection.setReadTimeout(6000);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("User-Agent", "NFSU2-Android/" + installed(context));
        connection.setRequestProperty("X-GitHub-Api-Version", "2026-03-10");
        try {
            int code = connection.getResponseCode();
            if (code == 404) {
                // No published release yet: nothing to offer, not a connection problem.
                prefs.edit().putLong("last_check", System.currentTimeMillis())
                        .putString("last_channel", channel)
                        .putString("latest_tag", "").putString("latest_url", REPO + "releases")
                        .putBoolean("latest_is_prerelease", false).apply();
                return new Result("", REPO + "releases", false, null);
            }
            if (code != 200) throw new IOException("GitHub unavailable (" + code + ")");
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) {
                byte[] buffer = new byte[8192]; int n;
                while ((n = input.read(buffer)) != -1) {
                    if (body.size() + n > 512 * 1024) throw new IOException("Response too large");
                    body.write(buffer, 0, n);
                }
            }
            String bodyStr = new String(body.toByteArray(), StandardCharsets.UTF_8).trim();
            JSONObject release = null;
            boolean isPrerelease = false;

            if (isBeta) {
                JSONArray releases = new JSONArray(bodyStr);
                JSONObject latestPre = null;
                JSONObject latestAny = null;
                for (int i = 0; i < releases.length(); i++) {
                    JSONObject rel = releases.getJSONObject(i);
                    if (rel.optBoolean("draft")) continue;
                    if (latestAny == null) latestAny = rel;
                    if (rel.optBoolean("prerelease") && latestPre == null) {
                        latestPre = rel;
                    }
                }
                if (latestPre != null && latestAny != null && !latestPre.optString("tag_name").equals(latestAny.optString("tag_name"))) {
                    // If latestAny is newer than latestPre, prefer latestAny; otherwise prefer latestPre
                    if (ReleaseVersion.newer(latestAny.optString("tag_name"), latestPre.optString("tag_name"))) {
                        release = latestAny;
                    } else {
                        release = latestPre;
                    }
                } else if (latestPre != null) {
                    release = latestPre;
                } else {
                    release = latestAny;
                }
                if (release != null) {
                    isPrerelease = release.optBoolean("prerelease");
                }
            } else {
                release = new JSONObject(bodyStr);
                if (release.optBoolean("draft") || release.optBoolean("prerelease")) {
                    release = null;
                }
            }

            if (release == null) {
                prefs.edit().putLong("last_check", System.currentTimeMillis())
                        .putString("last_channel", channel)
                        .putString("latest_tag", "").putString("latest_url", REPO + "releases")
                        .putBoolean("latest_is_prerelease", false).apply();
                return new Result("", REPO + "releases", false, null);
            }

            String tag = release.optString("tag_name", "");
            String url = REPO + "releases/tag/" + Uri.encode(tag);
            // Prefer this release's ARM64 APK. Source ZIPs and links outside our repository are ignored.
            JSONArray assets = release.optJSONArray("assets");
            if (assets != null) for (int i = 0; i < assets.length(); i++) {
                JSONObject asset = assets.getJSONObject(i);
                String name = asset.optString("name").toLowerCase(java.util.Locale.ROOT);
                String download = asset.optString("browser_download_url");
                if (name.endsWith(".apk") && download.startsWith(REPO + "releases/download/" + Uri.encode(tag) + "/")) {
                    url = download;
                    if (name.contains("arm64")) break;
                }
            }
            prefs.edit().putLong("last_check", System.currentTimeMillis())
                    .putString("last_channel", channel)
                    .putString("latest_tag", tag).putString("latest_url", url)
                    .putBoolean("latest_is_prerelease", isPrerelease).apply();
            return new Result(tag, url, isPrerelease, null);
        } finally { connection.disconnect(); }
    }
}
