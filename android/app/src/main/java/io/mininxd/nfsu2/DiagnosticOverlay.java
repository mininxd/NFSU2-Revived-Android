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
import android.view.Choreographer;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.PopupWindow;
import android.widget.TextView;

import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Diagnostic and performance telemetry overlay.
 * Supports "compact" (FPS, racers, CPU/RAM) and "full" (detailed metrics matching log_example.txt).
 */
public final class DiagnosticOverlay implements Choreographer.FrameCallback {
    private final Activity activity;
    private final String mode; // "compact" or "full"
    private final Handler handler = new Handler(Looper.getMainLooper());

    private PopupWindow window;
    private TextView textView;
    private boolean running = false;
    private boolean attached = false;

    // Frame rate measurement
    private long lastFrameTimeNanos = 0;
    private int frameCount = 0;
    private long lastFpsUpdateNanos = 0;
    private float currentFps = 0.0f;
    private float currentFrameTimeMs = 0.0f;

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
        textView.setTextColor(Color.parseColor("#EAEAEA"));
        textView.setTextSize(isFull() ? 11 : 12);

        int padH = dp(10);
        int padV = dp(6);
        textView.setPadding(padH, padV, padH, padV);

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.parseColor("#C80D0D0D")); // 78% opacity dark charcoal
        bg.setCornerRadius(dp(8));
        bg.setStroke(dp(1), Color.parseColor("#55333333"));
        textView.setBackground(bg);

        window = new PopupWindow(textView, ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, false);
        window.setTouchable(false); // Zero interference with game touch controls
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

        Choreographer.getInstance().postFrameCallback(this);
        handler.post(ticker);
    }

    public void stop() {
        if (!running) return;
        running = false;

        Choreographer.getInstance().removeFrameCallback(this);
        handler.removeCallbacks(ticker);

        hide();
    }

    public void show() {
        if (!isEnabled() || !running || attached) return;
        View decor = activity.getWindow().getDecorView();
        if (decor.isAttachedToWindow() && activity.hasWindowFocus()) {
            try {
                int marginX = dp(16);
                int marginY = dp(18);
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

    @Override
    public void doFrame(long frameTimeNanos) {
        if (!running) return;

        if (lastFrameTimeNanos > 0) {
            long delta = frameTimeNanos - lastFrameTimeNanos;
            currentFrameTimeMs = delta / 1_000_000.0f;
            frameCount++;

            if (frameTimeNanos - lastFpsUpdateNanos >= 1_000_000_000L) {
                currentFps = (frameCount * 1_000_000_000.0f) / (frameTimeNanos - lastFpsUpdateNanos);
                frameCount = 0;
                lastFpsUpdateNanos = frameTimeNanos;
            }
        } else {
            lastFpsUpdateNanos = frameTimeNanos;
        }

        lastFrameTimeNanos = frameTimeNanos;
        Choreographer.getInstance().postFrameCallback(this);
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

        // Online & Racers
        List<String> racers = new ArrayList<>();
        String myName = GameOptions.roamName(activity);
        if (myName != null && !myName.trim().isEmpty()) {
            racers.add(myName.trim());
        } else {
            racers.add("Racer");
        }

        boolean roamActive = GameOptions.isRoamEnabled(activity);
        if (roamActive) {
            for (AdhocRoamClient.Peer peer : AdhocRoamClient.getInstance().getPeers().values()) {
                if (!racers.contains(peer.name)) {
                    racers.add(peer.name);
                }
            }
        }

        int onlineCount = roamActive ? racers.size() : 1;
        int onRaceCount = racers.size();

        int fpsInt = Math.round(currentFps > 0 ? currentFps : (currentFrameTimeMs > 0 ? (1000.0f / currentFrameTimeMs) : 0));
        if (fpsInt > 300) fpsInt = 0; // sanity clamp on startup

        if (isFull()) {
            StringBuilder sb = new StringBuilder();
            sb.append("fps: ").append(fpsInt).append('\n');
            sb.append("player_online: ").append(onlineCount).append('\n');
            sb.append("player_on_race: ").append(onRaceCount).append('\n');
            sb.append("player_racing: [");
            for (int i = 0; i < racers.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(racers.get(i));
            }
            sb.append("]\n");
            sb.append(String.format(Locale.US, "cpu_usage: %.1f%%\n", cpu));
            sb.append("gpu_usage: ").append(gpuDriverName).append('\n');
            sb.append("system: ").append(gpuDriverName).append(", ").append(cpuModel).append(", ").append(systemPlatform).append('\n');
            sb.append("memory: ").append(usedMb).append(" MB / ").append(totalMb).append(" MB");
            textView.setText(sb.toString());
        } else {
            // Compact Mode: sleek 2-line HUD
            String text = String.format(Locale.US,
                    "FPS: %d (%.1f ms)  |  CPU: %.0f%%  |  RAM: %.1f/%.1f GB\nPlayers: %d %s",
                    fpsInt, currentFrameTimeMs, cpu,
                    usedMb / 1024.0f, totalMb / 1024.0f,
                    onlineCount, racers.toString());
            textView.setText(text);
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
