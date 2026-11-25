package io.mininxd.nfsu2;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Launcher in the style of the MW and Carbon ports: game files and PLAY on the left, options on the
 * right (Vulkan driver, resolution, frame cap, widescreen, compatibility).
 */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_DRIVER_ZIP = 45, REQUEST_DRIVER_PROBE = 46, REQUEST_CHOOSE_FOLDER = 47;
    // NFSU2 Revived Theme (inspired by nfsu2-web: dark asphalt, carbon grey, electric lime).
    private static final int ACCENT = 0xFFC8F088; // NFSU2 menu electric lime
    private static final int BG_PANEL = 0xFF1C1C1C;
    private static final int STROKE_PANEL = 0xFF333333;
    private static final int BG_FIELD = 0xFF141414;
    private static final int STROKE_FIELD = 0xFF2A2A2A;
    private static final int TEXT_MUTED = 0xFFC4C4C4;
    private static final int TEXT_HINT = 0xFF8A8A8A;
    private static final int COLOR_OK = 0xFF7CD992;
    private static final int COLOR_ERR = 0xFFFF8A80;
    private static final int COLOR_WARN = 0xFFFFD36E;
    /** {label, guest language name, LANGUAGES/<file>.bin}. */
    private static final String[][] LANGUAGES = {
        {"English", "English UK", "English"}, {"Español", "Spanish", "Spanish"},
        {"Français", "French", "French"}, {"Deutsch", "German", "German"},
        {"Italiano", "Italian", "Italian"}, {"Nederlands", "Dutch", "Dutch"},
        {"Svenska", "Swedish", "Swedish"}, {"Dansk", "Danish", "Danish"},
        {"日本語", "Japanese", "Japanese"}, {"한국어", "Korean", "Korean"},
        {"繁體中文", "Chinese (Traditional)", "Chinese"}, {"ไทย", "Thai", "Thailand"},
        {"Português (Brasil)", "Portuguese", "Portuguese"}, {"Polski", "Polish", "Polish"},
        {"Русский", "Russian", "Russian"}, {"Türkçe", "Turkish", "Turkish"},
        {"Magyar", "Hungarian", "Hungarian"}
    };

    private LinearLayout optionsList;
    private TextView status;
    private Button play, access, downloadFiles, chooseLocation;
    private Button btnPresetPerformance, btnPresetBalanced, btnPresetOriginal, btnPresetCustom;
    public static Context sAppContext = null;
    public static String sCustomGamePath = null;
    private boolean playAfterProbe, checkingUpdates, resumed;
    private String updateLaterTag;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());

    private void applyDisplayRefreshRate() {
        try {
            android.view.Display display = getWindowManager().getDefaultDisplay();
            if (display == null) return;
            android.view.Display.Mode current = display.getMode();
            if (current == null) return;
            android.view.Display.Mode[] modes = display.getSupportedModes();
            if (modes == null) return;
            float refresh = 60f;
            android.view.Display.Mode bestMode = null;
            for (android.view.Display.Mode mode : modes) {
                if (mode != null && mode.getPhysicalWidth() == current.getPhysicalWidth() &&
                        mode.getPhysicalHeight() == current.getPhysicalHeight()) {
                    refresh = Math.max(refresh, mode.getRefreshRate());
                    if (bestMode == null || mode.getRefreshRate() > bestMode.getRefreshRate()) {
                        bestMode = mode;
                    }
                }
            }
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredRefreshRate = Math.min(120f, refresh);
            if (Build.VERSION.SDK_INT >= 23 && bestMode != null) {
                lp.preferredDisplayModeId = bestMode.getModeId();
            }
            try {
                java.lang.reflect.Field minField = lp.getClass().getField("preferredMinDisplayRefreshRate");
                minField.setFloat(lp, Math.min(60.0f, refresh));
                java.lang.reflect.Field maxField = lp.getClass().getField("preferredMaxDisplayRefreshRate");
                maxField.setFloat(lp, Math.min(120.0f, refresh));
            } catch (Throwable ignored) {}
            getWindow().setAttributes(lp);
        } catch (Throwable ignored) {}
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            try {
                getWindow().getDecorView().setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
                applyDisplayRefreshRate();
            } catch (Throwable ignored) {}
        }
    }

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        sAppContext = getApplicationContext();
        sCustomGamePath = GameOptions.prefs(this).getString(GameOptions.CUSTOM_GAME_DIR, null);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        setContentView(buildScreen());
        applyDisplayRefreshRate();
        checkUpdates(false);
        String updated = ReleaseUpdates.pendingWhatsNew(this);
        if (updated != null) showWhatsNew(updated);
    }

    /** First launch after an update: the installed version's release notes from GitHub. */
    private void showWhatsNew(String version) {
        AlertDialog dialog = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Updated to version " + version + "!")
                .setMessage("Loading what's new…")
                .setPositiveButton("OK", (d, which) -> ReleaseUpdates.whatsNewSeen(this))
                .setNeutralButton("View on GitHub", (d, which) -> {
                    ReleaseUpdates.whatsNewSeen(this);
                    openUrl(Uri.parse("https://github.com/mininxd/NFSU2-Revived-Android/releases/tag/v" + Uri.encode(version)));
                })
                .setOnCancelListener(d -> ReleaseUpdates.whatsNewSeen(this))
                .show();
        ReleaseUpdates.releaseNotes(this, version, notes -> {
            if (!dialog.isShowing()) return;
            // In the app, skip the download/install preamble of the release page.
            int start = notes == null ? -1 : Math.max(notes.indexOf("What's new"), notes.indexOf("Novedades"));
            if (start > 0) notes = notes.substring(start);
            dialog.setMessage(notes != null ? notes
                    : "Could not load what's new (offline or not published yet). You can view them on GitHub.");
        });
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        AdhocRoamClient.getInstance().queryServerPlayers(this, () -> {
            if (resumed) runOnUiThread(this::refreshOptions);
        });
        String crash = Diagnostics.unreportedCrash(this);
        if (crash != null) {
            Diagnostics.acknowledgeCrashes(this);
            autosaveDiagnostic(crash);
        }
    }

    @Override protected void onPause() {
        resumed = false;
        super.onPause();
    }

    // ---- Screen ------------------------------------------------------------------------------------------

    private View buildScreen() {
        FrameLayout screen = new FrameLayout(this);
        screen.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {0xFF222222, 0xFF0D0D0D, 0xFF080808}));
        LinearLayout columns = new LinearLayout(this);
        columns.setPadding(dp(24), dp(10), dp(24), dp(10));
        screen.addView(columns, new FrameLayout.LayoutParams(-1, -1));

        // Left: logo, game files and PLAY.
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setGravity(Gravity.CENTER_HORIZONTAL);
        ScrollView leftScroll = new ScrollView(this);
        leftScroll.setFillViewport(true);
        leftScroll.addView(left);
        columns.addView(leftScroll, new LinearLayout.LayoutParams(0, -1, 1.05f));

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.icono);
        logo.setAdjustViewBounds(true);
        header.addView(logo, new LinearLayout.LayoutParams(dp(76), dp(76)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(14), 0, 0, 0);
        titles.addView(label("NFSU2 Revived", 20, Color.WHITE, true));
        titles.addView(label("Need for Speed: Underground 2 · Recompiled", 12, TEXT_MUTED, false));
        header.addView(titles);
        left.addView(header, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout card = card();
        LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(-1, -2);
        cardParams.topMargin = dp(8);
        left.addView(card, cardParams);
        card.addView(label("GAME FILES", 12, ACCENT, true));
        status = label("", 13, 0xDDFFFFFF, false);
        status.setPadding(0, dp(4), 0, dp(6));
        card.addView(status);

        chooseLocation = actionButton("Choose Game Folder", false);
        chooseLocation.setOnClickListener(view -> showChooseLocationDialog());
        LinearLayout.LayoutParams chooseParams = new LinearLayout.LayoutParams(-1, dp(42));
        chooseParams.topMargin = dp(4);
        card.addView(chooseLocation, chooseParams);

        play = actionButton("PLAY", true);
        play.setOnClickListener(view -> play());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(-1, dp(54));
        playParams.topMargin = dp(8);
        left.addView(play, playParams);

        // Presets: Low, High, Original
        LinearLayout presetContainer = new LinearLayout(this);
        presetContainer.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams pcParams = new LinearLayout.LayoutParams(-1, -2);
        pcParams.topMargin = dp(6);
        left.addView(presetContainer, pcParams);

        TextView presetHeader = label("GRAPHICS PRESET", 11, TEXT_HINT, true);
        presetHeader.setPadding(dp(2), 0, 0, dp(3));
        presetContainer.addView(presetHeader);

        LinearLayout presetRow = new LinearLayout(this);
        presetRow.setOrientation(LinearLayout.HORIZONTAL);
        presetRow.setGravity(Gravity.CENTER_VERTICAL);
        presetContainer.addView(presetRow, new LinearLayout.LayoutParams(-1, dp(36)));

        btnPresetPerformance = presetButton("Performance", "performance");
        btnPresetBalanced = presetButton("Balanced", "balanced");
        btnPresetOriginal = presetButton("Original", "original");
        btnPresetCustom = presetButton("Custom", "custom");

        LinearLayout.LayoutParams pBtnLp1 = new LinearLayout.LayoutParams(0, -1, 1f);
        pBtnLp1.rightMargin = dp(4);
        presetRow.addView(btnPresetPerformance, pBtnLp1);

        LinearLayout.LayoutParams pBtnLp2 = new LinearLayout.LayoutParams(0, -1, 1f);
        pBtnLp2.rightMargin = dp(4);
        presetRow.addView(btnPresetBalanced, pBtnLp2);

        LinearLayout.LayoutParams pBtnLp3 = new LinearLayout.LayoutParams(0, -1, 1f);
        pBtnLp3.rightMargin = dp(4);
        presetRow.addView(btnPresetOriginal, pBtnLp3);

        LinearLayout.LayoutParams pBtnLp4 = new LinearLayout.LayoutParams(0, -1, 1f);
        presetRow.addView(btnPresetCustom, pBtnLp4);

        access = actionButton("Allow access to internal storage/nfsu2", false);
        access.setOnClickListener(view -> requestAccess());
        LinearLayout.LayoutParams accessParams = new LinearLayout.LayoutParams(-1, dp(46));
        accessParams.topMargin = dp(8);
        left.addView(access, accessParams);

        downloadFiles = actionButton("Download Game Data", false);
        downloadFiles.setOnClickListener(view -> startDownloadOrExtractGameData());
        LinearLayout.LayoutParams dlParams = new LinearLayout.LayoutParams(-1, dp(46));
        dlParams.topMargin = dp(8);
        left.addView(downloadFiles, dlParams);

        // Right: options.
        LinearLayout right = card();
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, -1, 1f);
        rightParams.leftMargin = dp(24);
        columns.addView(right, rightParams);
        right.addView(label("SETTINGS", 12, ACCENT, true));
        ScrollView scroll = new ScrollView(this);
        optionsList = new LinearLayout(this);
        optionsList.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(optionsList);
        right.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));
        return screen;
    }

    private void refresh() {
        boolean allowed = allowed();
        File root = gameRoot(this);
        if (allowed) ensureScriptsInstalled(root);
        boolean exe = allowed && isGameExecutable(root);
        if (allowed && !exe) {
            ensureGamefilesInstalled(root);
            exe = isGameExecutable(root);
        }
        boolean ready = exe && !languages().isEmpty();
        String displayPath = root != null ? root.getAbsolutePath() : "";
        status.setText(ready ? displayPath : (!displayPath.isEmpty() ? (!exe ? "SPEED2.EXE not found in " + displayPath : displayPath) : ""));
        status.setVisibility(ready || (!displayPath.isEmpty() && !exe) ? View.VISIBLE : View.GONE);
        play.setEnabled(ready);
        play.setAlpha(ready ? 1f : .45f);
        access.setVisibility(allowed ? View.GONE : View.VISIBLE);
        chooseLocation.setText(ready ? "Change Game Folder" : "Choose Game Folder");
        chooseLocation.setVisibility(allowed ? View.VISIBLE : View.GONE);

        String dlUrl = getGamefilesUrl();
        File localCacheZip = new File(Environment.getExternalStorageDirectory(), "cache/NFSU2_GameFiles.zip");
        boolean canInstall = allowed && !ready && (dlUrl != null || localCacheZip.isFile());
        downloadFiles.setVisibility(canInstall ? View.VISIBLE : View.GONE);
        if (canInstall) {
            downloadFiles.setText(localCacheZip.isFile() ? "Install Game Data (Found in /sdcard/cache)" : "Download & Install Game Data");
        }

        updatePresetButtons(GameOptions.prefs(this).getString("active_preset", "original"));
        refreshOptions();
    }

    private String getGamefilesUrl() {
        try (java.io.InputStream in = getAssets().open("gamefiles_url.txt")) {
            byte[] b = new byte[in.available()];
            in.read(b);
            String url = new String(b, java.nio.charset.StandardCharsets.UTF_8).trim();
            if (!url.isEmpty()) return url;
        } catch (Exception ignored) {}
        return null;
    }

    private void ensureGamefilesInstalled(File root) {
        if (!allowed() || isGameExecutable(root)) return;
        try {
            String[] list = getAssets().list("gamefiles");
            if (list != null && list.length > 0) {
                runOnUiThread(() -> status.setText("Extracting bundled game files..."));
                if (!root.exists()) root.mkdirs();
                copyAssetFolder(getAssets(), "gamefiles", root);
            }
        } catch (Exception e) {
            android.util.Log.w("NFSU2", "Failed to unpack bundled gamefiles", e);
        }
    }

    private static void copyAssetFolder(android.content.res.AssetManager assets, String src, File dest) {
        try {
            String[] files = assets.list(src);
            if (files == null || files.length == 0) {
                try (java.io.InputStream in = assets.open(src);
                     java.io.OutputStream out = new java.io.FileOutputStream(dest)) {
                    byte[] buf = new byte[16384];
                    int r;
                    while ((r = in.read(buf)) > 0) out.write(buf, 0, r);
                }
            } else {
                if (!dest.exists()) dest.mkdirs();
                for (String file : files) {
                    copyAssetFolder(assets, src + "/" + file, new File(dest, file));
                }
            }
        } catch (Exception ignored) {}
    }

    private void startDownloadOrExtractGameData() {
        File localCacheZip = new File(Environment.getExternalStorageDirectory(), "cache/NFSU2_GameFiles.zip");
        String url = getGamefilesUrl();
        downloadFiles.setEnabled(false);
        downloadFiles.setAlpha(0.5f);

        worker.execute(() -> {
            try {
                File targetZip = localCacheZip;
                if (!targetZip.isFile()) {
                    if (url == null) throw new RuntimeException("No download URL or cache zip found");
                    main.post(() -> status.setText("Connecting to download game files..."));
                    File cacheDir = new File(Environment.getExternalStorageDirectory(), "cache");
                    if (!cacheDir.exists()) cacheDir.mkdirs();
                    targetZip = new File(cacheDir, "NFSU2_GameFiles.zip");

                    java.net.HttpURLConnection conn = (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
                    conn.setConnectTimeout(20000);
                    conn.setReadTimeout(60000);
                    conn.setInstanceFollowRedirects(true);
                    int total = conn.getContentLength();
                    try (java.io.InputStream in = conn.getInputStream();
                         java.io.OutputStream out = new java.io.FileOutputStream(targetZip)) {
                        byte[] buf = new byte[32768];
                        int r, downloaded = 0;
                        long lastReport = 0;
                        while ((r = in.read(buf)) > 0) {
                            out.write(buf, 0, r);
                            downloaded += r;
                            long now = System.currentTimeMillis();
                            if (now - lastReport >= 400) {
                                lastReport = now;
                                final int d = downloaded, t = total;
                                main.post(() -> {
                                    if (t > 0) {
                                        int pct = (int) (((long) d * 100) / t);
                                        status.setText("Downloading: " + pct + "% (" + (d / (1024 * 1024)) + "/" + (t / (1024 * 1024)) + " MB)");
                                    } else {
                                        status.setText("Downloading: " + (d / (1024 * 1024)) + " MB");
                                    }
                                });
                            }
                        }
                    }
                }

                main.post(() -> status.setText("Extracting game files to internal storage..."));
                File destDir = Environment.getExternalStorageDirectory();
                extractZip(targetZip, destDir);

                main.post(() -> {
                    status.setText("Game files installed successfully!");
                    refresh();
                });
            } catch (Exception e) {
                android.util.Log.e("NFSU2", "Failed to download or install game data", e);
                main.post(() -> {
                    status.setText("Installation error: " + e.getMessage());
                    downloadFiles.setEnabled(true);
                    downloadFiles.setAlpha(1.0f);
                });
            }
        });
    }

    private static void extractZip(File zipFile, File destDir) throws Exception {
        byte[] buffer = new byte[32768];
        try (java.util.zip.ZipInputStream zis = new java.util.zip.ZipInputStream(new java.io.FileInputStream(zipFile))) {
            java.util.zip.ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                File target = new File(destDir, entry.getName());
                if (entry.isDirectory()) {
                    target.mkdirs();
                } else {
                    File parent = target.getParentFile();
                    if (parent != null && !parent.exists()) parent.mkdirs();
                    try (java.io.FileOutputStream fos = new java.io.FileOutputStream(target)) {
                        int len;
                        while ((len = zis.read(buffer)) > 0) fos.write(buffer, 0, len);
                    }
                }
                zis.closeEntry();
            }
        }
    }

    private void ensureScriptsInstalled(File root) {
        if (!root.isDirectory()) return;

        // Clean up legacy DLL or ASI files if previously extracted
        try {
            File oldDinput = new File(root, "dinput8.dll");
            if (oldDinput.exists()) oldDinput.delete();
            File oldAsi = new File(root, "scripts/NFSUnderground2.WidescreenFix.asi");
            if (oldAsi.exists()) oldAsi.delete();
        } catch (Exception ignored) {
        }

        // Install widescreen data file only
        File scriptsDir = new File(root, "scripts");
        if (!scriptsDir.exists() && !scriptsDir.mkdirs()) return;
        try {
            File targetDat = new File(scriptsDir, "NFSUnderground2.WidescreenFix.dat");
            File held = new File(scriptsDir, "NFSUnderground2.WidescreenFix.dat.hold");
            if ((!targetDat.exists() && !held.exists()) || (targetDat.exists() && targetDat.length() == 0)) {
                copyAssetFile(this, "scripts/NFSUnderground2.WidescreenFix.dat", targetDat);
                android.util.Log.i("NFSU2", "Installed widescreen data to " + targetDat.getAbsolutePath());
            }
        } catch (Exception e) {
            android.util.Log.w("NFSU2", "Failed to copy widescreen data", e);
        }

        // Install Extra Options settings INI
        try {
            File targetIni = new File(scriptsDir, "NFSU2ExtraOptionsSettings.ini");
            if (!targetIni.exists() || targetIni.length() == 0) {
                File cacheIni = new File(Environment.getExternalStorageDirectory(), "cache/scripts/NFSU2ExtraOptionsSettings.ini");
                if (cacheIni.isFile() && cacheIni.length() > 0) {
                    try (java.io.InputStream in = new java.io.FileInputStream(cacheIni);
                         java.io.OutputStream out = new java.io.FileOutputStream(targetIni)) {
                        byte[] buf = new byte[32768];
                        int r;
                        while ((r = in.read(buf)) > 0) out.write(buf, 0, r);
                    }
                    android.util.Log.i("NFSU2", "Installed Extra Options INI from /sdcard/cache to " + targetIni.getAbsolutePath());
                } else {
                    copyAssetFile(this, "scripts/NFSU2ExtraOptionsSettings.ini", targetIni);
                    android.util.Log.i("NFSU2", "Installed Extra Options INI from assets to " + targetIni.getAbsolutePath());
                }
            }
        } catch (Exception e) {
            android.util.Log.w("NFSU2", "Failed to copy Extra Options INI", e);
        }
    }

    public static void copyAssetFile(Context context, String assetPath, File destFile) throws Exception {
        try (java.io.InputStream in = context.getAssets().open(assetPath);
             java.io.OutputStream out = new java.io.FileOutputStream(destFile)) {
            byte[] buf = new byte[32768];
            int read;
            while ((read = in.read(buf)) > 0) {
                out.write(buf, 0, read);
            }
        }
    }

    private void refreshOptions() {
        optionsList.removeAllViews();
        GpuDrivers.Driver driver = GpuDrivers.selected(this);
        // Custom drivers (libadrenotools) exist only in the 64-bit build.
        if (android.os.Process.is64Bit())
            optionsList.addView(optionRow("Vulkan Driver", driver == null ? "System default" : driver.name, this::chooseDriver));
        optionsList.addView(optionRow("Check driver", "GPU, graphics API & driver info", () -> probeDriver(false)));
        optionsList.addView(optionRow("Game Location", locationLabel(), this::showChooseLocationDialog));
        optionsList.addView(optionRow("Language", languageLabel(), this::chooseLanguage));
        for (GameOptions.Option option : GameOptions.ALL)
            optionsList.addView(optionRow(option.title, option.label(GameOptions.get(this, option.key)), () -> chooseOption(option)));
        // Hidden for clean release (Extra Options enabled by default):
        // optionsList.addView(optionRow("Cheat Codes", cheatCodesSummary(), this::chooseCheatCodes));
        // optionsList.addView(optionRow("Extra Options", extraOptionsSummary(), this::chooseExtraOptions));
        // optionsList.addView(optionRow("Freeroam Online (beta)", GameOptions.isRoamEnabled(this) ? "On" : "Off", this::toggleFreeRoam));
        // optionsList.addView(optionRow("Ghost Mode", GameOptions.isGhostModeEnabled(this) ? "On" : "Off", this::toggleGhostMode));
        optionsList.addView(optionRow("Online Server", serverStatusSummary(), this::chooseOnlineServer));
        // optionsList.addView(optionRow("Room Name", GameOptions.roamRoom(this).isEmpty() ? "Default (Public)" : GameOptions.roamRoom(this), this::chooseRoamRoom));
        // optionsList.addView(optionRow("Crew Name", GameOptions.roamCrew(this).isEmpty() ? "None" : GameOptions.roamCrew(this), this::chooseRoamCrew));
        optionsList.addView(optionRow("Update Channel", updateChannelLabel(), this::chooseUpdateChannel));
        optionsList.addView(optionRow("Check for updates",
                checkingUpdates ? "Checking GitHub (" + updateChannelLabel() + ")…" : "Version " + ReleaseUpdates.installed(this), () -> checkUpdates(true)));
        optionsList.addView(optionRow("Report an issue", "GitHub issue / diagnostic ZIP", () -> showReportDialog(null)));
    }

    private String serverStatusSummary() {
        String server = GameOptions.onlineServer(this);
        int roam = AdhocRoamClient.getInstance().getTotalRoamPlayers();
        int total = AdhocRoamClient.getInstance().getTotalServerPlayers();
        if (roam > 0) {
            return server + " (" + roam + " roaming)";
        } else if (total > 0) {
            return server + " (" + total + " online)";
        }
        return server;
    }

    // ---- Options ---------------------------------------------------------------------------------------

    private List<String[]> languages() {
        List<String[]> found = new ArrayList<>();
        File root = gameRoot(this);
        if (root == null || !root.isDirectory()) return found;
        File langDir = findSubDir(root, "LANGUAGES");
        File langPacks = findSubDir(root, "LANGPACKS");
        for (String[] language : LANGUAGES) {
            String code = language[2];
            String codeLower = code.toLowerCase(Locale.ROOT);
            String nameLower = language[1].toLowerCase(Locale.ROOT);
            if ((langDir != null && (findSubFile(langDir, code + ".bin") != null || findSubFile(langDir, codeLower + ".bin") != null))
                    || (langPacks != null && (findSubDir(langPacks, codeLower) != null || findSubDir(langPacks, nameLower) != null || findSubFile(langPacks, code + ".bin") != null))
                    || ("Spanish".equalsIgnoreCase(language[1]) && langDir != null && (
                        findSubFile(langDir, "Spanish.bin") != null
                        || findSubFile(langDir, "spanish.bin") != null
                        || findSubFile(langDir, "Espanol.bin") != null
                        || findSubFile(langDir, "espanol.bin") != null
                    ))) {
                found.add(language);
            }
        }
        return found;
    }

    private String languageLabel() {
        String current = GameOptions.language(this);
        for (String[] language : LANGUAGES) {
            if (language[1].equalsIgnoreCase(current) || language[0].equalsIgnoreCase(current)) return language[0];
        }
        return "English";
    }

    private void chooseLanguage() {
        List<String[]> found = allowed() ? languages() : new ArrayList<>();
        if (found.isEmpty()) {
            File root = gameRoot(this);
            String langPath = root != null ? (root.getAbsolutePath() + "/LANGUAGES") : "internal storage/nfsu2/LANGUAGES";
            message("Language", "No languages found in " + langPath + ".");
            return;
        }
        String[] names = new String[found.size()];
        int checked = 0;
        String current = GameOptions.language(this);
        for (int i = 0; i < names.length; i++) {
            names[i] = found.get(i)[0];
            if (found.get(i)[1].equalsIgnoreCase(current)) checked = i;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Language")
                .setSingleChoiceItems(names, checked, (dialog, which) -> {
                    String selected = found.get(which)[1];
                    if ("English".equalsIgnoreCase(selected)) selected = "English UK";
                    GameOptions.prefs(this).edit().putString(GameOptions.LANGUAGE, selected).apply();
                    dialog.dismiss(); refreshOptions();
                }).setNegativeButton("Cancel", null).show();
    }

    private void chooseOption(GameOptions.Option option) {
        String current = GameOptions.get(this, option.key);
        int checked = 0;
        for (int i = 0; i < option.values.length; i++) if (option.values[i].equals(current)) checked = i;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(option.title)
                .setSingleChoiceItems(option.labels, checked, (dialog, which) -> {
                    String val = option.values[which];
                    GameOptions.set(this, option.key, val);
                    if (GameOptions.TOUCH_SIZE.equals(option.key)) {
                        getSharedPreferences("touch_layouts_v2", MODE_PRIVATE)
                                .edit().putString("control_size", val).apply();
                    }
                    if (GameOptions.MENU_BUTTON_SIZE.equals(option.key)) {
                        GameOptions.syncWidescreenIni(this);
                    }
                    if (GameOptions.WIDESCREEN.equals(option.key) || GameOptions.MINIMAP.equals(option.key)) {
                        GameOptions.applyWidescreenPatchFile(this);
                    }
                    saveCustomPreset();
                    dialog.dismiss(); refreshOptions();
                }).setNegativeButton("Cancel", null).show();
    }

    private String cheatCodesSummary() {
        int count = 0;
        for (GameOptions.Cheat c : GameOptions.CHEATS) {
            if (GameOptions.isCheatEnabled(this, c.id)) count++;
        }
        if (count == 0) return "None active";
        if (count == GameOptions.CHEATS.length) return "All active (" + count + ")";
        return count + " active";
    }

    private void chooseCheatCodes() {
        int n = GameOptions.CHEATS.length;
        CharSequence[] items = new CharSequence[n];
        boolean[] checked = new boolean[n];
        for (int i = 0; i < n; i++) {
            GameOptions.Cheat c = GameOptions.CHEATS[i];
            items[i] = c.title + "\n" + c.description;
            checked[i] = GameOptions.isCheatEnabled(this, c.id);
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Cheat Codes")
                .setMultiChoiceItems(items, checked, (dialog, which, isChecked) -> {
                    checked[which] = isChecked;
                    GameOptions.setCheatEnabled(this, GameOptions.CHEATS[which].id, isChecked);
                })
                .setPositiveButton("Done", (dialog, which) -> refreshOptions())
                .setNeutralButton("Select All", (dialog, which) -> {
                    for (int i = 0; i < n; i++) {
                        GameOptions.setCheatEnabled(this, GameOptions.CHEATS[i].id, true);
                    }
                    refreshOptions();
                })
                .setNegativeButton("Clear All", (dialog, which) -> {
                    for (int i = 0; i < n; i++) {
                        GameOptions.setCheatEnabled(this, GameOptions.CHEATS[i].id, false);
                    }
                    refreshOptions();
                })
                .setOnDismissListener(dialog -> refreshOptions())
                .show();
    }

    private String extraOptionsSummary() {
        boolean enabled = GameOptions.isExtraOptionsEnabled(this);
        if (!enabled) return "Disabled";
        return "Active (Regional Cars, Laps 0-127, Opponents 0-5, 3D Fixes)";
    }

    private void chooseExtraOptions() {
        boolean enabled = GameOptions.isExtraOptionsEnabled(this);
        String[] features = new String[] {
            "Extra Options Active (ExOpts Team mod)",
            "Regional Cars (Peugeot 106 & Opel Corsa in NTSC)",
            "Lap Controller (0 - 127 laps in Quick Race & LAN)",
            "Opponent Controller (0 - 5 opponents in all modes)",
            "1-Lap Race Progress (COMPLETED %X display)",
            "Drift Multiplier (AxB format, plus sign, up to 9x)",
            "3D Fixes (Wheels fix, damaged glass, megalow parts)",
            "Menu Enhancements (Outrun mode & Free Run track select)"
        };
        boolean[] checked = new boolean[] {
            enabled,
            GameOptions.getExtraOption(this, "regional_cars", true),
            GameOptions.getExtraOption(this, "lap_controller", true),
            GameOptions.getExtraOption(this, "opponent_controller", true),
            GameOptions.getExtraOption(this, "lap_percent", true),
            GameOptions.getExtraOption(this, "drift_tweaks", true),
            GameOptions.getExtraOption(this, "fixes_3d", true),
            GameOptions.getExtraOption(this, "menu_enhancements", true)
        };

        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("NFSU2 Extra Options (ExOpts Team)")
                .setMultiChoiceItems(features, checked, (dialog, which, isChecked) -> {
                    checked[which] = isChecked;
                    if (which == 0) {
                        GameOptions.setExtraOptionsEnabled(this, isChecked);
                    } else {
                        String[] keys = {"", "regional_cars", "lap_controller", "opponent_controller", "lap_percent", "drift_tweaks", "fixes_3d", "menu_enhancements"};
                        GameOptions.setExtraOption(this, keys[which], isChecked);
                    }
                    GameOptions.syncExtraOptionsIni(this);
                })
                .setPositiveButton("Done", (dialog, which) -> refreshOptions())
                .setNeutralButton("Reset Defaults", (dialog, which) -> {
                    GameOptions.setExtraOptionsEnabled(this, true);
                    GameOptions.resetExtraOptions(this);
                    GameOptions.syncExtraOptionsIni(this);
                    refreshOptions();
                })
                .setOnDismissListener(dialog -> refreshOptions())
                .show();
    }

    private void toggleFreeRoam() {
        boolean next = !GameOptions.isRoamEnabled(this);
        GameOptions.setRoamEnabled(this, next);
        refreshOptions();
    }

    private void toggleGhostMode() {
        boolean next = !GameOptions.isGhostModeEnabled(this);
        GameOptions.setGhostModeEnabled(this, next);
        refreshOptions();
    }

    private void chooseOnlineServer() {
        android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setText(GameOptions.onlineServer(this));
        input.setSelection(input.getText().length());
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        android.widget.FrameLayout box = new android.widget.FrameLayout(this);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad / 2, pad, 0);
        box.addView(input);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Online server")
                .setMessage("Server for EA Nation and Free Roam multiplayer. Default: eahub.eu (Free Roam uses adhoc.eahub.eu). Custom servers use the exact host entered.")
                .setView(box)
                .setPositiveButton("Save", (dialog, which) -> {
                    String value = input.getText().toString().trim();
                    if (!GameOptions.validServer(value)) { message("Online server", "\"" + value + "\" is not a valid server name."); return; }
                    GameOptions.prefs(this).edit().putString(GameOptions.ONLINE_SERVER, value).apply();
                    AdhocRoamClient.getInstance().resetServerStats();
                    refreshOptions();
                    AdhocRoamClient.getInstance().queryServerPlayers(this, () -> {
                        if (resumed) runOnUiThread(this::refreshOptions);
                    });
                })
                .setNeutralButton("Reset", (dialog, which) -> {
                    GameOptions.prefs(this).edit().remove(GameOptions.ONLINE_SERVER).apply();
                    AdhocRoamClient.getInstance().resetServerStats();
                    refreshOptions();
                })
                .setNegativeButton("Cancel", null).show();
    }

    private void chooseRoamRoom() {
        android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setHint("Leave empty for public room");
        input.setText(GameOptions.roamRoom(this));
        input.setSelection(input.getText().length());
        android.widget.FrameLayout box = new android.widget.FrameLayout(this);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad / 2, pad, 0);
        box.addView(input);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Room Name")
                .setMessage("Multiplayer room name. Leave empty for the main public world (default). Set a name to join a private room with friends.")
                .setView(box)
                .setPositiveButton("Save", (dialog, which) -> {
                    String value = input.getText().toString().trim();
                    GameOptions.setRoamRoom(this, value);
                    refreshOptions();
                })
                .setNeutralButton("Clear (Public)", (dialog, which) -> {
                    GameOptions.setRoamRoom(this, "");
                    refreshOptions();
                })
                .setNegativeButton("Cancel", null).show();
    }

    private void chooseRoamCrew() {
        android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setHint("Crew name (e.g. DRIFT, SPEED)");
        input.setText(GameOptions.roamCrew(this));
        input.setSelection(input.getText().length());
        android.widget.FrameLayout box = new android.widget.FrameLayout(this);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad / 2, pad, 0);
        box.addView(input);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Crew Name")
                .setMessage("Friends who type the same crew name always see each other in free roam, however far apart, with names and map arrows in the crew's colour. Up to 4 crews. Anyone who types the name joins.")
                .setView(box)
                .setPositiveButton("Save", (dialog, which) -> {
                    String value = input.getText().toString().trim();
                    GameOptions.setRoamCrew(this, value);
                    refreshOptions();
                })
                .setNeutralButton("Clear", (dialog, which) -> {
                    GameOptions.setRoamCrew(this, "");
                    refreshOptions();
                })
                .setNegativeButton("Cancel", null).show();
    }

    private void chooseDriver() {
        List<GpuDrivers.Driver> drivers = GpuDrivers.list(this);
        String[] names = new String[drivers.size() + 1];
        names[0] = "System default driver";
        int selected = 0;
        for (int i = 0; i < drivers.size(); ++i) {
            names[i + 1] = drivers.get(i).name;
            if (drivers.get(i).id.equals(GpuDrivers.selectedId(this))) selected = i + 1;
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Vulkan Driver")
                .setSingleChoiceItems(names, selected, (dialog, which) -> {
                    GpuDrivers.select(this, which == 0 ? GpuDrivers.SYSTEM_DRIVER_ID : drivers.get(which - 1).id);
                    refreshOptions(); dialog.dismiss();
                })
                .setPositiveButton("Import ZIP", (dialog, which) -> startActivityForResult(
                        new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),
                        REQUEST_DRIVER_ZIP));
        if (!drivers.isEmpty()) {
            builder.setNeutralButton("Delete Custom Drivers", (dialog, which) -> {
                GpuDrivers.clearAll(this);
                refreshOptions();
                message("Vulkan Driver", "All custom drivers removed. Using system default driver.");
            });
        }
        builder.setNegativeButton("Back", null).show();
    }


    private void probeDriver(boolean thenPlay) {
        playAfterProbe = thenPlay;
        play.setEnabled(false);
        try {
            Intent intent = new Intent(this, GpuProbeActivity.class);
            intent.putExtra("backend", GameOptions.getEffectiveGraphicsBackend(this));
            startActivityForResult(intent, REQUEST_DRIVER_PROBE);
        } catch (Exception error) {
            play.setEnabled(true);
            playAfterProbe = false;
            message("Driver Check", "Could not check driver: " + error.getMessage());
        }
    }

    private void showDriverReport(String report) {
        play.setEnabled(true);
        try {
            org.json.JSONObject gpu = new org.json.JSONObject(report);
            boolean failed = gpu.optBoolean("driverLoadFailed") || gpu.has("probeError");
            if (playAfterProbe && !failed && gpu.optBoolean("compatible")) { playAfterProbe = false; launch(); return; }
            // The player pressed PLAY: a driver that loads but lacks a feature the
            // probe checks may still run the game (issue #1107), so offer it.
            boolean wantedToPlay = playAfterProbe && !failed;
            playAfterProbe = false;

            StringBuilder text = new StringBuilder();
            if (failed) {
                text.append("Could not use driver: ").append(gpu.optString("probeError", "error initializing graphics driver."));
            } else {
                String backend = gpu.optString("backend", "");
                if (backend.isEmpty()) {
                    backend = GameOptions.getEffectiveGraphicsBackend(this);
                }
                boolean isGles = "opengl".equalsIgnoreCase(backend) || gpu.has("gles");

                if (isGles) {
                    text.append("Graphics API:\n• Direct3D 9 via OpenGL ES (")
                            .append(gpu.optString("gles", "3.0+")).append(")\n\n");
                } else {
                    text.append("Graphics API:\n• Direct3D 9 via Vulkan (")
                            .append(gpu.optString("vulkan", "1.1+")).append(")\n\n");
                }

                text.append("GPU Renderer:\n• ").append(gpu.optString("gpu", "Unknown GPU")).append("\n\n");

                String driver = gpu.optString("driverName", "—").trim();
                String driverInfo = gpu.optString("driverInfo", "").trim();
                text.append("Driver:\n• ").append(driver);
                if (!driverInfo.isEmpty() && !driverInfo.equalsIgnoreCase(driver)) {
                    text.append(" (").append(driverInfo).append(")");
                }
                text.append("\n\n");

                GpuDrivers.Driver selected = GpuDrivers.selected(this);
                String selectedLabel = selected == null ? "System default" : selected.name;
                text.append("Active Driver Profile:\n• ").append(selectedLabel).append("\n\n");

                text.append("Features & Compatibility:\n");
                text.append("• BC Textures: ").append(gpu.optBoolean("bcTextures") ? "Hardware (GPU)" : "CPU emulated").append("\n");
                String missingVert = gpu.optString("vertexFormatsMissing");
                if (!missingVert.isEmpty()) {
                    text.append("• Unsupported vertex types: ").append(missingVert).append("\n");
                }
                text.append("• Status: ").append(gpu.optBoolean("compatible") ? "Compatible with NFSU2 Revived"
                        : "Missing features: " + gpu.optJSONArray("missing"));
            }

            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Driver Check")
                    .setMessage(text.toString())
                    .setPositiveButton("OK", null)
                    .setNeutralButton("Use system driver", (dialog, which) -> { GpuDrivers.select(this, GpuDrivers.SYSTEM_DRIVER_ID); refreshOptions(); })
                    .setNegativeButton(wantedToPlay ? "Play anyway" : null, wantedToPlay ? (dialog, which) -> launch() : null)
                    .show();
        } catch (Exception error) { message("Driver Check", "Invalid diagnostic: " + error.getMessage()); }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_DRIVER_PROBE) {
            showDriverReport(resultCode == RESULT_OK && data != null ? data.getStringExtra("gpu_report")
                    : "{\"probeError\":\"Test was closed or cancelled.\",\"driverLoadFailed\":true}");
        } else if (requestCode == REQUEST_DRIVER_ZIP && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri zip = data.getData();
            status.setText("Importing Vulkan driver…");
            worker.execute(() -> {
                try {
                    GpuDrivers.Driver driver = GpuDrivers.importZip(this, zip);
                    main.post(() -> {
                        GpuDrivers.select(this, driver.id); refresh();
                        status.setText("Driver imported: " + driver.name + ". Use Check driver to verify.");
                    });
                } catch (Exception error) {
                    main.post(() -> { refresh(); message("Vulkan Driver", "Could not import driver: " + error.getMessage()); });
                }
            });
        } else if (requestCode == REQUEST_CHOOSE_FOLDER && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri treeUri = data.getData();
            try {
                getContentResolver().takePersistableUriPermission(treeUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (Throwable ignored) {}
            String path = getPathFromTreeUri(this, treeUri);
            if (path != null) {
                File dir = resolveCustomPath(path, false);
                if (dir == null) dir = new File(path);
                setCustomGameRoot(dir);
                refresh();
            } else {
                message("Custom Location", "Could not resolve storage path from selected folder. Use Browse Folders or enter the path directly.");
            }
        }
    }

    // ---- Updates and reports ---------------------------------------------------------------------------

    private String updateChannelLabel() {
        return ReleaseUpdates.CHANNEL_BETA.equalsIgnoreCase(ReleaseUpdates.getChannel(this)) ? "Beta" : "Stable";
    }

    private void chooseUpdateChannel() {
        String[] options = {"Stable", "Beta"};
        int current = ReleaseUpdates.CHANNEL_BETA.equalsIgnoreCase(ReleaseUpdates.getChannel(this)) ? 1 : 0;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Update Channel")
                .setSingleChoiceItems(options, current, (dialog, which) -> {
                    String selected = (which == 1) ? ReleaseUpdates.CHANNEL_BETA : ReleaseUpdates.CHANNEL_STABLE;
                    ReleaseUpdates.setChannel(this, selected);
                    dialog.dismiss();
                    refreshOptions();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void checkUpdates(boolean manual) {
        if (checkingUpdates) return;
        checkingUpdates = true;
        if (optionsList != null) refreshOptions();
        ReleaseUpdates.check(this, manual, result -> {
            checkingUpdates = false;
            if (isFinishing() || isDestroyed()) return;
            refreshOptions();
            String channelName = updateChannelLabel();
            if (result.error != null) { if (manual) message("Updates", result.error); return; }
            boolean isBeta = ReleaseUpdates.CHANNEL_BETA.equalsIgnoreCase(ReleaseUpdates.getChannel(this));
            if (!ReleaseVersion.isUpdateAvailable(result.tag, ReleaseUpdates.installed(this), isBeta, result.isPrerelease)) {
                if (manual) message("Updates", "You have version " + ReleaseUpdates.installed(this)
                        + " (" + channelName + " channel).\nThere is no newer version on GitHub.");
                return;
            }
            android.content.SharedPreferences updates = ReleaseUpdates.prefs(this);
            if (!manual) {
                if (result.tag.equals(updates.getString("skip_update_tag", ""))) return;
                if (result.tag.equals(updateLaterTag)) return;
            }
            String releaseType = result.isPrerelease ? "Pre-Release" : "Release";
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("New " + releaseType + " available: " + result.tag)
                    .setMessage("Channel: " + channelName + "\n\nYou can download it from GitHub or keep playing with this version. Install the APK over "
                            + "the current one to preserve your saves and settings.")
                    .setPositiveButton("Update", (dialog, which) -> openUrl(Uri.parse(result.url)))
                    .setNeutralButton("Don't show again", (dialog, which) ->
                            updates.edit().putString("skip_update_tag", result.tag).apply())
                    .setNegativeButton("Later", (dialog, which) -> updateLaterTag = result.tag)
                    .show();
        });
    }

    /** Crash return to the launcher: write the ZIP without waiting for a tap. */
    private void autosaveDiagnostic(String crash) {
        status.setText("Saving diagnostic ZIP…");
        worker.execute(() -> {
            try {
                File report = Diagnostics.createReport(this);
                String location = Diagnostics.saveToDownloads(this, report);
                String sdcardLoc = Diagnostics.saveToSdcard(report);
                Uri issue = Diagnostics.issueUri(this, crash, location);
                main.post(() -> {
                    refresh();
                    String msg = crash + ".\n\nDiagnostic ZIP saved to:\n• " + location;
                    if (sdcardLoc != null) msg += "\n• " + sdcardLoc;
                    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle("The game crashed")
                            .setMessage(msg)
                            .setPositiveButton("Report on GitHub", (dialog, which) -> openUrl(issue))
                            .setNegativeButton("OK", null)
                            .show();
                });
            } catch (Exception error) {
                main.post(() -> {
                    refresh();
                    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle("The game crashed")
                            .setMessage(crash + ".\n\nCould not save the diagnostic ZIP: " + error.getMessage())
                            .setPositiveButton("Report on GitHub", (dialog, which) -> prepareReport(crash))
                            .setNegativeButton("OK", null)
                            .show();
                });
            }
        });
    }

    /**
     * Builds the report ZIP, saves it to Download/NFSU2 and /sdcard/NFSU2, and opens a pre-filled GitHub issue.
     */
    private void prepareReport(String crash) {
        status.setText("Preparing log…");
        worker.execute(() -> {
            try {
                File report = Diagnostics.createReport(this);
                String location = Diagnostics.saveToDownloads(this, report);
                Diagnostics.saveToSdcard(report);
                Uri issue = Diagnostics.issueUri(this, crash, location);
                main.post(() -> {
                    refresh();
                    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle("Report on GitHub")
                            .setMessage("The diagnostic log has been saved to:\n" + location
                                    + "\nand /sdcard/NFSU2/" + report.getName()
                                    + "\n\nA GitHub issue will open on mininxd/NFSU2-Revived-Android with a pre-filled technical summary. Describe what happened and, "
                                    + "if possible, attach that ZIP (GitHub does not allow attaching files automatically).")
                            .setPositiveButton("Open GitHub", (dialog, which) -> openUrl(issue))
                            .setNeutralButton("Save ZIP Only", null)
                            .setNegativeButton("Cancel", null).show();
                });
            } catch (Exception error) {
                main.post(() -> { refresh(); message("Report an issue", "Could not prepare log: " + error.getMessage()); });
            }
        });
    }

    private void saveDiagnosticZip(String crash) {
        status.setText("Saving diagnostic ZIP…");
        worker.execute(() -> {
            try {
                File report = Diagnostics.createReport(this);
                String location = Diagnostics.saveToDownloads(this, report);
                String sdcardLoc = Diagnostics.saveToSdcard(report);
                main.post(() -> {
                    refresh();
                    String msg = "Diagnostic ZIP successfully saved to:\n• " + location;
                    if (sdcardLoc != null) msg += "\n• " + sdcardLoc;
                    msg += "\n\nYou can attach or send this file for troubleshooting.";
                    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle("Diagnostic Saved")
                            .setMessage(msg)
                            .setPositiveButton("OK", null)
                            .show();
                });
            } catch (Exception error) {
                main.post(() -> { refresh(); message("Save Diagnostic", "Could not save diagnostic: " + error.getMessage()); });
            }
        });
    }

    private void showReportDialog(String crash) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(crash != null ? "The game crashed" : "Diagnostics & Bug Report")
                .setMessage(crash != null
                        ? crash + "\n\nChoose an option below to report on GitHub or save the diagnostic ZIP file."
                        : "Generate diagnostic logs, crash traces, and system specs to report an issue or save locally.")
                .setPositiveButton("Report on GitHub", (dialog, which) -> prepareReport(crash))
                .setNeutralButton("Save Diagnostic ZIP", (dialog, which) -> saveDiagnosticZip(crash))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void openUrl(Uri uri) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, uri)); }
        catch (android.content.ActivityNotFoundException error) { message("GitHub", "No web browser found to open " + uri); }
    }

    // ---- Launch ----------------------------------------------------------------------------------------

    private void play() {
        launch();
    }

    private void launch() {
        // Each launch uses a fresh guest machine in the dedicated :game process.
        android.app.ActivityManager manager = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        List<android.app.ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes != null) for (android.app.ActivityManager.RunningAppProcessInfo process : processes)
            if (process.uid == android.os.Process.myUid() && process.processName.equals(getPackageName() + ":game"))
                android.os.Process.killProcess(process.pid);
        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int[] size = GameOptions.renderSize(this, Math.max(metrics.widthPixels, metrics.heightPixels),
                Math.min(metrics.widthPixels, metrics.heightPixels));
        String[] loader = GpuDrivers.loaderArguments(this);
        ensureScriptsInstalled(gameRoot());
        GameOptions.applyWidescreenPatchFile(this);
        GameOptions.syncWidescreenIni(this);
        GameOptions.syncExtraOptionsIni(this);
        GamePerformanceBooster.apply(this, getWindow());
        PerformanceCores.bindToPerformanceCores();
        boolean logsEnabled = !"off".equals(GameOptions.get(this, GameOptions.DISPLAY_LOGS));
        if (GameOptions.usesWidescreenPatch(this)) GameOptions.syncWidescreenHudDat(this, logsEnabled);
        startActivity(new Intent(this, GameActivity.class)
                .putExtra("gameRoot", gameRoot(this).getAbsolutePath())
                .putExtra("language", GameOptions.language(this))
                .putExtra("renderWidth", size[0]).putExtra("renderHeight", size[1])
                .putExtra("resolutionPercent", GameOptions.get(this, GameOptions.RESOLUTION))
                .putExtra("frameCap", Integer.parseInt(GameOptions.get(this, GameOptions.FPS)))
                .putExtra("widescreen", GameOptions.get(this, GameOptions.WIDESCREEN).equals("on"))
                .putExtra("widescreenMode", GameOptions.widescreenMode(this))
                .putExtra("minimapTop", GameOptions.get(this, GameOptions.MINIMAP).equals("top"))
                .putExtra("backBuffers", 2)        // default latency (hidden option)
                .putExtra("cpuTextures", false)    // default GPU textures (hidden option)
                .putExtra("region", GameOptions.regionCode(this))
                .putExtra("speedometer", GameOptions.speedometerPosition(this))
                .putExtra("consolePad", GameOptions.isConsolePad(this))
                .putExtra("onlineServer", GameOptions.onlineServer(this))
                .putExtra("driverLoader", loader)
                .putExtra("graphicsBackend", GameOptions.getEffectiveGraphicsBackend(this))
                .putExtra("displayLogs", GameOptions.get(this, GameOptions.DISPLAY_LOGS))
                .putExtra("roamEnabled", GameOptions.isRoamEnabled(this))
                .putExtra("roamServer", GameOptions.roamServer(this))
                .putExtra("roamRoom", GameOptions.roamRoom(this))
                .putExtra("roamName", GameOptions.roamName(this))
                .putExtra("roamSolid", GameOptions.isRoamSolid(this))
                .putExtra("roamCrew", GameOptions.roamCrew(this))
                .putStringArrayListExtra("cheats", GameOptions.getActiveCheatCodes(this)));
    }

    // ---- Presets & Custom Location --------------------------------------------------------------------

    private Button presetButton(String title, String presetId) {
        Button b = new Button(this);
        b.setText(title);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        b.setPadding(dp(2), 0, dp(2), 0);
        b.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD_ITALIC));
        b.setOnClickListener(v -> applyPreset(presetId));
        b.setStateListAnimator(null);
        return b;
    }

    private void updatePresetButtons(String activePreset) {
        if (btnPresetPerformance == null || btnPresetBalanced == null || btnPresetOriginal == null || btnPresetCustom == null) return;
        setPresetButtonState(btnPresetPerformance, "performance".equalsIgnoreCase(activePreset));
        setPresetButtonState(btnPresetBalanced, "balanced".equalsIgnoreCase(activePreset));
        setPresetButtonState(btnPresetOriginal, "original".equalsIgnoreCase(activePreset));
        setPresetButtonState(btnPresetCustom, "custom".equalsIgnoreCase(activePreset));
    }

    private void setPresetButtonState(Button b, boolean active) {
        b.setTextColor(active ? 0xFF0D1A04 : Color.WHITE);
        GradientDrawable bg = new GradientDrawable();
        if (active) {
            bg.setColor(ACCENT);
        } else {
            bg.setColor(BG_FIELD);
            bg.setStroke(dp(1), STROKE_FIELD);
        }
        bg.setCornerRadius(dp(8));
        b.setBackground(bg);
    }

    private void saveCustomPreset() {
        android.content.SharedPreferences sp = GameOptions.prefs(this);
        sp.edit()
                .putString("custom_resolution", GameOptions.get(this, GameOptions.RESOLUTION))
                .putString("custom_draw_distance", GameOptions.get(this, GameOptions.DRAW_DISTANCE))
                .putString("custom_graphics_backend", GameOptions.get(this, GameOptions.GRAPHICS_BACKEND))
                .putString("custom_fps", GameOptions.get(this, GameOptions.FPS))
                .putBoolean("has_custom_preset", true)
                .putString("active_preset", "custom")
                .apply();
        updatePresetButtons("custom");
    }

    private void applyPreset(String preset) {
        android.content.SharedPreferences sp = GameOptions.prefs(this);
        android.content.SharedPreferences.Editor ed = sp.edit();
        ed.putString("active_preset", preset);
        if ("performance".equalsIgnoreCase(preset)) {
            ed.putString(GameOptions.DRAW_DISTANCE, "performance");
            ed.putString(GameOptions.RESOLUTION, "50");
        } else if ("balanced".equalsIgnoreCase(preset)) {
            ed.putString(GameOptions.DRAW_DISTANCE, "extended");
            ed.putString(GameOptions.RESOLUTION, "70");
        } else if ("original".equalsIgnoreCase(preset)) {
            ed.putString(GameOptions.DRAW_DISTANCE, "original");
            ed.putString(GameOptions.RESOLUTION, "100");
        } else if ("custom".equalsIgnoreCase(preset)) {
            if (sp.getBoolean("has_custom_preset", false)) {
                String cRes = sp.getString("custom_resolution", null);
                if (cRes != null) ed.putString(GameOptions.RESOLUTION, cRes);
                String cDraw = sp.getString("custom_draw_distance", null);
                if (cDraw != null) ed.putString(GameOptions.DRAW_DISTANCE, cDraw);
                String cBackend = sp.getString("custom_graphics_backend", null);
                if (cBackend != null) ed.putString(GameOptions.GRAPHICS_BACKEND, cBackend);
                String cFps = sp.getString("custom_fps", null);
                if (cFps != null) ed.putString(GameOptions.FPS, cFps);
            } else {
                ed.putString("custom_resolution", GameOptions.get(this, GameOptions.RESOLUTION));
                ed.putString("custom_draw_distance", GameOptions.get(this, GameOptions.DRAW_DISTANCE));
                ed.putString("custom_graphics_backend", GameOptions.get(this, GameOptions.GRAPHICS_BACKEND));
                ed.putString("custom_fps", GameOptions.get(this, GameOptions.FPS));
                ed.putBoolean("has_custom_preset", true);
            }
        }
        ed.apply();
        updatePresetButtons(preset);
        refreshOptions();
    }

    private String locationLabel() {
        File root = gameRoot(this);
        if (root == null) return "Default";
        String path = root.getAbsolutePath();
        String ext = Environment.getExternalStorageDirectory().getAbsolutePath();
        if (path.startsWith(ext)) {
            String rel = path.substring(ext.length()).replaceAll("^/+", "");
            return rel.isEmpty() ? "/sdcard" : rel;
        }
        return path;
    }

    private void showChooseLocationDialog() {
        File current = gameRoot(this);
        android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setText(current.getAbsolutePath());
        input.setSelection(input.getText().length());

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(18);
        layout.setPadding(pad, dp(10), pad, dp(4));

        TextView hint = label("Enter folder path or tap Browse to select from file manager:", 12, TEXT_MUTED, false);
        hint.setPadding(0, 0, 0, dp(6));
        layout.addView(hint);
        layout.addView(input);

        Button browseBtn = actionButton("Browse Folders…", false);
        LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(-1, dp(40));
        btnLp.topMargin = dp(8);
        layout.addView(browseBtn, btnLp);

        AlertDialog dialog = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Select Game Folder")
                .setView(layout)
                .setPositiveButton("Save", (d, which) -> {
                    String p = input.getText().toString().trim();
                    if (!p.isEmpty()) {
                        File f = resolveCustomPath(p, false);
                        if (f == null) f = new File(p);
                        setCustomGameRoot(f);
                        refresh();
                    }
                })
                .setNeutralButton("Reset Default", (d, which) -> {
                    GameOptions.prefs(this).edit().remove(GameOptions.CUSTOM_GAME_DIR).commit();
                    sCustomGamePath = null;
                    refresh();
                })
                .setNegativeButton("Cancel", null)
                .create();

        browseBtn.setOnClickListener(v -> {
            dialog.dismiss();
            if (allowed()) {
                File start = (current != null && current.isDirectory()) ? current : Environment.getExternalStorageDirectory();
                showInAppFolderBrowser(start);
            } else {
                launchFolderPicker();
            }
        });

        dialog.show();
    }

    private List<File> listStorageRoots() {
        List<File> roots = new ArrayList<>();
        File ext = Environment.getExternalStorageDirectory();
        if (ext != null && ext.isDirectory()) roots.add(ext);
        File dl = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (dl != null && dl.isDirectory()) roots.add(dl);
        File storageDir = new File("/storage");
        File[] volumes = storageDir.listFiles();
        if (volumes != null) {
            for (File v : volumes) {
                String n = v.getName();
                if (!v.isDirectory() || "emulated".equals(n) || "self".equals(n) || "enc_emulated".equals(n)) continue;
                if (v.canRead() && v.listFiles() != null) roots.add(v);
            }
        }
        return roots;
    }

    private void showInAppFolderBrowser(File startDir) {
        final File ext = Environment.getExternalStorageDirectory();
        File initial = (startDir != null && startDir.isDirectory() && startDir.canRead()) ? startDir : ext;
        final File[] currentDir = { initial };

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(12), dp(8), dp(12), dp(4));

        // Quick storage root buttons (Internal, Downloads, SD cards)
        List<File> storageRoots = listStorageRoots();
        android.widget.HorizontalScrollView chipsScroll = new android.widget.HorizontalScrollView(this);
        chipsScroll.setHorizontalScrollBarEnabled(false);
        LinearLayout chipsRow = new LinearLayout(this);
        chipsRow.setOrientation(LinearLayout.HORIZONTAL);
        chipsScroll.addView(chipsRow);
        root.addView(chipsScroll, new LinearLayout.LayoutParams(-1, -2));

        TextView pathView = label("", 12, ACCENT, true);
        pathView.setPadding(dp(4), dp(8), dp(4), dp(2));
        root.addView(pathView);

        TextView exeBadge = label("", 11, TEXT_MUTED, false);
        exeBadge.setPadding(dp(4), 0, dp(4), dp(6));
        root.addView(exeBadge);

        android.widget.ListView listView = new android.widget.ListView(this);
        listView.setDividerHeight(1);
        LinearLayout.LayoutParams listLp = new LinearLayout.LayoutParams(-1, dp(260));
        root.addView(listView, listLp);

        final List<File> entryTargets = new ArrayList<>();
        final List<String> entryLabels = new ArrayList<>();
        android.widget.ArrayAdapter<String> adapter = new android.widget.ArrayAdapter<String>(
                this, android.R.layout.simple_list_item_1, entryLabels) {
            @Override
            public View getView(int position, View convertView, android.view.ViewGroup parent) {
                View v = super.getView(position, convertView, parent);
                TextView tv = v.findViewById(android.R.id.text1);
                if (tv != null) {
                    String txt = getItem(position);
                    tv.setTextColor(txt != null && txt.contains("[SPEED2.EXE]") ? COLOR_OK : Color.WHITE);
                    tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
                }
                return v;
            }
        };
        listView.setAdapter(adapter);

        final Runnable[] reloadRef = new Runnable[1];
        reloadRef[0] = () -> {
            File dir = currentDir[0];
            if (dir == null || !dir.isDirectory()) dir = ext;
            currentDir[0] = dir;
            pathView.setText(dir.getAbsolutePath());
            boolean hasExe = isGameExecutable(dir);
            exeBadge.setText(hasExe
                    ? "SPEED2.EXE found in this folder"
                    : "Tap a folder to open it, or tap Use This Folder");
            exeBadge.setTextColor(hasExe ? COLOR_OK : TEXT_MUTED);

            entryTargets.clear();
            entryLabels.clear();

            File parent = dir.getParentFile();
            if (parent != null && parent.isDirectory() && parent.canRead()
                    && !"/".equals(dir.getAbsolutePath())) {
                entryTargets.add(parent);
                entryLabels.add("..  (Parent folder)");
            }

            File[] children = dir.listFiles();
            if (children != null) {
                List<File> dirs = new ArrayList<>();
                for (File c : children) {
                    if (c.isDirectory() && !c.getName().startsWith(".")) dirs.add(c);
                }
                java.util.Collections.sort(dirs, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
                for (File d : dirs) {
                    entryTargets.add(d);
                    entryLabels.add(isGameExecutable(d) ? d.getName() + "/   [SPEED2.EXE]" : d.getName() + "/");
                }
            }
            adapter.notifyDataSetChanged();
            listView.setSelection(0);
        };

        for (File sr : storageRoots) {
            String chipLabel;
            if (ext != null && sr.getAbsolutePath().equals(ext.getAbsolutePath())) chipLabel = "Internal";
            else if (sr.getName().equalsIgnoreCase("Download") || sr.getName().equalsIgnoreCase("Downloads")) chipLabel = "Downloads";
            else chipLabel = sr.getName();
            Button chip = actionButton(chipLabel, false);
            chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
            chip.setPadding(dp(10), dp(4), dp(10), dp(4));
            chip.setOnClickListener(v -> {
                currentDir[0] = sr;
                reloadRef[0].run();
            });
            LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(-2, dp(32));
            clp.rightMargin = dp(6);
            chipsRow.addView(chip, clp);
        }

        listView.setOnItemClickListener((parent, view, position, id) -> {
            if (position >= 0 && position < entryTargets.size()) {
                File target = entryTargets.get(position);
                if (target != null && target.isDirectory()) {
                    currentDir[0] = target;
                    reloadRef[0].run();
                }
            }
        });

        reloadRef[0].run();

        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Browse Game Folder")
                .setView(root)
                .setPositiveButton("Use This Folder", (d, which) -> {
                    setCustomGameRoot(currentDir[0]);
                    refresh();
                })
                .setNeutralButton("System Picker", (d, which) -> launchFolderPicker())
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void launchFolderPicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_CHOOSE_FOLDER);
        } catch (Exception e) {
            message("Folder Picker", "Could not launch folder picker: " + e.getMessage());
        }
    }

    public static boolean isGameExecutable(File root) {
        if (root == null || !root.isDirectory()) return false;
        return new File(root, "SPEED2.EXE").isFile()
                || new File(root, "speed2.exe").isFile()
                || new File(root, "Speed2.exe").isFile()
                || findSubFile(root, "speed2.exe") != null;
    }

    public static File findGameRootInDir(File dir) {
        if (dir == null) return null;
        if (isGameExecutable(dir)) return dir;

        for (String subName : new String[]{"nfsu2", "NFSU2", "NFS Underground 2", "Need for Speed Underground 2", "Need For Speed Underground 2"}) {
            File sub = new File(dir, subName);
            if (isGameExecutable(sub)) return sub;
        }

        if (dir.isDirectory()) {
            File[] children = dir.listFiles();
            if (children != null) {
                for (File child : children) {
                    if (child.isDirectory() && isGameExecutable(child)) {
                        return child;
                    }
                }
            }
        }
        return dir;
    }

    private static File findSubDir(File parent, String name) {
        if (parent == null || !parent.isDirectory()) return null;
        File direct = new File(parent, name);
        if (direct.isDirectory()) return direct;
        File[] files = parent.listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.isDirectory() && f.getName().equalsIgnoreCase(name)) return f;
            }
        }
        return null;
    }

    private static File findSubFile(File parent, String name) {
        if (parent == null || !parent.isDirectory()) return null;
        File direct = new File(parent, name);
        if (direct.isFile()) return direct;
        File[] files = parent.listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.isFile() && f.getName().equalsIgnoreCase(name)) return f;
            }
        }
        return null;
    }

    private static File resolveCaseInsensitive(File base, String relPath) {
        if (base == null || !base.isDirectory() || relPath == null) return null;
        String[] parts = relPath.split("/+");
        File current = base;
        for (String part : parts) {
            if (part.isEmpty() || ".".equals(part)) continue;
            if ("..".equals(part)) {
                current = current.getParentFile();
                if (current == null) return null;
                continue;
            }
            File direct = new File(current, part);
            if (direct.exists()) {
                current = direct;
                continue;
            }
            File[] files = current.listFiles();
            if (files == null) return null;
            File found = null;
            for (File f : files) {
                if (f.getName().equalsIgnoreCase(part)) {
                    found = f;
                    break;
                }
            }
            if (found == null) {
                if ("download".equalsIgnoreCase(part)) {
                    for (File f : files) {
                        if ("downloads".equalsIgnoreCase(f.getName())) { found = f; break; }
                    }
                } else if ("downloads".equalsIgnoreCase(part)) {
                    for (File f : files) {
                        if ("download".equalsIgnoreCase(f.getName())) { found = f; break; }
                    }
                }
            }
            if (found == null) return null;
            current = found;
        }
        return current;
    }

    public static File resolveCustomPath(String rawPath) {
        return resolveCustomPath(rawPath, false);
    }

    public static File resolveCustomPath(String rawPath, boolean autoDescend) {
        if (rawPath == null) return null;
        String p = rawPath.trim();
        if (p.isEmpty()) return null;

        File direct = new File(p);
        if (direct.exists() && direct.isDirectory()) {
            return autoDescend ? findGameRootInDir(direct) : direct;
        }

        File ext = Environment.getExternalStorageDirectory();

        if (p.startsWith("/sdcard/") || p.equals("/sdcard")) {
            String rel = p.length() > 7 ? p.substring(7).replaceAll("^/+", "") : "";
            File sub = rel.isEmpty() ? ext : new File(ext, rel);
            if (sub.exists() && sub.isDirectory()) return autoDescend ? findGameRootInDir(sub) : sub;
            File match = resolveCaseInsensitive(ext, rel);
            if (match != null && match.isDirectory()) return autoDescend ? findGameRootInDir(match) : match;
            return sub;
        }

        String emulatedPrefix = "/storage/emulated/0";
        if (p.startsWith(emulatedPrefix)) {
            String rel = p.substring(emulatedPrefix.length()).replaceAll("^/+", "");
            File sub = rel.isEmpty() ? ext : new File(ext, rel);
            if (sub.exists() && sub.isDirectory()) return autoDescend ? findGameRootInDir(sub) : sub;
            File match = resolveCaseInsensitive(ext, rel);
            if (match != null && match.isDirectory()) return autoDescend ? findGameRootInDir(match) : match;
            return sub;
        }

        String stripped = p.replaceAll("^/+", "");
        File underExt = new File(ext, stripped);
        if (underExt.exists() && underExt.isDirectory()) {
            return autoDescend ? findGameRootInDir(underExt) : underExt;
        }

        File match = resolveCaseInsensitive(ext, stripped);
        if (match != null && match.isDirectory()) {
            return autoDescend ? findGameRootInDir(match) : match;
        }

        if (stripped.toLowerCase(Locale.ROOT).startsWith("download/") || stripped.toLowerCase(Locale.ROOT).startsWith("downloads/")) {
            int slash = stripped.indexOf('/');
            String subRel = slash >= 0 ? stripped.substring(slash + 1) : "";
            for (String dlName : new String[]{"Download", "download", "Downloads", "downloads"}) {
                File dlBase = new File(ext, dlName);
                if (dlBase.isDirectory()) {
                    File subFile = new File(dlBase, subRel);
                    if (subFile.exists() && subFile.isDirectory()) return autoDescend ? findGameRootInDir(subFile) : subFile;
                    File dlMatch = resolveCaseInsensitive(dlBase, subRel);
                    if (dlMatch != null && dlMatch.isDirectory()) return autoDescend ? findGameRootInDir(dlMatch) : dlMatch;
                }
            }
        }

        if (!p.startsWith("/storage/") && !p.startsWith("/mnt/")) {
            return underExt;
        }
        return direct;
    }

    private void setCustomGameRoot(File dir) {
        if (dir == null) return;
        // Preserve the exact folder selected by the user; never hijack back to /storage/emulated/0/nfsu2.
        File actual = dir;
        if (!actual.exists()) {
            try { actual.mkdirs(); } catch (Throwable ignored) {}
        }
        String abs = actual.getAbsolutePath();
        GameOptions.prefs(this).edit().putString(GameOptions.CUSTOM_GAME_DIR, abs).commit();
        sCustomGamePath = abs;
        android.util.Log.i("NFSU2", "Custom game root set to: " + abs);
    }

    private static String getPathFromTreeUri(Context context, Uri uri) {
        if (uri == null) return null;
        try {
            String docId = null;
            if (Build.VERSION.SDK_INT >= 21) {
                try {
                    docId = DocumentsContract.getTreeDocumentId(uri);
                } catch (Throwable ignored) {}
            }
            if (docId == null) {
                try {
                    docId = DocumentsContract.getDocumentId(uri);
                } catch (Throwable ignored) {}
            }
            if (docId == null) docId = uri.getPath();
            if (docId == null) return null;

            try {
                docId = java.net.URLDecoder.decode(docId, "UTF-8");
            } catch (Throwable ignored) {}

            if (docId.startsWith("raw:")) {
                String raw = docId.substring(4);
                if (new File(raw).exists()) return raw;
                File resolved = resolveCustomPath(raw, false);
                if (resolved != null && resolved.exists()) return resolved.getAbsolutePath();
                return raw;
            }

            String cleanedDocId = docId;
            if (cleanedDocId.startsWith("/tree/")) cleanedDocId = cleanedDocId.substring(6);
            else if (cleanedDocId.startsWith("tree/")) cleanedDocId = cleanedDocId.substring(5);
            if (cleanedDocId.startsWith("/document/")) cleanedDocId = cleanedDocId.substring(10);
            else if (cleanedDocId.startsWith("document/")) cleanedDocId = cleanedDocId.substring(9);

            int colon = cleanedDocId.indexOf(':');
            String type = colon >= 0 ? cleanedDocId.substring(0, colon) : cleanedDocId;
            String rel = colon >= 0 ? cleanedDocId.substring(colon + 1) : "";

            File ext = Environment.getExternalStorageDirectory();

            if ("primary".equalsIgnoreCase(type) || type.toLowerCase(Locale.ROOT).endsWith("primary")) {
                File target = rel.isEmpty() ? ext : new File(ext, rel);
                if (target.exists()) return target.getAbsolutePath();
                File resolved = resolveCustomPath(rel, false);
                if (resolved != null && resolved.exists()) return resolved.getAbsolutePath();
                return target.getAbsolutePath();
            }

            if ("downloads".equalsIgnoreCase(type) || "download".equalsIgnoreCase(type)
                    || "msf".equalsIgnoreCase(type) || "msd".equalsIgnoreCase(type)) {
                File dlDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
                if ("msf".equalsIgnoreCase(type) || "msd".equalsIgnoreCase(type)) {
                    // Android 10+ DownloadsProvider uses msd:<media_id> for folders; query its real path or display name.
                    if (context != null && Build.VERSION.SDK_INT >= 21) {
                        try {
                            Uri docUri = DocumentsContract.buildDocumentUriUsingTree(uri, docId);
                            try (android.database.Cursor cur = context.getContentResolver().query(
                                    docUri,
                                    new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME, "_data" },
                                    null, null, null)) {
                                if (cur != null && cur.moveToFirst()) {
                                    int dataIdx = cur.getColumnIndex("_data");
                                    if (dataIdx >= 0) {
                                        String dataPath = cur.getString(dataIdx);
                                        if (dataPath != null && !dataPath.isEmpty() && new File(dataPath).isDirectory()) {
                                            return dataPath;
                                        }
                                    }
                                    int nameIdx = cur.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                                    if (nameIdx >= 0) {
                                        String disp = cur.getString(nameIdx);
                                        if (disp != null && !disp.isEmpty()) {
                                            File underDl = new File(dlDir, disp);
                                            if (underDl.isDirectory()) return underDl.getAbsolutePath();
                                            File matchDl = resolveCaseInsensitive(dlDir, disp);
                                            if (matchDl != null && matchDl.isDirectory()) return matchDl.getAbsolutePath();
                                        }
                                    }
                                }
                            }
                        } catch (Throwable ignored) {}
                    }
                    return dlDir.getAbsolutePath();
                }
                if (rel.isEmpty()) return dlDir.getAbsolutePath();
                File target = new File(dlDir, rel);
                if (target.exists()) return target.getAbsolutePath();
                File resolved = resolveCaseInsensitive(dlDir, rel);
                if (resolved != null && resolved.exists()) return resolved.getAbsolutePath();
                return target.getAbsolutePath();
            }

            File extDir = new File("/storage/" + type + (rel.isEmpty() ? "" : "/" + rel));
            if (extDir.exists() || extDir.isDirectory()) {
                return extDir.getAbsolutePath();
            }
            File mntDir = new File("/mnt/media_rw/" + type + (rel.isEmpty() ? "" : "/" + rel));
            if (mntDir.exists() || mntDir.isDirectory()) {
                return mntDir.getAbsolutePath();
            }

            File fallback = resolveCustomPath(rel.isEmpty() ? cleanedDocId : rel, false);
            if (fallback != null && fallback.exists()) {
                return fallback.getAbsolutePath();
            }
        } catch (Throwable t) {
            android.util.Log.w("NFSU2", "Failed to resolve tree uri: " + uri, t);
        }
        return null;
    }

    // ---- Storage ---------------------------------------------------------------------------------------

    public static File gameRoot(Context context) {
        String custom = null;
        if (context != null) {
            custom = GameOptions.prefs(context).getString(GameOptions.CUSTOM_GAME_DIR, null);
        }
        if ((custom == null || custom.trim().isEmpty()) && sCustomGamePath != null && !sCustomGamePath.trim().isEmpty()) {
            custom = sCustomGamePath;
        }
        if (custom != null && !custom.trim().isEmpty()) {
            // Always honor an explicit custom folder instead of falling back to /storage/emulated/0/nfsu2.
            File resolved = resolveCustomPath(custom.trim(), false);
            return resolved != null ? resolved : new File(custom.trim());
        }

        File ext = Environment.getExternalStorageDirectory();
        File[] candidates = new File[] {
            new File(ext, "nfsu2"),
            new File(ext, "NFSU2"),
            new File(ext, "Download/nfsu2"),
            new File(ext, "Download/NFSU2"),
            new File(ext, "Downloads/nfsu2"),
            new File(ext, "Downloads/NFSU2")
        };

        for (File candidate : candidates) {
            if (isGameExecutable(candidate)) return candidate;
        }

        for (File candidate : candidates) {
            if (candidate.isDirectory()) {
                String[] list = candidate.list();
                if (list != null && list.length > 0) return candidate;
            }
        }

        if (new File(ext, "nfsu2").isDirectory()) return new File(ext, "nfsu2");
        if (new File(ext, "NFSU2").isDirectory()) return new File(ext, "NFSU2");

        return new File(ext, "nfsu2");
    }

    public static File gameRoot() {
        return gameRoot(sAppContext);
    }

    private boolean allowed() {
        return Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
            : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30)
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        else requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
    }

    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        refresh();
    }

    @Override protected void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }

    // ---- Widgets ---------------------------------------------------------------------------------------

    private View optionRow(String title, String value, Runnable onClick) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(12), dp(9), dp(12), dp(9));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(BG_FIELD);
        bg.setStroke(dp(1), STROKE_FIELD);
        bg.setCornerRadius(dp(10));
        row.setBackground(bg);
        row.setClickable(true);
        row.setOnClickListener(v -> onClick.run());
        row.addView(label(title, 14, Color.WHITE, false), new LinearLayout.LayoutParams(0, -2, 1f));
        TextView current = label(value + "  ›", 13, ACCENT, true);
        current.setGravity(Gravity.END);
        row.addView(current, new LinearLayout.LayoutParams(0, -2, 1f));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(5);
        row.setLayoutParams(lp);
        return row;
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(12), dp(16), dp(12));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(BG_PANEL);
        bg.setCornerRadius(dp(14));
        bg.setStroke(dp(1), STROKE_PANEL);
        card.setBackground(bg);
        return card;
    }

    private Button actionButton(String text, boolean primary) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(primary ? 0xFF0D1A04 : Color.WHITE);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, primary ? 20 : 14);
        b.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD_ITALIC));
        b.setLetterSpacing(primary ? .10f : 0f);
        GradientDrawable bg = new GradientDrawable();
        if (primary) {
            bg.setColor(ACCENT);
        } else {
            bg.setColor(BG_FIELD);
            bg.setStroke(dp(1), 0xFF444444);
        }
        bg.setCornerRadius(dp(12));
        b.setBackground(bg);
        b.setStateListAnimator(null);
        return b;
    }

    private TextView label(String text, int sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) {
            t.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD_ITALIC));
            t.setLetterSpacing(.05f);
        }
        return t;
    }

    private void message(String title, String text) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(title).setMessage(text).setPositiveButton("OK", null).show();
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
