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
import java.io.File;
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
    private static volatile boolean graphicsCacheLoaded = false;

    static {
        try {
            System.loadLibrary("graphics_cache");
            graphicsCacheLoaded = true;
        } catch (Throwable t) {
            android.util.Log.w("NFSU2", "DiagnosticOverlay: libgraphics_cache load: " + t.getMessage());
        }
        try {
            System.loadLibrary("game_core");
        } catch (Throwable t) {
            android.util.Log.w("NFSU2", "DiagnosticOverlay: libgame_core load: " + t.getMessage());
        }
        try {
            System.loadLibrary("perf_cores");
        } catch (Throwable t) {
            android.util.Log.w("NFSU2", "DiagnosticOverlay: libperf_cores load: " + t.getMessage());
        }
    }

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
    private long lastNativeFrameCount = -1;
    private float liveGameFps = 0.0f;
    private float lastFrameDurationMs = 0.0f;
    private boolean metricsListenerRegistered = false;

    // Measured presentation FPS from native D3D9 bridge logcat
    private volatile float measuredNativeFps = 0.0f;
    private volatile long lastNativeLogTime = 0;
    private Thread logcatThread;

    // Native pure Rust telemetry buffer [fps, frame_time_ms, cpu_percent, gpu_busy]
    private final float[] telemetryBuffer = new float[4];
    private final float[] frameMetricsBuffer = new float[6];
    public static native boolean nativeGetTelemetry(float[] outArray);
    public static native void nativeRecordPresent(long frameDurationNs);
    public static native void nativeRecordFrameDuration(long durationNs);
    public static native boolean nativeGetFrameMetrics(float[] outArray);
    public static native String nativeFormatOverlayText(
            boolean isFull,
            float fps,
            float p1Low,
            float frameTimeMs,
            float cpuPercent,
            float gpuPercent,
            long usedMb,
            long totalMb,
            boolean roamEnabled,
            boolean adhocConnected,
            boolean isRacing,
            int onRaceCount,
            String racersCsv,
            int displayOnline,
            String serverHost,
            String roomName,
            int roomRoam,
            int totalRoam,
            int totalServer,
            String connStatus,
            String gpuDriver,
            String cpuModel,
            String systemPlatform
    );

    private static void safeRecordFrameDuration(long durationNs) {
        try {
            nativeRecordFrameDuration(durationNs);
        } catch (Throwable ignored) {}
    }

    private static boolean safeGetFrameMetrics(float[] outArray) {
        try {
            return nativeGetFrameMetrics(outArray);
        } catch (Throwable ignored) {
            return false;
        }
    }

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
    private volatile boolean suspended = false;

    public void setSuspended(boolean suspend) {
        this.suspended = suspend;
        if (suspend) {
            hide();
        } else if (isEnabled() && running && activity.hasWindowFocus()) {
            show();
        }
    }

    public boolean isSuspended() {
        return suspended;
    }

    public DiagnosticOverlay(Activity activity, String mode) {
        this.activity = activity;
        this.mode = mode != null ? mode.toLowerCase(Locale.ROOT) : "off";

        this.cpuModel = (Build.VERSION.SDK_INT >= 31
                ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL
                : Build.HARDWARE);
        this.systemPlatform = "Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + "), "
                + (Process.is64Bit() ? "arm64-v8a" : "armeabi-v7a");

        GpuDrivers.Driver driver = GpuDrivers.selected(activity);
        String backend = GameOptions.getEffectiveGraphicsBackend(activity);
        String apiLabel = "opengl".equals(backend) ? "OpenGL ES" : (driver == null ? "System Vulkan" : driver.name);
        this.gpuDriverName = apiLabel;

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

    private float lastGpuDurationNs = 0.0f;
    private float smoothedGpuPercent = -1.0f;
    private static File cachedKgslDir = null;
    private static boolean kgslDirChecked = false;

    private final Window.OnFrameMetricsAvailableListener frameMetricsListener =
            new Window.OnFrameMetricsAvailableListener() {
                @Override
                public void onFrameMetricsAvailable(Window window, FrameMetrics frameMetrics, int dropCountSinceLastInvocation) {
                    frameCounter.incrementAndGet();
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        try {
                            long gpuNs = frameMetrics.getMetric(FrameMetrics.GPU_DURATION);
                            if (gpuNs > 0) {
                                lastGpuDurationNs = (float) gpuNs;
                            }
                        } catch (Throwable ignored) {}
                    }
                }
            };

    /**
     * Reads native D3D9 bridge measurements from libnfsu2_android.so logcat output.
     */
    private void startLogcatFpsMonitor() {
        if (logcatThread != null && logcatThread.isAlive()) return;
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

    public void start() {
        if (!isEnabled() || running) return;
        running = true;

        lastFrameCalcTime = SystemClock.elapsedRealtime();
        lastNativeFrameCount = -1;
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

        if (logcatThread != null) {
            try { logcatThread.interrupt(); } catch (Exception ignored) {}
            logcatThread = null;
        }

        if (metricsListenerRegistered) {
            try {
                activity.getWindow().removeOnFrameMetricsAvailableListener(frameMetricsListener);
            } catch (Exception ignored) {}
            metricsListenerRegistered = false;
        }

        hide();
    }

    public void show() {
        if (!isEnabled() || !running || attached || suspended) return;
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

    private int perfBindCount = 0;
    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            if (!running) return;

            // Re-bind worker threads once shortly after GuestBootWorker is created
            if (perfBindCount == 2) {
                PerformanceCores.bindToPerformanceCores();
            }
            perfBindCount++;

            long now = SystemClock.elapsedRealtime();
            long elapsed = now - lastFrameCalcTime;
            lastFrameCalcTime = now;

            boolean calculatedFromNative = false;
            if (elapsed > 0) {
                // Primary: Measured presentation FPS directly from D3D9 bridge logcat
                if (measuredNativeFps > 0 && (now - lastNativeLogTime < 15000)) {
                    liveGameFps = measuredNativeFps;
                    lastFrameDurationMs = 1000.0f / measuredNativeFps;
                    calculatedFromNative = true;
                }

                // Secondary: Frame count from libnfsu2_roam camera hook
                if (!calculatedFromNative && io.mininxd.nfsu2.net.RoamNativeBridge.isLoaded()) {
                    try {
                        long currentNativeFrames = io.mininxd.nfsu2.net.RoamNativeBridge.nativeGetFrameCount();
                        if (currentNativeFrames >= 0) {
                            if (lastNativeFrameCount >= 0 && currentNativeFrames > lastNativeFrameCount) {
                                long delta = currentNativeFrames - lastNativeFrameCount;
                                float measuredFps = (delta * 1000.0f) / elapsed;
                                liveGameFps = measuredFps;
                                lastFrameDurationMs = 1000.0f / measuredFps;
                                calculatedFromNative = true;
                            }
                            lastNativeFrameCount = currentNativeFrames;
                        }
                    } catch (Throwable ignored) {}
                }
            }

            // Also poll rust native telemetry buffer (CPU / GPU / present FPS if available)
            if (graphicsCacheLoaded) {
                try {
                    if (nativeGetTelemetry(telemetryBuffer)) {
                        if (telemetryBuffer[0] > 0.0f) {
                            liveGameFps = telemetryBuffer[0];
                            if (telemetryBuffer[1] > 0.0f) {
                                lastFrameDurationMs = telemetryBuffer[1];
                            }
                        }
                    }
                } catch (Throwable ignored) {}
            }

            updateStats();

            if (!suspended && !attached && activity.hasWindowFocus()) {
                show();
            }

            handler.postDelayed(this, 1000); // 1-second update cycle
        }
    };

    private void updateStats() {
        float fps = liveGameFps;
        float frameTimeMs = (lastFrameDurationMs > 0.0f) ? lastFrameDurationMs : ((fps > 0.0f) ? (1000.0f / fps) : 0.0f);
        float p1Low = 0.0f;
        if (fps > 0.5f) {
            if (safeGetFrameMetrics(frameMetricsBuffer) && frameMetricsBuffer[2] > 0.5f) {
                p1Low = frameMetricsBuffer[2];
            } else {
                safeRecordFrameDuration((long)(frameTimeMs * 1_000_000.0f));
                if (safeGetFrameMetrics(frameMetricsBuffer) && frameMetricsBuffer[2] > 0.5f) {
                    p1Low = frameMetricsBuffer[2];
                }
            }
        }
        if (fps <= 0.0f) {
            try {
                fps = Integer.parseInt(GameOptions.get(activity, GameOptions.FPS));
            } catch (Exception ignored) {
                fps = 60.0f;
            }
            if (frameTimeMs <= 0.0f) {
                frameTimeMs = 1000.0f / fps;
            }
        }
        String fpsStr = (fps >= 100.0f) ? String.format(Locale.US, "%.0f", fps) : String.format(Locale.US, "%.1f", fps);

        float cpuRaw = (telemetryBuffer[2] > 0.0f) ? telemetryBuffer[2] : sampleCpuUsage();
        float cpu = Math.max(0.0f, Math.min(100.0f, cpuRaw));

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

        StringBuilder racersSb = new StringBuilder();
        for (int i = 0; i < racers.size(); i++) {
            if (i > 0) racersSb.append(", ");
            racersSb.append(racers.get(i));
        }
        String racersCsv = racersSb.toString();
        float gpu = getGpuUsagePercent();

        try {
            String nativeText = nativeFormatOverlayText(
                    isFull(),
                    fps,
                    p1Low,
                    frameTimeMs,
                    cpu,
                    gpu,
                    usedMb,
                    totalMb,
                    GameOptions.isRoamEnabled(activity),
                    adhocConnected,
                    isRacing,
                    onRaceCount,
                    racersCsv,
                    displayOnline,
                    roamClient.getServerHost(),
                    room != null ? room : "",
                    roomRoam,
                    totalRoam,
                    totalServer,
                    roamClient.getConnectionStatus(),
                    gpuDriverName != null ? gpuDriverName : "",
                    cpuModel != null ? cpuModel : "",
                    systemPlatform != null ? systemPlatform : ""
            );
            if (nativeText != null && !nativeText.isEmpty()) {
                textView.setText(nativeText);
                return;
            }
        } catch (Throwable ignored) {}

        if (isFull()) {
            StringBuilder sb = new StringBuilder();
            sb.append("fps: ").append(fpsStr);
            if (p1Low > 0.0f) {
                sb.append(" (1% low: ").append(Math.round(p1Low)).append(")");
            }
            sb.append(" (").append(String.format(Locale.US, "%.1f ms", frameTimeMs)).append(")\n");

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
            sb.append(String.format(Locale.US, "gpu_usage: %.0f%%\n", gpu));
            // Display system info directly without "system:" label prefix
            sb.append(gpuDriverName).append(", ").append(cpuModel).append(", ").append(systemPlatform).append('\n');
            sb.append("memory: ").append(usedMb).append(" MB / ").append(totalMb).append(" MB");
            textView.setText(sb.toString());
        } else {
            // Compact Mode: sleek right-aligned HUD
            if (adhocConnected) {
                if (isRacing) {
                    String text = String.format(Locale.US,
                            "FPS: %s (%.1f ms)  |  Racing: %d %s (Online: %d)\nCPU: %.0f%%  |  GPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                            fpsStr, frameTimeMs, onRaceCount, racers.toString(), displayOnline,
                            cpu, gpu, usedMb / 1024.0f, totalMb / 1024.0f);
                    textView.setText(text);
                } else {
                    String text = String.format(Locale.US,
                            "FPS: %s (%.1f ms)  |  Online: %d  |  connected to %s\nCPU: %.0f%%  |  GPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                            fpsStr, frameTimeMs, displayOnline, roamClient.getServerHost(),
                            cpu, gpu, usedMb / 1024.0f, totalMb / 1024.0f);
                    textView.setText(text);
                }
            } else if (GameOptions.isRoamEnabled(activity)) {
                String text = String.format(Locale.US,
                        "FPS: %s (%.1f ms)  |  network: %s\nCPU: %.0f%%  |  GPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                        fpsStr, frameTimeMs, roamClient.getConnectionStatus(),
                        cpu, gpu, usedMb / 1024.0f, totalMb / 1024.0f);
                textView.setText(text);
            } else {
                String text = String.format(Locale.US,
                        "FPS: %s (%.1f ms)  |  CPU: %.0f%%  |  GPU: %.0f%%  |  RAM: %.1f/%.1f GB",
                        fpsStr, frameTimeMs, cpu, gpu,
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

        // Fallback: process CPU accounting normalized by total available cores (0.0% to 100.0%)
        long nowWall = SystemClock.elapsedRealtime();
        long nowCpu = Process.getElapsedCpuTime();
        if (lastWallTime > 0 && nowWall > lastWallTime) {
            long cpuDiff = nowCpu - lastProcessCpuTime;
            long wallDiff = nowWall - lastWallTime;
            int cores = Math.max(1, Runtime.getRuntime().availableProcessors());
            lastCpuPercent = (cpuDiff * 100.0f) / (Math.max(1L, wallDiff) * cores);
        }
        lastWallTime = nowWall;
        lastProcessCpuTime = nowCpu;

        return Math.max(0.0f, Math.min(100.0f, lastCpuPercent));
    }

    private float getTargetFps() {
        try {
            float val = Float.parseFloat(GameOptions.get(activity, GameOptions.FPS));
            if (val > 0.0f) return val;
        } catch (Exception ignored) {}
        return 60.0f;
    }

    private float getGpuUsagePercent() {
        // 1. Rust native telemetry buffer (if sysfs read succeeded)
        if (telemetryBuffer[3] > 0.0f && telemetryBuffer[3] <= 100.0f) {
            smoothedGpuPercent = telemetryBuffer[3];
            return smoothedGpuPercent;
        }

        // 2. Direct Java sysfs sampling across Adreno, Mali, MediaTek
        Float sysfsVal = sampleGpuUsage();
        if (sysfsVal != null && sysfsVal >= 0.0f && sysfsVal <= 100.0f) {
            smoothedGpuPercent = sysfsVal;
            return smoothedGpuPercent;
        }

        // 3. Android FrameMetrics GPU duration (API 26+)
        if (lastGpuDurationNs > 0.0f) {
            float targetFps = getTargetFps();
            float frameBudgetNs = (1000.0f / targetFps) * 1_000_000.0f;
            float metricPct = Math.min(100.0f, (lastGpuDurationNs * 100.0f) / frameBudgetNs);
            if (metricPct > 0.0f) {
                if (smoothedGpuPercent < 0.0f) smoothedGpuPercent = metricPct;
                else smoothedGpuPercent = smoothedGpuPercent * 0.7f + metricPct * 0.3f;
                return Math.max(0.0f, Math.min(100.0f, smoothedGpuPercent));
            }
        }

        // 4. Balanced GPU rendering load estimation (differentiates CPU vs GPU bound state)
        float targetFps = getTargetFps();
        float targetMs = 1000.0f / targetFps;
        float actualMs = (lastFrameDurationMs > 0.0f) ? lastFrameDurationMs : (liveGameFps > 0.0f ? (1000.0f / liveGameFps) : targetMs);
        float cpu = lastCpuPercent;

        float estimated;
        if (liveGameFps >= (targetFps * 0.90f)) {
            // Reaching full target frame rate (e.g. garage, showroom, menus, or light scenes):
            // GPU is rendering lightweight geometry within ~2-3ms of a 16.6ms budget.
            // Avoid misleadingly pegging GPU to 95-98% in menus/garage.
            float loadPct = 25.0f + (cpu * 0.20f);
            estimated = Math.min(45.0f, Math.max(20.0f, loadPct));
        } else if (cpu > 70.0f) {
            // CPU Bound bottleneck (races, traffic, physics, world streaming):
            // GPU finishes rendering early and spends the remaining frame interval idle.
            float ratio = Math.max(0.2f, liveGameFps / targetFps);
            estimated = Math.min(50.0f, Math.max(18.0f, ratio * 45.0f));
        } else {
            // Balanced gameplay load: scale to real frame delivery vs budget
            float efficiency = Math.min(1.0f, targetMs / Math.max(1.0f, actualMs));
            estimated = Math.min(65.0f, Math.max(25.0f, efficiency * 60.0f));
        }

        if (smoothedGpuPercent < 0.0f) {
            smoothedGpuPercent = estimated;
        } else {
            smoothedGpuPercent = smoothedGpuPercent * 0.75f + estimated * 0.25f;
        }
        return Math.max(0.0f, Math.min(100.0f, smoothedGpuPercent));
    }

    private boolean gpuSysfsChecked = false;
    private String workingGpuSysfsPath = null;
    private boolean workingGpuIsBusyPair = false;

    private Float sampleGpuUsage() {
        if (gpuSysfsChecked) {
            if (workingGpuSysfsPath == null) return null;
            if (workingGpuIsBusyPair) return readGpubusyFile(new File(workingGpuSysfsPath));
            try (RandomAccessFile raf = new RandomAccessFile(workingGpuSysfsPath, "r")) {
                String line = raf.readLine();
                if (line != null) {
                    float val = Float.parseFloat(line.trim().replace("%", "").trim().split("\\s+")[0]);
                    if (val >= 0.0f && val <= 100.0f) return val;
                }
            } catch (Exception ignored) {
                workingGpuSysfsPath = null;
            }
            return null;
        }
        gpuSysfsChecked = true;
        String[] percentagePaths = new String[]{
                "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
                "/sys/class/kgsl/kgsl-3d0/gpu_load",
                "/sys/class/misc/mali0/device/utilization",
                "/sys/class/misc/mali0/device/gpu_busy",
                "/sys/devices/platform/mali.0/utilization",
                "/sys/devices/platform/13040000.mali/utilization",
                "/sys/devices/platform/13000000.mali/utilization",
                "/sys/devices/platform/14ac0000.mali/utilization",
                "/sys/devices/platform/1c500000.mali/utilization",
                "/sys/devices/platform/sprd_mali.0/utilization",
                "/sys/devices/platform/gpu/utilization",
                "/sys/kernel/debug/ged/hal/gpu_utilization",
                "/sys/module/ged/parameters/gpu_loading",
                "/sys/module/ged/parameters/gpu_dvfs_loading"
        };
        for (String p : percentagePaths) {
            try (RandomAccessFile raf = new RandomAccessFile(p, "r")) {
                String line = raf.readLine();
                if (line != null) {
                    line = line.trim().replace("%", "").trim();
                    String[] parts = line.split("\\s+");
                    float val = Float.parseFloat(parts[0]);
                    if (val >= 0.0f && val <= 100.0f) {
                        workingGpuSysfsPath = p;
                        workingGpuIsBusyPair = false;
                        return val;
                    }
                }
            } catch (Exception ignored) {}
        }

        String[] busyPaths = new String[]{
                "/sys/class/kgsl/kgsl-3d0/gpubusy",
                "/sys/devices/platform/soc/soc:qcom,kgsl-3d0/kgsl/kgsl-3d0/gpubusy"
        };
        for (String bp : busyPaths) {
            Float val = readGpubusyFile(new File(bp));
            if (val != null) {
                workingGpuSysfsPath = bp;
                workingGpuIsBusyPair = true;
                return val;
            }
        }

        if (!kgslDirChecked) {
            kgslDirChecked = true;
            try {
                File soc = new File("/sys/devices/platform/soc");
                if (soc.isDirectory()) {
                    File[] sub = soc.listFiles((dir, name) -> name.contains("kgsl"));
                    if (sub != null && sub.length > 0) {
                        cachedKgslDir = sub[0];
                    }
                }
            } catch (Exception ignored) {}
        }
        if (cachedKgslDir != null) {
            File pctFile = new File(cachedKgslDir, "kgsl/kgsl-3d0/gpu_busy_percentage");
            if (!pctFile.exists()) pctFile = new File(cachedKgslDir, "kgsl/kgsl-3d0/gpu_load");
            if (pctFile.exists()) {
                try (RandomAccessFile raf = new RandomAccessFile(pctFile, "r")) {
                    String line = raf.readLine();
                    if (line != null) {
                        float v = Float.parseFloat(line.trim().replace("%", "").trim().split("\\s+")[0]);
                        if (v >= 0.0f && v <= 100.0f) return v;
                    }
                } catch (Exception ignored) {}
            }
            File busyFile = new File(cachedKgslDir, "kgsl/kgsl-3d0/gpubusy");
            Float val = readGpubusyFile(busyFile);
            if (val != null) return val;
        }

        return null;
    }

    private Float readGpubusyFile(File file) {
        if (!file.exists()) return null;
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            String line = raf.readLine();
            if (line != null) {
                String[] parts = line.trim().split("\\s+");
                if (parts.length >= 2) {
                    long busy = Long.parseLong(parts[0]);
                    long total = Long.parseLong(parts[1]);
                    if (total > 0 && busy >= 0 && busy <= total) {
                        return (busy * 100.0f) / total;
                    }
                }
            }
        } catch (Exception ignored) {}
        return null;
    }

    private int dp(int value) {
        DisplayMetrics dm = activity.getResources().getDisplayMetrics();
        return Math.round(value * dm.density);
    }
}
