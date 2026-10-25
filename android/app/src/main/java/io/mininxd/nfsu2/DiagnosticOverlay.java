package io.mininxd.nfsu2;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.PopupWindow;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Diagnostic and performance telemetry overlay.
 * Right-aligned, 60% opacity, compact font (6.5sp).
 * Measures actual game FPS (not display vsync rate).
 * Only displays online player information when online mode is active.
 */
public final class DiagnosticOverlay {
    private final Activity activity;
    private final String mode; // "compact" or "full"
    private final Handler handler = new Handler(Looper.getMainLooper());

    private PopupWindow window;
    private TextView textView;
    private volatile boolean running = false;
    private boolean attached = false;

    // Actual game FPS measured from native D3D9 presentation bridge
    private volatile float measuredGameFps = 0.0f;
    private volatile long lastNativeLogTime = 0;
    private Thread logcatThread;

    // CPU measurement state
    private long prevTotal = 0;
    private long prevIdle = 0;
    private float lastCpuPercent = 0.0f;
    private long lastWallTime = 0;
    private long lastProcessCpuTime = 0;

    // Cached hardware details
    private final String systemPlatform;
    private final String cpuModel;
    private final String gpuDriverName;

    public DiagnosticOverlay(Activity activity, String mode) {
        this.activity = activity;
        this.mode = mode != null ? mode.toLowerCase(Locale.ROOT) : "off";

        this.cpuModel = (Build.VERSION.SDK_INT >= 31
                ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL
                : Build.HARDWARE);
        this.systemPlatform = "Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + "), "
                + (Process.is64Bit() ? "arm64-v8a" : "armeabi-v7a");

        GpuDrivers.Driver driver = GpuDrivers.selected(activity);
        this.gpuDriverName = (driver == null ? "System Vulkan" : driver.name);

        initView();
    }

    private void initView() {
        textView = new TextView(activity);
        textView.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        textView.setTextColor(Color.WHITE);

        // Right align text
        textView.setGravity(Gravity.END);
        textView.setTextAlignment(View.TEXT_ALIGNMENT_TEXT_END);

        // Reduce font size to ~6.5sp (~6px scaled for device density)
        textView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 6.5f);

        // 60% Opacity
        textView.setAlpha(0.60f);

        int padH = dp(7);
        int padV = dp(4);
        textView.setPadding(padH, padV, padH, padV);

        GradientDrawable bg = new GradientDrawable();
        // 60% opacity dark background (#990A0A0A)
        bg.setColor(Color.argb((int)(0.60 * 255), 10, 10, 10));
        bg.setCornerRadius(dp(4));
        bg.setStroke(dp(1), Color.argb((int)(0.40 * 255), 70, 70, 70));
        textView.setBackground(bg);

        window = new PopupWindow(textView, ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, false);
        window.setTouchable(false); // Zero interference with game controls
        window.setFocusable(false);
    }

    public boolean isEnabled() {
        return "compact".equals(mode) || "full".equals(mode);
    }

    public boolean isFull() {
        return "full".equals(mode);
    }

    public void start() {
        if (!isEnabled() || running) return;
        running = true;

        startLogcatFpsMonitor();
        handler.post(ticker);
    }

    public void stop() {
        if (!running) return;
        running = false;

        handler.removeCallbacks(ticker);
        if (logcatThread != null) {
            try { logcatThread.interrupt(); } catch (Exception ignored) {}
            logcatThread = null;
        }

        hide();
    }

    public void show() {
        if (!isEnabled() || !running || attached) return;
        View decor = activity.getWindow().getDecorView();
        if (decor.isAttachedToWindow() && activity.hasWindowFocus()) {
            try {
                int marginX = dp(12);
                int marginY = dp(14);
                window.showAtLocation(decor, Gravity.TOP | Gravity.END, marginX, marginY);
                attached = true;
            } catch (Exception ignored) {}
        }
    }

    public void hide() {
        if (window != null && window.isShowing()) {
            try { window.dismiss(); } catch (Exception ignored) {}
        }
        attached = false;
    }

    /**
     * Reads the actual game rendering FPS directly from the native D3D9 bridge in libnfsu2_android.so.
     * Prevents reading Android display vsync rates (such as 120Hz displays when game is capped to 30 FPS).
     */
    private void startLogcatFpsMonitor() {
        logcatThread = new Thread(() -> {
            java.lang.Process process = null;
            try {
                process = new ProcessBuilder("logcat", "-v", "brief", "-s", "NFSU2:I")
                        .redirectErrorStream(true)
                        .start();
                BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
                String line;
                while (running && (line = reader.readLine()) != null) {
                    // Pattern: "Guest D3D9 presentation %.1f FPS over %.2f seconds..."
                    int idx = line.indexOf("Guest D3D9 presentation ");
                    if (idx >= 0) {
                        String sub = line.substring(idx + 24);
                        int fpsIdx = sub.indexOf(" FPS");
                        if (fpsIdx > 0) {
                            try {
                                float parsed = Float.parseFloat(sub.substring(0, fpsIdx).trim());
                                if (parsed > 0 && parsed <= 360) {
                                    measuredGameFps = parsed;
                                    lastNativeLogTime = SystemClock.elapsedRealtime();
                                }
                            } catch (Exception ignored) {}
                        }
                    }
                }
            } catch (Exception ignored) {
            } finally {
                if (process != null) {
                    try { process.destroy(); } catch (Exception ignored) {}
                }
            }
        }, "FpsLogcatMonitor");
        logcatThread.setDaemon(true);
        logcatThread.start();
    }

    private int getGameTargetFps() {
        try {
            int cap = Integer.parseInt(GameOptions.get(activity, GameOptions.FPS));
            if (cap > 0) return cap;
        } catch (Exception ignored) {}
        return 60; // default cap
    }

    private float getCurrentGameFps() {
        // If native D3D9 presentation logged recently (< 6 seconds ago), use it
        if (measuredGameFps > 0 && (SystemClock.elapsedRealtime() - lastNativeLogTime < 6000)) {
            return measuredGameFps;
        }
        // Otherwise, return configured game frame limit (e.g. 30 FPS, not 120Hz display rate)
        return getGameTargetFps();
    }

    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            if (!running) return;

            updateStats();

            if (!attached && activity.hasWindowFocus()) {
                show();
            }

            handler.postDelayed(this, 1000); // 1-second update cycle
        }
    };

    private void updateStats() {
        float gameFps = getCurrentGameFps();
        float frameTimeMs = gameFps > 0 ? (1000.0f / gameFps) : 0.0f;
        int fpsInt = Math.round(gameFps);

        float cpu = sampleCpuUsage();

        // Memory
        ActivityManager am = (ActivityManager) activity.getSystemService(Context.ACTIVITY_SERVICE);
        long usedMb = 0, totalMb = 0;
        if (am != null) {
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            usedMb = (mi.totalMem - mi.availMem) / (1024 * 1024);
            totalMb = mi.totalMem / (1024 * 1024);
        }

        // Online players is ONLY shown if entered online mode (Free Roam enabled)
        boolean onlineMode = GameOptions.isRoamEnabled(activity);

        List<String> racers = new ArrayList<>();
        int onlineCount = 0;
        int onRaceCount = 0;

        if (onlineMode) {
            String myName = GameOptions.roamName(activity);
            if (myName != null && !myName.trim().isEmpty()) {
                racers.add(myName.trim());
            } else {
                racers.add("Racer");
            }

            for (AdhocRoamClient.Peer peer : AdhocRoamClient.getInstance().getPeers().values()) {
                if (!racers.contains(peer.name)) {
                    racers.add(peer.name);
                }
            }
            onlineCount = racers.size();
            onRaceCount = racers.size();
        }

        if (isFull()) {
            StringBuilder sb = new StringBuilder();
            sb.append("fps: ").append(fpsInt).append(" (").append(String.format(Locale.US, "%.1f ms", frameTimeMs)).append(")\n");
            if (onlineMode) {
                sb.append("player_online: ").append(onlineCount).append('\n');
                sb.append("player_on_race: ").append(onRaceCount).append('\n');
                sb.append("player_racing: [");
                for (int i = 0; i < racers.size(); i++) {
                    if (i > 0) sb.append(", ");
                    sb.append(racers.get(i));
                }
                sb.append("]\n");
            }
            sb.append(String.format(Locale.US, "cpu_usage: %.1f%%\n", cpu));
            sb.append("gpu_usage: ").append(gpuDriverName).append('\n');
            sb.append("system: ").append(gpuDriverName).append(", ").append(cpuModel).append(", ").append(systemPlatform).append('\n');
            sb.append("memory: ").append(usedMb).append(" MB / ").append(totalMb).append(" MB");
            textView.setText(sb.toString());
        } else {
            // Compact Mode: sleek right-aligned HUD
            if (onlineMode) {
                String text = String.format(Locale.US,
                        "FPS: %d (%.1f ms)  |  CPU: %.0f%%  |  RAM: %.1f/%.1f GB\nOnline: %d %s",
                        fpsInt, frameTimeMs, cpu,
                        usedMb / 1024.0f, totalMb / 1024.0f,
                        onlineCount, racers.toString());
                textView.setText(text);
            } else {
                String text = String.format(Locale.US,
                        "FPS: %d (%.1f ms)  |  CPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                        fpsInt, frameTimeMs, cpu,
                        usedMb / 1024.0f, totalMb / 1024.0f);
                textView.setText(text);
            }
        }
    }

    private float sampleCpuUsage() {
        try (RandomAccessFile reader = new RandomAccessFile("/proc/stat", "r")) {
            String load = reader.readLine();
            if (load != null) {
                String[] toks = load.split(" +");
                if (toks.length >= 8) {
                    long user = Long.parseLong(toks[1]);
                    long nice = Long.parseLong(toks[2]);
                    long sys = Long.parseLong(toks[3]);
                    long idle = Long.parseLong(toks[4]);
                    long iowait = Long.parseLong(toks[5]);
                    long irq = Long.parseLong(toks[6]);
                    long softirq = Long.parseLong(toks[7]);

                    long total = user + nice + sys + idle + iowait + irq + softirq;
                    long work = user + nice + sys;

                    if (prevTotal > 0 && total > prevTotal) {
                        long totalDiff = total - prevTotal;
                        long workDiff = work - (prevTotal - prevIdle);
                        if (totalDiff > 0) {
                            lastCpuPercent = (workDiff * 100.0f) / totalDiff;
                        }
                    }
                    prevTotal = total;
                    prevIdle = idle;
                    if (lastCpuPercent >= 0.0f && lastCpuPercent <= 100.0f) {
                        return lastCpuPercent;
                    }
                }
            }
        } catch (Exception ignored) {}

        // Fallback: process CPU accounting
        long nowWall = SystemClock.elapsedRealtime();
        long nowCpu = Process.getElapsedCpuTime();
        if (lastWallTime > 0 && nowWall > lastWallTime) {
            long cpuDiff = nowCpu - lastProcessCpuTime;
            long wallDiff = nowWall - lastWallTime;
            int cores = Math.max(1, Runtime.getRuntime().availableProcessors());
            lastCpuPercent = (cpuDiff * 100.0f) / (wallDiff * cores);
        }
        lastWallTime = nowWall;
        lastProcessCpuTime = nowCpu;

        return Math.max(0.0f, Math.min(100.0f, lastCpuPercent));
    }

    private int dp(int value) {
        DisplayMetrics dm = activity.getResources().getDisplayMetrics();
        return Math.round(value * dm.density);
    }
}
