package io.mininxd.nfsu2;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import android.os.Process;
import android.util.Log;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Date;
import java.util.Deque;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Dedicated persistent game & halt recorder for NFSU2 Android.
 * Records game boots, halts, exceptions, graphics API switches, and captures system logcat tails.
 *
 * Log locations:
 * - Primary app file: /Android/data/io.mininxd.nfsu2/files/game_log.txt
 * - Internal fallback: /data/data/io.mininxd.nfsu2/files/game_log.txt
 * - SDCard: /sdcard/NFSU2/game_log.txt & /sdcard/NFSU2/halt_log.txt
 */
public final class GameLog {
    private static final String TAG = "NFSU2-GameLog";
    private static final int MAX_MEMORY_LINES = 1000;
    private static final long MAX_FILE_SIZE = 5 * 1024 * 1024; // 5 MB

    private static volatile Context appContext = null;
    private static final Deque<String> memoryBuffer = new ArrayDeque<>(MAX_MEMORY_LINES);
    private static final ExecutorService writerExecutor = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "GameLogWriter");
        t.setDaemon(true);
        return t;
    });

    private static final SimpleDateFormat DATE_FORMAT =
            new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);

    public static synchronized void init(Context context) {
        if (context == null) return;
        appContext = context.getApplicationContext();
        i("System", "=== Game Logger Initialized (PID " + Process.myPid()
                + ", Device: " + Build.MANUFACTURER + " " + Build.MODEL
                + ", Android " + Build.VERSION.RELEASE + ") ===");
    }

    public static void i(String tag, String msg) { log("INFO", tag, msg); }
    public static void w(String tag, String msg) { log("WARN", tag, msg); }
    public static void e(String tag, String msg) { log("ERROR", tag, msg); }

    public static void log(String level, String tag, String msg) {
        String timestamp = DATE_FORMAT.format(new Date());
        String line = String.format(Locale.US, "[%s] [%s/%s]: %s", timestamp, level, tag, msg);

        synchronized (memoryBuffer) {
            if (memoryBuffer.size() >= MAX_MEMORY_LINES) {
                memoryBuffer.pollFirst();
            }
            memoryBuffer.addLast(line);
        }

        Log.i(TAG, "[" + level + "/" + tag + "] " + msg);

        final Context ctx = appContext;
        writerExecutor.execute(() -> writeToFile(ctx, line));
    }

    /**
     * Triggered every time a halt or crash message is shown on screen.
     * Records timestamp, halt message, configuration, and appends the recent Logcat buffer.
     */
    public static void logHalt(Context context, String haltMessage) {
        if (context != null && appContext == null) {
            appContext = context.getApplicationContext();
        }
        String timestamp = DATE_FORMAT.format(new Date());
        StringBuilder sb = new StringBuilder(2048);
        sb.append("\n==================== GAME BOOT HALTED ====================\n");
        sb.append("Timestamp: ").append(timestamp).append("\n");
        sb.append("Message: ").append(haltMessage).append("\n");
        sb.append("Device: ").append(Build.MANUFACTURER).append(" ").append(Build.MODEL).append("\n");
        sb.append("Android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        sb.append("SoC: ").append(Build.VERSION.SDK_INT >= 31 ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL : Build.HARDWARE).append("\n");
        sb.append("ABI: ").append(Process.is64Bit() ? "arm64-v8a" : "armeabi-v7a").append("\n");

        if (context != null) {
            try {
                String selectedBackend = GameOptions.get(context, GameOptions.GRAPHICS_BACKEND);
                String effectiveBackend = GameOptions.getEffectiveGraphicsBackend(context);
                String backendLog = "auto".equalsIgnoreCase(selectedBackend)
                        ? "auto (" + effectiveBackend + ")"
                        : selectedBackend;
                sb.append("Graphics API: ").append(backendLog).append("\n");
                GpuDrivers.Driver driver = GpuDrivers.selected(context);
                sb.append("GPU Driver: ").append(driver == null ? "System default" : driver.name).append("\n");
                sb.append("Resolution: ").append(GameOptions.get(context, GameOptions.RESOLUTION)).append("%\n");
                sb.append("Widescreen: ").append(GameOptions.get(context, GameOptions.WIDESCREEN)).append("\n");
                sb.append("Latency Sync: ").append(GameOptions.get(context, GameOptions.LATENCY)).append("\n");
                sb.append("Textures: ").append(GameOptions.get(context, GameOptions.TEXTURES)).append("\n");
            } catch (Throwable ignored) {}
        }

        sb.append("\n--- Logcat Tail (Recent NFSU2 Logs) ---\n");
        try {
            java.lang.Process p = Runtime.getRuntime().exec(new String[]{"logcat", "-d", "-v", "time", "-t", "200", "NFSU2:*", "*:W"});
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    sb.append(line).append("\n");
                }
            }
        } catch (Throwable t) {
            sb.append("Could not capture logcat: ").append(t.getMessage()).append("\n");
        }
        sb.append("==========================================================\n\n");

        final String haltReport = sb.toString();
        log("HALT", "Status", haltMessage);

        final Context ctx = appContext != null ? appContext : context;
        writerExecutor.execute(() -> {
            writeToFile(ctx, haltReport);
            writeHaltFile(haltReport);
        });
    }

    private static void writeToFile(Context ctx, String entry) {
        try {
            File logFile = getPrimaryLogFile(ctx);
            if (logFile == null) return;

            File parent = logFile.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();

            if (logFile.exists() && logFile.length() > MAX_FILE_SIZE) {
                File oldFile = new File(logFile.getAbsolutePath() + ".old");
                if (oldFile.exists()) oldFile.delete();
                logFile.renameTo(oldFile);
            }

            try (FileWriter writer = new FileWriter(logFile, true)) {
                writer.write(entry);
                writer.write("\n");
                writer.flush();
            }

            // Sync copy to /sdcard/NFSU2/game_log.txt and /sdcard/nfsu2/game_log.txt
            for (String folder : new String[]{"NFSU2", "nfsu2"}) {
                try {
                    File sdcardDir = new File(Environment.getExternalStorageDirectory(), folder);
                    if (sdcardDir.isDirectory() || sdcardDir.mkdirs()) {
                        File sdcardFile = new File(sdcardDir, "game_log.txt");
                        try (FileWriter writer = new FileWriter(sdcardFile, true)) {
                            writer.write(entry);
                            writer.write("\n");
                            writer.flush();
                        }
                    }
                } catch (Throwable ignored) {}
            }
        } catch (Throwable t) {
            Log.d(TAG, "writeToFile failed: " + t.getMessage());
        }
    }

    private static void writeHaltFile(String report) {
        for (String folder : new String[]{"NFSU2", "nfsu2"}) {
            try {
                File sdcardDir = new File(Environment.getExternalStorageDirectory(), folder);
                if (sdcardDir.isDirectory() || sdcardDir.mkdirs()) {
                    File haltFile = new File(sdcardDir, "halt_log.txt");
                    try (FileWriter writer = new FileWriter(haltFile, true)) {
                        writer.write(report);
                        writer.flush();
                    }
                }
            } catch (Throwable ignored) {}
        }
    }

    public static File getPrimaryLogFile(Context ctx) {
        if (ctx == null) return null;
        try {
            File ext = ctx.getExternalFilesDir(null);
            if (ext != null) {
                return new File(ext, "game_log.txt");
            }
        } catch (Throwable ignored) {}
        return new File(ctx.getFilesDir(), "game_log.txt");
    }

    public static List<String> getRecentLogLines(Context ctx, int maxLines) {
        List<String> result = new ArrayList<>();
        File file = getPrimaryLogFile(ctx);
        if (file == null || !file.exists()) {
            synchronized (memoryBuffer) {
                result.addAll(memoryBuffer);
            }
            return result;
        }

        try (BufferedReader reader = new BufferedReader(new java.io.FileReader(file))) {
            Deque<String> tail = new ArrayDeque<>(maxLines);
            String line;
            while ((line = reader.readLine()) != null) {
                if (tail.size() >= maxLines) tail.pollFirst();
                tail.addLast(line);
            }
            result.addAll(tail);
        } catch (Throwable t) {
            synchronized (memoryBuffer) {
                result.addAll(memoryBuffer);
            }
        }
        return result;
    }

    public static String exportLog(Context ctx) {
        File source = getPrimaryLogFile(ctx);
        if (source == null || !source.exists()) {
            for (String folder : new String[]{"NFSU2", "nfsu2"}) {
                File alt = new File(new File(Environment.getExternalStorageDirectory(), folder), "game_log.txt");
                if (alt.exists()) { source = alt; break; }
            }
        }
        if (source == null || !source.exists()) return null;

        String exportedPath = null;
        // 1. Try public Download folder
        try {
            File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            if (downloadDir != null && (downloadDir.isDirectory() || downloadDir.mkdirs())) {
                File dest = new File(downloadDir, "NFSU2-game.log");
                try (InputStream in = new FileInputStream(source);
                     FileOutputStream out = new FileOutputStream(dest)) {
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) != -1) {
                        out.write(buf, 0, n);
                    }
                }
                exportedPath = dest.getAbsolutePath();
            }
        } catch (Throwable ignored) {}

        // 2. Try /sdcard/NFSU2/
        for (String folder : new String[]{"NFSU2", "nfsu2"}) {
            try {
                File sdcardDir = new File(Environment.getExternalStorageDirectory(), folder);
                if (sdcardDir.isDirectory() || sdcardDir.mkdirs()) {
                    File dest = new File(sdcardDir, "NFSU2-game.log");
                    try (InputStream in = new FileInputStream(source);
                         FileOutputStream out = new FileOutputStream(dest)) {
                        byte[] buf = new byte[8192];
                        int n;
                        while ((n = in.read(buf)) != -1) {
                            out.write(buf, 0, n);
                        }
                    }
                    if (exportedPath == null) exportedPath = dest.getAbsolutePath();
                }
            } catch (Throwable ignored) {}
        }

        return exportedPath != null ? exportedPath : (source != null ? source.getAbsolutePath() : null);
    }

    public static String exportLogToDownloads(Context ctx) {
        return exportLog(ctx);
    }

    public static String getFullLogText(Context ctx) {
        File file = getPrimaryLogFile(ctx);
        if (file == null || !file.exists()) {
            for (String folder : new String[]{"NFSU2", "nfsu2"}) {
                File alt = new File(new File(Environment.getExternalStorageDirectory(), folder), "game_log.txt");
                if (alt.exists()) { file = alt; break; }
            }
        }
        if (file == null || !file.exists()) {
            StringBuilder sb = new StringBuilder();
            synchronized (memoryBuffer) {
                for (String s : memoryBuffer) {
                    sb.append(s).append("\n");
                }
            }
            return sb.toString();
        }

        try (BufferedReader reader = new BufferedReader(new InputStreamReader(new FileInputStream(file), java.nio.charset.StandardCharsets.UTF_8))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line).append("\n");
            }
            return sb.toString();
        } catch (Throwable t) {
            return "Failed reading game log: " + t.getMessage();
        }
    }

    public static byte[] getLogBytes(Context ctx) {
        String full = getFullLogText(ctx);
        return full != null ? full.getBytes(java.nio.charset.StandardCharsets.UTF_8) : new byte[0];
    }

    public static void clear(Context ctx) {
        synchronized (memoryBuffer) {
            memoryBuffer.clear();
        }
        File file = getPrimaryLogFile(ctx);
        if (file != null && file.exists()) {
            file.delete();
        }
        for (String folder : new String[]{"NFSU2", "nfsu2"}) {
            try {
                File sdcardDir = new File(Environment.getExternalStorageDirectory(), folder);
                File gLog = new File(sdcardDir, "game_log.txt");
                if (gLog.exists()) gLog.delete();
                File hLog = new File(sdcardDir, "halt_log.txt");
                if (hLog.exists()) hLog.delete();
                File eLog = new File(sdcardDir, "NFSU2-game.log");
                if (eLog.exists()) eLog.delete();
            } catch (Throwable ignored) {}
        }
        i("System", "=== Game Log Cleared ===");
    }
}
