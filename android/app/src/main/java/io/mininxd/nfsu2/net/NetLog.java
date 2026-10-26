package io.mininxd.nfsu2.net;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
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
 * High-resolution persistent network recorder for NFSU2 Android.
 * Records every connection attempt, handshake, datagram, fallback, and failure/success status.
 *
 * Log locations:
 * - Primary app file: /Android/data/io.mininxd.nfsu2/files/net_log.txt
 * - Internal fallback: /data/data/io.mininxd.nfsu2/files/net_log.txt
 * - Public export: /sdcard/NFSU2/net_log.txt & Downloads/NFSU2/net_log.txt
 */
public final class NetLog {
    private static final String TAG = "NFSU2-NetLog";
    private static final int MAX_MEMORY_LINES = 1000;
    private static final long MAX_FILE_SIZE = 5 * 1024 * 1024; // 5 MB

    private static volatile Context appContext = null;
    private static final Deque<String> memoryBuffer = new ArrayDeque<>(MAX_MEMORY_LINES);
    private static final ExecutorService fileWriterExecutor = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "NetLogWriter");
        t.setDaemon(true);
        return t;
    });

    private static final SimpleDateFormat DATE_FORMAT =
            new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);

    public static synchronized void init(Context context) {
        if (context == null) return;
        appContext = context.getApplicationContext();
        i("System", "=== Network Logging Initialized (PID " + android.os.Process.myPid()
                + ", Device: " + Build.MANUFACTURER + " " + Build.MODEL
                + ", Android " + Build.VERSION.RELEASE + ") ===");
    }

    public static void step(String tag, String msg) {
        log("STEP", tag, msg);
    }

    public static void success(String tag, String msg) {
        log("SUCCESS", tag, msg);
    }

    public static void fail(String tag, String msg) {
        log("FAIL", tag, msg);
    }

    public static void fail(String tag, String msg, Throwable tr) {
        if (tr != null) {
            StringWriter sw = new StringWriter();
            tr.printStackTrace(new PrintWriter(sw));
            log("FAIL", tag, msg + "\nException: " + sw.toString().trim());
        } else {
            log("FAIL", tag, msg);
        }
    }

    public static void i(String tag, String msg) {
        log("INFO", tag, msg);
    }

    public static void w(String tag, String msg) {
        log("WARN", tag, msg);
    }

    public static void e(String tag, String msg) {
        log("ERROR", tag, msg);
    }

    public static void e(String tag, String msg, Throwable tr) {
        if (tr != null) {
            StringWriter sw = new StringWriter();
            tr.printStackTrace(new PrintWriter(sw));
            log("ERROR", tag, msg + "\nException: " + sw.toString().trim());
        } else {
            log("ERROR", tag, msg);
        }
    }

    public static void log(String level, String tag, String msg) {
        String timestamp;
        synchronized (DATE_FORMAT) {
            timestamp = DATE_FORMAT.format(new Date());
        }
        String entry = "[" + timestamp + "] [" + level + "] [" + tag + "] " + msg;

        // Also output to logcat
        if ("ERROR".equals(level) || "FAIL".equals(level)) {
            Log.e(TAG, entry);
        } else if ("WARN".equals(level)) {
            Log.w(TAG, entry);
        } else {
            Log.i(TAG, entry);
        }

        // Buffer in memory
        synchronized (memoryBuffer) {
            if (memoryBuffer.size() >= MAX_MEMORY_LINES) {
                memoryBuffer.removeFirst();
            }
            memoryBuffer.addLast(entry);
        }

        // Write to file asynchronously
        final Context ctx = appContext;
        if (ctx != null) {
            fileWriterExecutor.execute(() -> writeToFile(ctx, entry));
        }
    }

    private static synchronized void writeToFile(Context ctx, String entry) {
        try {
            File logFile = getPrimaryLogFile(ctx);
            if (logFile == null) return;

            File parent = logFile.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }

            if (logFile.exists() && logFile.length() > MAX_FILE_SIZE) {
                File oldFile = new File(parent, logFile.getName() + ".old");
                if (oldFile.exists()) oldFile.delete();
                logFile.renameTo(oldFile);
            }

            try (FileWriter writer = new FileWriter(logFile, true)) {
                writer.write(entry);
                writer.write("\n");
                writer.flush();
            }

            // Sync copy to sdcard /sdcard/NFSU2/net_log.txt if available
            try {
                File sdcardDir = new File(Environment.getExternalStorageDirectory(), "NFSU2");
                if (sdcardDir.isDirectory() || sdcardDir.mkdirs()) {
                    File sdcardFile = new File(sdcardDir, "net_log.txt");
                    try (FileWriter writer = new FileWriter(sdcardFile, true)) {
                        writer.write(entry);
                        writer.write("\n");
                        writer.flush();
                    }
                }
            } catch (Throwable ignored) {}

        } catch (Throwable t) {
            Log.d(TAG, "writeToFile failed: " + t.getMessage());
        }
    }

    public static File getPrimaryLogFile(Context ctx) {
        if (ctx == null) return null;
        try {
            File ext = ctx.getExternalFilesDir(null);
            if (ext != null) {
                return new File(ext, "net_log.txt");
            }
        } catch (Throwable ignored) {}
        return new File(ctx.getFilesDir(), "net_log.txt");
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

        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            Deque<String> lines = new ArrayDeque<>(maxLines);
            String line;
            while ((line = reader.readLine()) != null) {
                if (lines.size() >= maxLines) {
                    lines.removeFirst();
                }
                lines.addLast(line);
            }
            result.addAll(lines);
        } catch (Throwable t) {
            synchronized (memoryBuffer) {
                result.addAll(memoryBuffer);
            }
        }
        return result;
    }

    public static String getFullLogText(Context ctx) {
        File file = getPrimaryLogFile(ctx);
        if (file == null || !file.exists()) {
            StringBuilder sb = new StringBuilder();
            synchronized (memoryBuffer) {
                for (String s : memoryBuffer) {
                    sb.append(s).append("\n");
                }
            }
            return sb.toString();
        }

        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line).append("\n");
            }
            return sb.toString();
        } catch (Throwable t) {
            return "Failed reading log file: " + t.getMessage();
        }
    }

    public static byte[] getLogBytes(Context ctx) {
        File file = getPrimaryLogFile(ctx);
        if (file != null && file.exists()) {
            try (FileInputStream in = new FileInputStream(file)) {
                byte[] data = new byte[(int) file.length()];
                int read = in.read(data);
                if (read == data.length) return data;
            } catch (Throwable ignored) {}
        }
        return getFullLogText(ctx).getBytes(StandardCharsets.UTF_8);
    }

    public static String exportLogToDownloads(Context ctx) {
        File source = getPrimaryLogFile(ctx);
        if (source == null || !source.exists()) {
            String mem = getFullLogText(ctx);
            if (mem == null || mem.trim().isEmpty()) {
                return null;
            }
            if (source != null) {
                try {
                    File p = source.getParentFile();
                    if (p != null && !p.exists()) p.mkdirs();
                    try (FileWriter fw = new FileWriter(source)) {
                        fw.write(mem);
                    }
                } catch (Throwable ignored) {}
            }
        }

        String exportedPath = null;
        // 1. Try MediaStore Downloads
        try {
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, "NFSU2-network.log");
            values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
            values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/NFSU2");
            ContentResolver resolver = ctx.getContentResolver();
            Uri target = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (target != null) {
                try (InputStream input = new FileInputStream(source);
                     OutputStream output = resolver.openOutputStream(target, "w")) {
                    if (output != null) {
                        byte[] buffer = new byte[8192];
                        int n;
                        while ((n = input.read(buffer)) != -1) {
                            output.write(buffer, 0, n);
                        }
                        exportedPath = "Downloads/NFSU2/NFSU2-network.log";
                    }
                }
            }
        } catch (Throwable ignored) {}

        // 2. Try public Downloads/NFSU2/
        try {
            File dl = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "NFSU2");
            if (!dl.isDirectory()) dl.mkdirs();
            File dest = new File(dl, "NFSU2-network.log");
            try (InputStream in = new FileInputStream(source);
                 FileOutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) != -1) {
                    out.write(buf, 0, n);
                }
                if (exportedPath == null) exportedPath = dest.getAbsolutePath();
            }
        } catch (Throwable ignored) {}

        // 3. Try /sdcard/NFSU2/
        try {
            File sdcardDir = new File(Environment.getExternalStorageDirectory(), "NFSU2");
            if (!sdcardDir.isDirectory()) sdcardDir.mkdirs();
            File dest = new File(sdcardDir, "NFSU2-network.log");
            try (InputStream in = new FileInputStream(source);
                 FileOutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) != -1) {
                    out.write(buf, 0, n);
                }
            }
        } catch (Throwable ignored) {}

        return exportedPath != null ? exportedPath : (source != null ? source.getAbsolutePath() : null);
    }

    public static void clear(Context ctx) {
        synchronized (memoryBuffer) {
            memoryBuffer.clear();
        }
        File file = getPrimaryLogFile(ctx);
        if (file != null && file.exists()) {
            file.delete();
        }
        try {
            File sdcardFile = new File(new File(Environment.getExternalStorageDirectory(), "NFSU2"), "net_log.txt");
            if (sdcardFile.exists()) sdcardFile.delete();
        } catch (Throwable ignored) {}
        i("System", "=== Network Log Cleared ===");
    }

    public static String hexPreview(byte[] data, int maxLen) {
        if (data == null) return "null";
        int len = Math.min(data.length, maxLen);
        StringBuilder sb = new StringBuilder(len * 3);
        for (int i = 0; i < len; i++) {
            sb.append(String.format("%02X ", data[i]));
        }
        if (data.length > maxLen) {
            sb.append("... (+").append(data.length - maxLen).append(" B)");
        }
        return sb.toString().trim();
    }

    public static String ipStr(int ip) {
        return String.format(Locale.ROOT, "%d.%d.%d.%d",
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    }
}
