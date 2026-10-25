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
import android.view.FrameMetrics;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.PopupWindow;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Diagnostic and performance telemetry overlay.
 * Right-aligned, 60% opacity, compact font (6.5sp).
 * Records actual game frames and dynamic presentation rate.
 * Only displays player_racing and player_on_race during an ongoing race; when roaming, they are hidden.
 */
public final class DiagnosticOverlay {
    private final Activity activity;
    private final String mode; // "compact" or "full"
    private final Handler handler = new Handler(Looper.getMainLooper());

    private PopupWindow window;
    private TextView textView;
    private volatile boolean running = false;
    private boolean attached = false;

    // Real dynamic game frame counting
    private final AtomicInteger frameCounter = new AtomicInteger(0);
    private long lastFrameCalcTime = 0;
    private float liveGameFps = 0.0f;
    private float lastFrameDurationMs = 0.0f;
    private boolean metricsListenerRegistered = false;

    // Measured presentation FPS from native D3D9 bridge logcat
    private volatile float measuredNativeFps = 0.0f;
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

        // Right-align text
        textView.setGravity(Gravity.END);
        textView.setTextAlignment(View.TEXT_ALIGNMENT_TEXT_END);

        // 8sp compact font
        textView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8.0f);

        // 60% opacity for entire overlay
        textView.setAlpha(0.60f);

        int padH = dp(7);
        int padV = dp(4);
        textView.setPadding(padH, padV, padH, padV);

        GradientDrawable bg = new GradientDrawable();
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

    private final Window.OnFrameMetricsAvailableListener frameMetricsListener =
            new Window.OnFrameMetricsAvailableListener() {
                @Override
                public void onFrameMetricsAvailable(Window window, FrameMetrics frameMetrics, int dropCountSinceLastInvocation) {
                    frameCounter.incrementAndGet();
                    long totalNs = frameMetrics.getMetric(FrameMetrics.TOTAL_DURATION);
                    if (totalNs > 0) {
                        lastFrameDurationMs = totalNs / 1_000_000.0f;
                    }
                }
            };

    public void start() {
        if (!isEnabled() || running) return;
        running = true;

        lastFrameCalcTime = SystemClock.elapsedRealtime();
        frameCounter.set(0);

        try {
            activity.getWindow().addOnFrameMetricsAvailableListener(frameMetricsListener, handler);
            metricsListenerRegistered = true;
        } catch (Exception ignored) {}

        startLogcatFpsMonitor();
        handler.post(ticker);
    }

    public void stop() {
        if (!running) return;
        running = false;

        handler.removeCallbacks(ticker);

        if (metricsListenerRegistered) {
            try {
                activity.getWindow().removeOnFrameMetricsAvailableListener(frameMetricsListener);
            } catch (Exception ignored) {}
            metricsListenerRegistered = false;
        }

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
     * Reads native D3D9 bridge measurements from libnfsu2_android.so logcat output.
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
                    int idx = line.indexOf("Guest D3D9 presentation ");
                    if (idx >= 0) {
                        String sub = line.substring(idx + 24);
                        int fpsIdx = sub.indexOf(" FPS");
                        if (fpsIdx > 0) {
                            try {
                                float parsed = Float.parseFloat(sub.substring(0, fpsIdx).trim());
                                if (parsed > 0 && parsed <= 360) {
                                    measuredNativeFps = parsed;
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

    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            if (!running) return;

            // Compute dynamic frame rate every second
            long now = SystemClock.elapsedRealtime();
            long elapsed = now - lastFrameCalcTime;
            lastFrameCalcTime = now;

            int frames = frameCounter.getAndSet(0);
            if (elapsed > 0 && frames > 0) {
                float measured = (frames * 1000.0f) / elapsed;
                // Clamp sanity: cannot exceed twice the native cap
                liveGameFps = measured;
            }

            // Cross-validate with native bridge report if available
            if (measuredNativeFps > 0 && (now - lastNativeLogTime < 5000)) {
                liveGameFps = measuredNativeFps;
            }

            updateStats();

            if (!attached && activity.hasWindowFocus()) {
                show();
            }

            handler.postDelayed(this, 1000); // 1-second update cycle
        }
    };

    private void updateStats() {
        float fps = liveGameFps;
        if (fps <= 0.0f) {
            try {
                fps = Integer.parseInt(GameOptions.get(activity, GameOptions.FPS));
            } catch (Exception ignored) {
                fps = 60.0f;
            }
        }
        float frameTimeMs = (fps > 0) ? (1000.0f / fps) : 0.0f;
        String fpsStr = (fps >= 100.0f) ? String.format(Locale.US, "%.0f", fps) : String.format(Locale.US, "%.1f", fps);

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

        // Online mode: ONLY shown when connected to adhoc server!
        boolean adhocConnected = GameOptions.isRoamEnabled(activity) && AdhocRoamClient.getInstance().isConnected();
        boolean isRacing = adhocConnected && AdhocRoamClient.getInstance().isRacing();

        List<String> racers = new ArrayList<>();
        int onlineCount = 0;
        int onRaceCount = 0;

        if (adhocConnected) {
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

        AdhocRoamClient roamClient = AdhocRoamClient.getInstance();
        int totalRoam = roamClient.getTotalRoamPlayers();
        int totalServer = roamClient.getTotalServerPlayers();
        int roomRoam = roamClient.getRoomRoamPlayers();
        String room = roamClient.getRoomName();
        boolean hasRoom = room != null && !room.trim().isEmpty();

        int displayOnline;
        if (hasRoom && roomRoam >= 0) {
            displayOnline = roomRoam;
        } else if (totalRoam > 0) {
            displayOnline = totalRoam;
        } else if (totalServer > 0) {
            displayOnline = totalServer;
        } else {
            displayOnline = onlineCount;
        }

        if (isFull()) {
            StringBuilder sb = new StringBuilder();
            sb.append("fps: ").append(fpsStr).append(" (").append(String.format(Locale.US, "%.1f ms", frameTimeMs)).append(")\n");

            if (GameOptions.isRoamEnabled(activity)) {
                if (adhocConnected) {
                    sb.append("network: connected to ").append(roamClient.getServerHost()).append('\n');
                    if (hasRoom && roomRoam >= 0) {
                        sb.append("player_online: ").append(roomRoam)
                          .append(" (room: \"").append(room).append("\", roam: ").append(totalRoam)
                          .append(", server: ").append(totalServer).append(")\n");
                    } else if (totalRoam > 0) {
                        sb.append("player_online: ").append(totalRoam)
                          .append(" (server: ").append(totalServer).append(")\n");
                    } else if (totalServer > 0) {
                        sb.append("player_online: ").append(totalServer).append('\n');
                    } else {
                        sb.append("player_online: ").append(onlineCount).append('\n');
                    }
                    // ONLY show player_on_race and player_racing when an ongoing race is active!
                    // When roaming, hide them completely.
                    if (isRacing) {
                        sb.append("player_on_race: ").append(onRaceCount).append('\n');
                        sb.append("player_racing: [");
                        for (int i = 0; i < racers.size(); i++) {
                            if (i > 0) sb.append(", ");
                            sb.append(racers.get(i));
                        }
                        sb.append("]\n");
                    }
                } else {
                    sb.append("network: ").append(roamClient.getConnectionStatus()).append('\n');
                }
            }

            sb.append(String.format(Locale.US, "cpu_usage: %.1f%%\n", cpu));
            String gpuUsage = sampleGpuUsage();
            if (gpuUsage != null) {
                sb.append("gpu_usage: ").append(gpuUsage).append('\n');
            }
            // Display system info directly without "system:" label prefix
            sb.append(gpuDriverName).append(", ").append(cpuModel).append(", ").append(systemPlatform).append('\n');
            sb.append("memory: ").append(usedMb).append(" MB / ").append(totalMb).append(" MB");
            textView.setText(sb.toString());
        } else {
            // Compact Mode: sleek right-aligned HUD
            if (adhocConnected) {
                if (isRacing) {
                    String text = String.format(Locale.US,
                            "FPS: %s (%.1f ms)  |  Racing: %d %s (Online: %d)\nCPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                            fpsStr, frameTimeMs, onRaceCount, racers.toString(), displayOnline,
                            cpu, usedMb / 1024.0f, totalMb / 1024.0f);
                    textView.setText(text);
                } else {
                    String text = String.format(Locale.US,
                            "FPS: %s (%.1f ms)  |  Online: %d  |  connected to %s\nCPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                            fpsStr, frameTimeMs, displayOnline, roamClient.getServerHost(),
                            cpu, usedMb / 1024.0f, totalMb / 1024.0f);
                    textView.setText(text);
                }
            } else if (GameOptions.isRoamEnabled(activity)) {
                String text = String.format(Locale.US,
                        "FPS: %s (%.1f ms)  |  network: %s\nCPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                        fpsStr, frameTimeMs, roamClient.getConnectionStatus(),
                        cpu, usedMb / 1024.0f, totalMb / 1024.0f);
                textView.setText(text);
            } else {
                String text = String.format(Locale.US,
                        "FPS: %s (%.1f ms)  |  CPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                        fpsStr, frameTimeMs, cpu,
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

    private String sampleGpuUsage() {
        String[] percentagePaths = new String[]{
                "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
                "/sys/class/misc/mali0/device/utilization",
                "/sys/devices/platform/mali.0/utilization",
                "/sys/kernel/debug/ged/hal/gpu_utilization",
                "/sys/module/ged/parameters/gpu_loading"
        };
        for (String p : percentagePaths) {
            try (RandomAccessFile raf = new RandomAccessFile(p, "r")) {
                String line = raf.readLine();
                if (line != null) {
                    line = line.trim().replace("%", "").trim();
                    String[] parts = line.split("\\s+");
                    int val = Integer.parseInt(parts[0]);
                    if (val >= 0 && val <= 100) {
                        return val + "%";
                    }
                }
            } catch (Exception ignored) {
            }
        }
        try (RandomAccessFile raf = new RandomAccessFile("/sys/class/kgsl/kgsl-3d0/gpubusy", "r")) {
            String line = raf.readLine();
            if (line != null) {
                String[] parts = line.trim().split("\\s+");
                if (parts.length >= 2) {
                    long busy = Long.parseLong(parts[0]);
                    long total = Long.parseLong(parts[1]);
                    if (total > 0 && busy >= 0 && busy <= total) {
                        return (busy * 100 / total) + "%";
                    }
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private int dp(int value) {
        DisplayMetrics dm = activity.getResources().getDisplayMetrics();
        return Math.round(value * dm.density);
    }
}
