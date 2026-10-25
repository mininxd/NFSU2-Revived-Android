package io.mininxd.nfsu2;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
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
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Launcher in the style of the MW and Carbon ports: game files and PLAY on the left, options on the
 * right (Vulkan driver, resolution, frame cap, widescreen, compatibility).
 */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_DRIVER_ZIP = 45, REQUEST_DRIVER_PROBE = 46;
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
        {"繁體中文", "Chinese (Traditional)", "Chinese"}, {"ไทย", "Thai", "Thailand"}
    };

    private LinearLayout checksList, optionsList;
    private TextView status;
    private Button play, access;
    private boolean playAfterProbe, checkingUpdates, resumed;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        setContentView(buildScreen());
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
                    openUrl(Uri.parse("https://github.com/mininxd/nfsu2-recompiled/releases/tag/v" + Uri.encode(version)));
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
        String crash = Diagnostics.unreportedCrash(this);
        if (crash != null) {
            Diagnostics.acknowledgeCrashes(this);
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("The game crashed")
                    .setMessage(crash + ".\n\nYou can report this issue on GitHub or save the diagnostic ZIP file to your device.")
                    .setPositiveButton("Report on GitHub", (dialog, which) -> prepareReport(crash))
                    .setNeutralButton("Save Diagnostic ZIP", (dialog, which) -> saveDiagnosticZip(crash))
                    .setNegativeButton("Not now", null).show();
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
        checksList = new LinearLayout(this);
        checksList.setOrientation(LinearLayout.VERTICAL);
        card.addView(checksList);

        play = actionButton("PLAY", true);
        play.setOnClickListener(view -> play());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(-1, dp(54));
        playParams.topMargin = dp(8);
        left.addView(play, playParams);

        access = actionButton("Allow access to internal storage/nfsu2", false);
        access.setOnClickListener(view -> requestAccess());
        LinearLayout.LayoutParams accessParams = new LinearLayout.LayoutParams(-1, dp(46));
        accessParams.topMargin = dp(10);
        left.addView(access, accessParams);

        // Right: options.
        LinearLayout right = card();
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, -1, 1f);
        rightParams.leftMargin = dp(24);
        columns.addView(right, rightParams);
        right.addView(label("SETTINGS & MULTIPLAYER", 12, ACCENT, true));
        TextView note = label("Applied when pressing PLAY.", 12, TEXT_HINT, false);
        note.setPadding(0, dp(2), 0, dp(4));
        right.addView(note);
        ScrollView scroll = new ScrollView(this);
        optionsList = new LinearLayout(this);
        optionsList.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(optionsList);
        right.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));
        return screen;
    }

    private void refresh() {
        boolean allowed = allowed();
        File root = gameRoot();
        checksList.removeAllViews();
        boolean exe = allowed && new File(root, "SPEED2.EXE").isFile();
        addCheck("SPEED2.EXE", exe, true);
        StringBuilder missing = new StringBuilder();
        for (String folder : new String[] {"GLOBAL", "TRACKS", "CARS", "FRONTEND", "LANGUAGES"})
            if (!(allowed && new File(root, folder).isDirectory())) missing.append(missing.length() == 0 ? "" : ", ").append(folder);
        addCheck(missing.length() == 0 ? "Game folders" : "Missing folders: " + missing, missing.length() == 0, true);
        addCheck("Widescreen HUD (scripts/…WidescreenFix.dat)",
                allowed && new File(root, "scripts/NFSUnderground2.WidescreenFix.dat").isFile(), false);
        boolean ready = exe && !languages().isEmpty();
        status.setText(!allowed ? "Allow file access to read internal storage/nfsu2."
                : ready ? "Internal storage/nfsu2" : "Copy your game files to internal storage/nfsu2.");
        play.setEnabled(ready);
        play.setAlpha(ready ? 1f : .45f);
        access.setVisibility(allowed ? View.GONE : View.VISIBLE);
        refreshOptions();
    }

    private void refreshOptions() {
        optionsList.removeAllViews();
        GpuDrivers.Driver driver = GpuDrivers.selected(this);
        // Custom drivers (libadrenotools) exist only in the 64-bit build.
        if (android.os.Process.is64Bit())
            optionsList.addView(optionRow("Vulkan Driver", driver == null ? "System default" : driver.name, this::chooseDriver));
        optionsList.addView(optionRow("Test driver", "GPU and Vulkan features", () -> probeDriver(false)));
        optionsList.addView(optionRow("Language", languageLabel(), this::chooseLanguage));
        optionsList.addView(optionRow("Reset graphics cache", "Regenerate shaders", () -> GraphicsCache.prompt(this)));
        for (GameOptions.Option option : GameOptions.ALL)
            optionsList.addView(optionRow(option.title, option.label(GameOptions.get(this, option.key)), () -> chooseOption(option)));
        optionsList.addView(optionRow("Cheat Codes", cheatCodesSummary(), this::chooseCheatCodes));
        optionsList.addView(optionRow("Free Roam (Multiplayer)", freeRoamSummary(), this::chooseFreeRoam));
        optionsList.addView(optionRow("Online Server (EA)", GameOptions.onlineServer(this), this::chooseOnlineServer));
        optionsList.addView(optionRow("Check for updates",
                checkingUpdates ? "Checking GitHub…" : "Version " + ReleaseUpdates.installed(this), () -> checkUpdates(true)));
        optionsList.addView(optionRow("Report an issue", "Report on GitHub / Save ZIP", () -> showReportDialog(null)));
    }

    // ---- Options ---------------------------------------------------------------------------------------

    private List<String[]> languages() {
        List<String[]> found = new ArrayList<>();
        File root = gameRoot();
        for (String[] language : LANGUAGES)
            if (new File(root, "LANGUAGES/" + language[2] + ".bin").isFile()) found.add(language);
        return found;
    }

    private String languageLabel() {
        String current = GameOptions.language(this);
        for (String[] language : LANGUAGES) if (language[1].equalsIgnoreCase(current)) return language[0];
        return "English";
    }

    private void chooseLanguage() {
        List<String[]> found = allowed() ? languages() : new ArrayList<>();
        if (found.isEmpty()) { message("Language", "No languages found in internal storage/nfsu2/LANGUAGES."); return; }
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
                    GameOptions.set(this, option.key, option.values[which]);
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

    private String freeRoamSummary() {
        if (!GameOptions.isRoamEnabled(this)) return "Disabled";
        String room = GameOptions.roamRoom(this);
        return "Adhoc [" + room + "] (" + (GameOptions.isRoamSolid(this) ? "Solid" : "Ghost") + ")";
    }

    private void chooseFreeRoam() {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        form.setPadding(pad, pad / 2, pad, pad / 2);

        android.widget.CheckBox enabled = new android.widget.CheckBox(this);
        enabled.setText("Enable Free Roam Multiplayer (Live players in Career)");
        enabled.setTextColor(Color.WHITE);
        enabled.setChecked(GameOptions.isRoamEnabled(this));
        form.addView(enabled);

        TextView nameLabel = label("Player Name (displayed above car in city):", 13, TEXT_MUTED, false);
        nameLabel.setPadding(0, dp(8), 0, dp(2));
        form.addView(nameLabel);
        android.widget.EditText nameInput = new android.widget.EditText(this);
        nameInput.setSingleLine(true);
        nameInput.setHint("Profile name (default)");
        nameInput.setText(GameOptions.roamName(this));
        form.addView(nameInput);

        TextView roomLabel = label("Adhoc Room / Group (default: FREEROAM):", 13, TEXT_MUTED, false);
        roomLabel.setPadding(0, dp(8), 0, dp(2));
        form.addView(roomLabel);
        android.widget.EditText roomInput = new android.widget.EditText(this);
        roomInput.setSingleLine(true);
        roomInput.setText(GameOptions.roamRoom(this));
        form.addView(roomInput);

        TextView srvLabel = label("Adhoc Server (Prometheus / EA Hub):", 13, TEXT_MUTED, false);
        srvLabel.setPadding(0, dp(8), 0, dp(2));
        form.addView(srvLabel);
        android.widget.EditText srvInput = new android.widget.EditText(this);
        srvInput.setSingleLine(true);
        srvInput.setText(GameOptions.roamServer(this));
        form.addView(srvInput);

        android.widget.CheckBox solid = new android.widget.CheckBox(this);
        solid.setText("Solid car physics (drive through cars if disabled)");
        solid.setTextColor(Color.WHITE);
        solid.setChecked(GameOptions.isRoamSolid(this));
        solid.setPadding(0, dp(6), 0, 0);
        form.addView(solid);

        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Free Roam (Adhoc Multiplayer)")
                .setMessage("Connects to the open Prometheus Adhoc server (adhoc.eahub.eu) to synchronize other racers directly in Career Free Roam.")
                .setView(form)
                .setPositiveButton("Save", (dialog, which) -> {
                    GameOptions.setRoamEnabled(this, enabled.isChecked());
                    GameOptions.setRoamName(this, nameInput.getText().toString().trim());
                    String r = roomInput.getText().toString().trim();
                    if (!r.isEmpty()) GameOptions.setRoamRoom(this, r);
                    String s = srvInput.getText().toString().trim();
                    if (!s.isEmpty()) GameOptions.setRoamServer(this, s);
                    GameOptions.setRoamSolid(this, solid.isChecked());
                    refreshOptions();
                })
                .setNeutralButton("Reset Default", (dialog, which) -> {
                    GameOptions.setRoamEnabled(this, true);
                    GameOptions.setRoamServer(this, GameOptions.DEFAULT_ROAM_SERVER);
                    GameOptions.setRoamRoom(this, GameOptions.DEFAULT_ROAM_ROOM);
                    GameOptions.setRoamSolid(this, false);
                    refreshOptions();
                })
                .setNegativeButton("Cancel", null)
                .show();
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
                .setMessage("Game online mode (EA Nation) connects to this community server instead of the closed EA servers.")
                .setView(box)
                .setPositiveButton("Save", (dialog, which) -> {
                    String value = input.getText().toString().trim();
                    if (!GameOptions.validServer(value)) { message("Online server", "\"" + value + "\" is not a valid server name."); return; }
                    GameOptions.prefs(this).edit().putString(GameOptions.ONLINE_SERVER, value).apply();
                    refreshOptions();
                })
                .setNeutralButton("Reset", (dialog, which) -> {
                    GameOptions.prefs(this).edit().remove(GameOptions.ONLINE_SERVER).apply();
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
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Vulkan Driver")
                .setSingleChoiceItems(names, selected, (dialog, which) -> {
                    GpuDrivers.select(this, which == 0 ? "" : drivers.get(which - 1).id);
                    refreshOptions(); dialog.dismiss();
                })
                .setPositiveButton("Import ZIP", (dialog, which) -> startActivityForResult(
                        new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),
                        REQUEST_DRIVER_ZIP))
                .setNeutralButton("Delete selected", (dialog, which) -> {
                    GpuDrivers.Driver driver = GpuDrivers.selected(this);
                    if (driver == null) return;
                    try { GpuDrivers.remove(this, driver); refreshOptions(); }
                    catch (java.io.IOException error) { message("Vulkan Driver", error.getMessage()); }
                })
                .setNegativeButton("Back", null).show();
    }

    private void probeDriver(boolean thenPlay) {
        playAfterProbe = thenPlay;
        play.setEnabled(false);
        startActivityForResult(new Intent(this, GpuProbeActivity.class), REQUEST_DRIVER_PROBE);
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
            String text = failed ? "Could not use driver: " + gpu.optString("probeError", "error initializing Vulkan.")
                    : "GPU: " + gpu.optString("gpu") + "\nVulkan: " + gpu.optString("vulkan")
                      + "\nDriver: " + gpu.optString("driverName", "—") + " " + gpu.optString("driverInfo", "")
                      + "\nBC Textures: " + (gpu.optBoolean("bcTextures") ? "on GPU" : "converted on CPU")
                      + (gpu.optString("vertexFormatsMissing").isEmpty() ? ""
                              : "\nUnsupported vertex types: " + gpu.optString("vertexFormatsMissing"))
                      + "\n" + (gpu.optBoolean("compatible") ? "Compatible with the game."
                              : "Missing features: " + gpu.optJSONArray("missing"));
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Vulkan Driver Test").setMessage(text).setPositiveButton("OK", null)
                    .setNeutralButton("Use system driver", (dialog, which) -> { GpuDrivers.select(this, ""); refreshOptions(); })
                    .setNegativeButton(wantedToPlay ? "Play anyway" : null, wantedToPlay ? (dialog, which) -> launch() : null)
                    .show();
        } catch (Exception error) { message("Vulkan Driver Test", "Invalid diagnostic: " + error.getMessage()); }
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
                        status.setText("Driver imported: " + driver.name + ". Use Test driver to verify.");
                    });
                } catch (Exception error) {
                    main.post(() -> { refresh(); message("Vulkan Driver", "Could not import driver: " + error.getMessage()); });
                }
            });
        }
    }

    // ---- Updates and reports ---------------------------------------------------------------------------

    private void checkUpdates(boolean manual) {
        if (checkingUpdates) return;
        checkingUpdates = true;
        if (optionsList != null) refreshOptions();
        ReleaseUpdates.check(this, manual, result -> {
            checkingUpdates = false;
            if (isFinishing() || isDestroyed()) return;
            refreshOptions();
            if (result.error != null) { if (manual) message("Updates", result.error); return; }
            if (!ReleaseVersion.newer(result.tag, ReleaseUpdates.installed(this))) {
                if (manual) message("Updates", "You have version " + ReleaseUpdates.installed(this)
                        + ". There is no newer version on GitHub.");
                return;
            }
            android.content.SharedPreferences updates = ReleaseUpdates.prefs(this);
            if (!manual && result.tag.equals(updates.getString("notified_tag", ""))) return;
            updates.edit().putString("notified_tag", result.tag).apply();
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("New version available: " + result.tag)
                    .setMessage("You can download it from GitHub or keep playing with this version. Install the APK over "
                            + "the current one to preserve your saves and settings.")
                    .setPositiveButton("Update", (dialog, which) -> openUrl(Uri.parse(result.url)))
                    .setNegativeButton("Later", null).show();
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
                                    + "\n\nA GitHub issue will open on mininxd/nfsu2-recompiled with a pre-filled technical summary. Describe what happened and, "
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
        if (android.os.Process.is64Bit() && GpuDrivers.selected(this) != null) probeDriver(true);
        else launch();
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
        startActivity(new Intent(this, GameActivity.class)
                .putExtra("language", GameOptions.language(this))
                .putExtra("renderWidth", size[0]).putExtra("renderHeight", size[1])
                .putExtra("frameCap", Integer.parseInt(GameOptions.get(this, GameOptions.FPS)))
                .putExtra("widescreen", GameOptions.get(this, GameOptions.WIDESCREEN).equals("on"))
                .putExtra("minimapTop", GameOptions.get(this, GameOptions.MINIMAP).equals("top"))
                .putExtra("backBuffers", Integer.parseInt(GameOptions.get(this, GameOptions.LATENCY)))
                .putExtra("cpuTextures", GameOptions.get(this, GameOptions.TEXTURES).equals("cpu"))
                .putExtra("onlineServer", GameOptions.onlineServer(this))
                .putExtra("cutout", GameOptions.get(this, GameOptions.CUTOUT))
                .putExtra("hudSafeArea", GameOptions.get(this, GameOptions.HUD_SAFE).equals("on"))
                .putExtra("driverLoader", loader)
                .putExtra("roamEnabled", GameOptions.isRoamEnabled(this))
                .putExtra("roamServer", GameOptions.roamServer(this))
                .putExtra("roamRoom", GameOptions.roamRoom(this))
                .putExtra("roamName", GameOptions.roamName(this))
                .putExtra("roamSolid", GameOptions.isRoamSolid(this))
                .putStringArrayListExtra("cheats", GameOptions.getActiveCheatCodes(this)));
    }

    // ---- Storage ---------------------------------------------------------------------------------------

    private static File gameRoot() { return new File(Environment.getExternalStorageDirectory(), "nfsu2"); }

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

    private void addCheck(String name, boolean ok, boolean required) {
        TextView row = label((ok ? "✓  " : required ? "✗  " : "!  ") + name + (ok || required ? "" : " · optional"), 13,
                ok ? COLOR_OK : required ? COLOR_ERR : COLOR_WARN, false);
        row.setPadding(0, dp(2), 0, dp(2));
        checksList.addView(row);
    }

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
