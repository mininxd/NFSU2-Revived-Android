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
import java.util.Locale;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import javax.net.ssl.HttpsURLConnection;

/** Optional official GitHub updates supporting Stable and Beta channels. */
final class ReleaseUpdates {
    private static final String REPO = "https://github.com/mininxd/NFSU2-Revived-Android/";
    private static final String API_RELEASES = "https://api.github.com/repos/mininxd/NFSU2-Revived-Android/releases?per_page=15";
    private static final String API_LATEST = "https://api.github.com/repos/mininxd/NFSU2-Revived-Android/releases/latest";

    static final String CHANNEL_KEY = "update_channel";
    static final String CHANNEL_STABLE = "stable";
    static final String CHANNEL_BETA = "beta";

    static final class Result {
        final String tag, url, error;
        final boolean isPrerelease;
        Result(String tag, String url, String error) { this(tag, url, error, false); }
        Result(String tag, String url, String error, boolean isPrerelease) {
            this.tag = tag; this.url = url; this.error = error; this.isPrerelease = isPrerelease;
        }
    }

    static SharedPreferences prefs(Context context) { return context.getSharedPreferences("nfsu2_updates", 0); }

    static String getChannel(Context context) {
        return prefs(context).getString(CHANNEL_KEY, CHANNEL_STABLE);
    }

    static void setChannel(Context context, String channel) {
        prefs(context).edit().putString(CHANNEL_KEY, channel).apply();
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
            catch (Exception error) {
                result = new Result(null, null, "Could not query GitHub. Check your connection and try again later.");
            }
            Result value = result;
            main.post(() -> callback.accept(value));
        }, "GitHubReleaseCheck");
        worker.setDaemon(true); worker.start();
    }

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

    static void releaseNotes(Context context, String version, Consumer<String> callback) {
        Handler main = new Handler(Looper.getMainLooper());
        Thread worker = new Thread(() -> {
            String notes = null;
            try {
                HttpsURLConnection connection = (HttpsURLConnection) new URL(
                        "https://api.github.com/repos/mininxd/NFSU2-Revived-Android/releases/tags/v" + Uri.encode(version)).openConnection();
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
        String channel = getChannel(context);
        boolean isBeta = CHANNEL_BETA.equalsIgnoreCase(channel);
        String cacheKeyTag = "latest_tag_" + channel;
        String cacheKeyUrl = "latest_url_" + channel;
        String cacheKeyPre = "latest_pre_" + channel;
        long age = System.currentTimeMillis() - prefs.getLong("last_check_" + channel, 0);
        if (!force && age >= 0 && age < TimeUnit.HOURS.toMillis(6)) {
            String tag = prefs.getString(cacheKeyTag, "");
            String url = prefs.getString(cacheKeyUrl, REPO + "releases");
            boolean pre = prefs.getBoolean(cacheKeyPre, false);
            return new Result(tag, url, null, pre);
        }

        // Beta queries the full releases list to discover pre-releases and new tags.
        // Stable queries the releases list (or latest) and filters for non-prerelease tags.
        String apiUrl = API_RELEASES;
        HttpsURLConnection connection = (HttpsURLConnection) new URL(apiUrl).openConnection();
        connection.setConnectTimeout(6000); connection.setReadTimeout(6000);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("User-Agent", "NFSU2-Android/" + installed(context));
        connection.setRequestProperty("X-GitHub-Api-Version", "2026-03-10");
        try {
            int code = connection.getResponseCode();
            if (code == 404) {
                prefs.edit().putLong("last_check_" + channel, System.currentTimeMillis())
                        .putString(cacheKeyTag, "").putString(cacheKeyUrl, REPO + "releases")
                        .putBoolean(cacheKeyPre, false).apply();
                return new Result("", REPO + "releases", null, false);
            }
            if (code != 200) throw new IOException("GitHub response " + code);
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) {
                byte[] buffer = new byte[8192]; int n;
                while ((n = input.read(buffer)) != -1) {
                    if (body.size() + n > 1024 * 1024) throw new IOException("Response too large");
                    body.write(buffer, 0, n);
                }
            }
            String rawJson = new String(body.toByteArray(), StandardCharsets.UTF_8).trim();
            JSONObject selectedRelease = null;

            if (rawJson.startsWith("[")) {
                JSONArray releases = new JSONArray(rawJson);
                for (int i = 0; i < releases.length(); i++) {
                    JSONObject r = releases.getJSONObject(i);
                    if (r.optBoolean("draft")) continue;
                    boolean isPre = r.optBoolean("prerelease");
                    if (!isBeta && isPre) continue;

                    String tag = r.optString("tag_name", "");
                    if (tag.isEmpty()) continue;

                    if (selectedRelease == null) {
                        selectedRelease = r;
                    } else {
                        String selectedTag = selectedRelease.optString("tag_name", "");
                        if (ReleaseVersion.newer(tag, selectedTag)) {
                            selectedRelease = r;
                        }
                    }
                }
            } else if (rawJson.startsWith("{")) {
                JSONObject r = new JSONObject(rawJson);
                if (!r.optBoolean("draft")) {
                    if (isBeta || !r.optBoolean("prerelease")) {
                        selectedRelease = r;
                    }
                }
            }

            if (selectedRelease == null) {
                prefs.edit().putLong("last_check_" + channel, System.currentTimeMillis())
                        .putString(cacheKeyTag, "").putString(cacheKeyUrl, REPO + "releases")
                        .putBoolean(cacheKeyPre, false).apply();
                return new Result("", REPO + "releases", null, false);
            }

            String tag = selectedRelease.optString("tag_name", "");
            boolean isPre = selectedRelease.optBoolean("prerelease", false);
            String url = REPO + "releases/tag/" + Uri.encode(tag);

            JSONArray assets = selectedRelease.optJSONArray("assets");
            if (assets != null) {
                for (int i = 0; i < assets.length(); i++) {
                    JSONObject asset = assets.getJSONObject(i);
                    String name = asset.optString("name").toLowerCase(Locale.ROOT);
                    String download = asset.optString("browser_download_url");
                    if (name.endsWith(".apk") && download.startsWith(REPO + "releases/download/" + Uri.encode(tag) + "/")) {
                        url = download;
                        if (name.contains("arm64")) break;
                    }
                }
            }

            prefs.edit().putLong("last_check_" + channel, System.currentTimeMillis())
                    .putString(cacheKeyTag, tag).putString(cacheKeyUrl, url)
                    .putBoolean(cacheKeyPre, isPre).apply();
            return new Result(tag, url, null, isPre);
        } finally { connection.disconnect(); }
    }
}
