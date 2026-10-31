package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Launcher options. GameActivity reads them when the game process starts and hands them to the native
 * runtime before the guest boots; changing one takes effect on the next PLAY.
 */
public final class GameOptions {
    static final class Option {
        final String key, title, defaultValue;
        final String[] values, labels;
        Option(String key, String title, String defaultValue, String[] values, String[] labels) {
            this.key = key; this.title = title; this.defaultValue = defaultValue; this.values = values; this.labels = labels;
        }
        String label(String value) {
            for (int i = 0; i < values.length; i++) if (values[i].equals(value)) return labels[i];
            return value;
        }
    }

    public static final String RESOLUTION = "resolution", FPS = "fps", WIDESCREEN = "widescreen", LATENCY = "latency",
            TEXTURES = "textures", MINIMAP = "minimap", LANGUAGE = "language",
            CUTOUT = "cutout", HUD_SAFE = "hud_safe", DISPLAY_LOGS = "display_logs",
            TOUCH_SIZE = "touch_size", MENU_BUTTON_SIZE = "menu_size", HORN = "horn",
            AUTOSAVE = "autosave", SHOW_HIDDEN_RACES = "show_hidden_races",
            STARTUP_MOVIES = "intro", CUTSCENE_BORDERS = "cutscene_borders",
            ROAM_CARS_LIMIT = "roam_max_cars", ROAM_CREW = "roam_crew",
            GRAPHICS_BACKEND = "graphics_backend";

    static final Option[] ALL = {
            new Option(GRAPHICS_BACKEND, "Graphics API", "auto",
                    new String[] {"auto", "vulkan", "opengl"},
                    new String[] {"Auto (Recommended)", "Vulkan (DXVK)", "OpenGL (ES)"}),
            new Option(RESOLUTION, "Internal Resolution", "75",
                    new String[] {"25", "30", "35", "40", "45", "50", "55", "60", "65", "70", "75", "80", "85", "90", "95", "100", "125", "150", "200"},
                    new String[] {"25%", "30%", "35%", "40%", "45%", "50%", "55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100%", "125%", "150%", "200%"}),
            new Option(CUTOUT, "Display Cutout (Notch)", "fullscreen",
                    new String[] {"fullscreen", "safe"},
                    new String[] {"Full Screen · bypass notch", "Letterbox · avoid cutout"}),
            new Option(HUD_SAFE, "HUD Safe Area (20:9 / Notch)", "on",
                    new String[] {"on", "off"},
                    new String[] {"16:9 Centered · safe from notch", "Screen edges · ultra-wide"}),
            new Option(FPS, "FPS Limit", "30",
                    new String[] {"30", "60", "90", "120", "0"},
                    new String[] {"30 FPS", "60 FPS", "90 FPS", "120 FPS", "Uncapped"}),
            new Option(DISPLAY_LOGS, "Display Logs", "off",
                    new String[] {"off", "compact", "full"},
                    new String[] {"Off", "Compact", "Full"}),
            new Option(WIDESCREEN, "Widescreen", "on",
                    new String[] {"on", "off"},
                    new String[] {"Corrected HUD and camera", "Original 4:3 stretched"}),
            new Option(MINIMAP, "Minimap", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Top left", "Bottom left (original)"}),
            new Option(AUTOSAVE, "Autosave", "on",
                    new String[] {"on", "off"},
                    new String[] {"On · Save without prompting", "Off · Original prompt"}),
            new Option(STARTUP_MOVIES, "Startup Movies", "skippable",
                    new String[] {"skippable", "original", "off"},
                    new String[] {"Skippable · Tap/Enter skips logos", "Original · Unskippable logos", "Off · Skip directly to main menu"}),
            new Option(CUTSCENE_BORDERS, "Cutscene Bars", "off",
                    new String[] {"off", "full"},
                    new String[] {"Off · No black letterbox bars", "Full · Black bars across wide screen"}),
            new Option(ROAM_CARS_LIMIT, "Players' Cars Shown", "24",
                    new String[] {"16", "20", "24", "28", "32"},
                    new String[] {"16 cars · Low memory", "20 cars", "24 cars · Default", "28 cars", "32 cars · Max"}),
            new Option(LATENCY, "CPU/GPU Sync", "2",
                    new String[] {"2", "1"},
                    new String[] {"Performance · CPU and GPU in parallel", "Low latency · lower FPS"}),
            new Option(TEXTURES, "Compressed Textures", "auto",
                    new String[] {"auto", "cpu"},
                    new String[] {"Automatic · GPU if supported", "Convert on CPU · compatibility"}),
    };

    private static final String PREFS = "nfsu2_game";

    private GameOptions() {}

    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static Option find(String key) {
        if (key == null) return null;
        for (Option o : ALL) if (o.key.equals(key)) return o;
        return null;
    }

    static String get(Context context, String key) {
        Option o = find(key);
        if (o == null) {
            if (TOUCH_SIZE.equals(key)) return prefs(context).getString(key, "auto");
            if (MENU_BUTTON_SIZE.equals(key)) return prefs(context).getString(key, "auto");
            return prefs(context).getString(key, "off");
        }
        String value = prefs(context).getString(key, o.defaultValue);
        for (String allowed : o.values) if (allowed.equals(value)) return value;
        return o.defaultValue;
    }

    static void set(Context context, String key, String value) {
        prefs(context).edit().putString(key, value).apply();
    }

    static final String ONLINE_SERVER = "online_server", DEFAULT_ONLINE_SERVER = "eahub.eu";

    /** Free Roam Multiplayer options (Prometheus Adhoc over adhoc.eahub.eu). */
    static final String ROAM_ENABLED = "roam_enabled", ROAM_SERVER = "roam_server",
            ROAM_ROOM = "roam_room", ROAM_NAME = "roam_name", ROAM_SOLID = "roam_solid";
    public static final String DEFAULT_ROAM_SERVER = "adhoc.eahub.eu";
    public static final String DEFAULT_ROAM_ROOM = "";
    public static final int ADHOCCTL_PORT = 27312;
    public static final int POSTOFFICE_PORT = 27313;

    static boolean isRoamEnabled(Context context) {
        return prefs(context).getBoolean(ROAM_ENABLED, true);
    }

    static void setRoamEnabled(Context context, boolean enabled) {
        prefs(context).edit().putBoolean(ROAM_ENABLED, enabled).apply();
    }

    static final String ROAM_GHOST = "roam_ghost";

    public static boolean isGhostModeEnabled(Context context) {
        return prefs(context).getBoolean(ROAM_GHOST, true);
    }

    public static boolean isDisplayLogsEnabled(Context context) {
        if (context == null) return false;
        String val = get(context, DISPLAY_LOGS);
        return val != null && !"off".equalsIgnoreCase(val);
    }

    static void setGhostModeEnabled(Context context, boolean enabled) {
        prefs(context).edit().putBoolean(ROAM_GHOST, enabled).apply();
    }

    public static String roamServer(Context context) {
        String server = onlineServer(context);
        if (DEFAULT_ONLINE_SERVER.equalsIgnoreCase(server) || "eahub.eu".equalsIgnoreCase(server)) {
            return DEFAULT_ROAM_SERVER;
        }
        return server;
    }

    public static String roamRoom(Context context) {
        return prefs(context).getString(ROAM_ROOM, DEFAULT_ROAM_ROOM);
    }

    static void setRoamRoom(Context context, String room) {
        prefs(context).edit().putString(ROAM_ROOM, room != null ? room.trim() : "").apply();
    }

    static String roamName(Context context) {
        return detectSavegamePlayerName();
    }

    public static String detectSavegamePlayerName() {
        try {
            java.io.File appData = new java.io.File(android.os.Environment.getExternalStorageDirectory(),
                    "nfsu2/AppData/Local/NFS Underground 2");
            if (appData.isDirectory()) {
                java.io.File[] profiles = appData.listFiles(java.io.File::isDirectory);
                if (profiles != null && profiles.length > 0) {
                    java.io.File latest = null;
                    long latestTime = 0;
                    for (java.io.File p : profiles) {
                        String name = p.getName();
                        if (name.contains("DVD") || name.contains("Magazine")) continue;
                        java.io.File save = new java.io.File(p, name);
                        if (save.isFile()) {
                            long m = save.lastModified();
                            if (m > latestTime) {
                                latestTime = m;
                                latest = save;
                            }
                        }
                    }
                    if (latest != null) return latest.getName();
                }
            }
        } catch (Exception ignored) {}
        return "Racer";
    }

    static boolean isRoamSolid(Context context) {
        return false;
    }

    public static String roamCrew(Context context) {
        return prefs(context).getString(ROAM_CREW, "");
    }

    static void setRoamCrew(Context context, String crew) {
        prefs(context).edit().putString(ROAM_CREW, crew != null ? crew.trim() : "").apply();
    }

    /** EA Nation replacement server: the game's *.ea.com hosts resolve to this name or IPv4 address. */
    public static String onlineServer(Context context) {
        String value = prefs(context).getString(ONLINE_SERVER, DEFAULT_ONLINE_SERVER);
        return validServer(value) ? value : DEFAULT_ONLINE_SERVER;
    }

    public static boolean validServer(String value) {
        if (value == null || value.trim().isEmpty()) return false;
        String s = value.trim();
        if (s.startsWith("http://")) s = s.substring(7);
        else if (s.startsWith("https://")) s = s.substring(8);
        else if (s.startsWith("ws://")) s = s.substring(5);
        else if (s.startsWith("wss://")) s = s.substring(6);
        if (s.contains("/")) s = s.substring(0, s.indexOf("/"));
        if (s.contains(":")) {
            String[] parts = s.split(":");
            if (parts.length != 2) return false;
            try {
                int p = Integer.parseInt(parts[1]);
                if (p <= 0 || p > 65535) return false;
            } catch (Exception e) {
                return false;
            }
            s = parts[0];
        }
        return s.length() <= 253 && s.matches("[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*");
    }

    /** The game's language folder name (LANGUAGES/<file>.bin) chosen in the launcher. */
    static String language(Context context) {
        String lang = prefs(context).getString(LANGUAGE, "English UK");
        if ("English".equalsIgnoreCase(lang) || "English (UK)".equalsIgnoreCase(lang) || "English (US)".equalsIgnoreCase(lang)) {
            return "English UK";
        }
        return lang;
    }

    /** Render size for the selected resolution on a display of width × height (landscape). */
    static int[] renderSize(Context context, int width, int height) {
        String value = get(context, RESOLUTION);
        if (value.equals("720")) return new int[] {1280, 720};
        float scale = Integer.parseInt(value) / 100f;
        return new int[] {Math.round(width * scale / 2f) * 2, Math.round(height * scale / 2f) * 2};
    }

    static final class Cheat {
        final String id, title, description;
        final String[] codes;
        Cheat(String id, String title, String description, String... codes) {
            this.id = id; this.title = title; this.description = description; this.codes = codes;
        }
    }

    static final Cheat[] CHEATS = {
        new Cheat("hidden_races", "Show Hidden Career Races", "Reveal all 33 hidden career races on world map"),
        new Cheat("districts", "Unlock All Districts", "Explore Coal Harbor, Jackson Heights & all map areas (opendoors)", "opendoors"),
        new Cheat("cars_parts", "Unlock All Cars & Circuit Parts", "All vehicles & performance parts in Career (ordermebaby)", "ordermebaby"),
        new Cheat("perf_lv1", "Unlock Performance Parts Lv 1", "Engine, transmission, ECU, nitrous Lv 1 (needperformance1)", "needperformance1"),
        new Cheat("perf_lv2", "Unlock Performance Parts Lv 2", "Top-tier Level 2 performance upgrades (needperformance2)", "needperformance2"),
        new Cheat("vis_lv1", "Unlock Visual Upgrades Lv 1", "Body kits, spoilers, hoods, rims Lv 1 (gimmevisual1)", "gimmevisual1"),
        new Cheat("vis_lv2", "Unlock Visual Upgrades Lv 2", "Level 2 widebody and visual customization (gimmevisual2)", "gimmevisual2"),
        new Cheat("cash", "Extra Career Cash (+$20,000)", "Starting bank boost in Career mode (regmebaby)", "regmebaby"),
        new Cheat("sponsors", "Unlock Sponsor Cars", "Snoop Dogg, Chingy, Capone, D3, ShineStreet & David Choe cars", "yodogg", "gimmechingy", "wannacapone", "davidchoeart", "tunejapantuning", "shinestreetbright", "wantmyd3"),
        new Cheat("vinyls", "Unlock Sponsor Vinyls", "Best Buy, Cingular, Burger King, Edge & Old Spice", "needmybestbuy", "gotmycingular", "gottahavebk", "gottaedge", "goforoldspice")
    };

    static boolean isCheatEnabled(Context context, String id) {
        if ("hidden_races".equals(id)) {
            SharedPreferences p = prefs(context);
            if (p.contains("cheat_hidden_races")) {
                return p.getBoolean("cheat_hidden_races", true);
            }
            if (p.contains(SHOW_HIDDEN_RACES)) {
                return "on".equals(p.getString(SHOW_HIDDEN_RACES, "on"));
            }
            return true;
        }
        return prefs(context).getBoolean("cheat_" + id, false);
    }

    static void setCheatEnabled(Context context, String id, boolean enabled) {
        prefs(context).edit().putBoolean("cheat_" + id, enabled).apply();
        if ("hidden_races".equals(id)) {
            prefs(context).edit().putString(SHOW_HIDDEN_RACES, enabled ? "on" : "off").apply();
        }
    }

    static java.util.ArrayList<String> getActiveCheatCodes(Context context) {
        java.util.ArrayList<String> list = new java.util.ArrayList<>();
        for (Cheat c : CHEATS) {
            if (isCheatEnabled(context, c.id)) {
                for (String code : c.codes) if (!list.contains(code)) list.add(code);
            }
        }
        return list;
    }

    /**
     * Synchronizes scripts/NFSUnderground2.WidescreenFix.ini settings.
     * Updates FEScale (button & UI sizing up to 200%), HudAspectRatioConstraint,
     * AutoFitFE, SingleCoreAffinity, SkipIntro, and DisableCutsceneBorders.
     */
    static void syncWidescreenIni(Context context) {
        try {
            java.io.File iniFile = new java.io.File(LauncherActivity.gameRoot(),
                    "scripts/NFSUnderground2.WidescreenFix.ini");
            if (!iniFile.isFile()) {
                iniFile = new java.io.File(android.os.Environment.getExternalStorageDirectory(),
                        "nfsu2/scripts/NFSUnderground2.WidescreenFix.ini");
                if (!iniFile.isFile()) {
                    iniFile = new java.io.File(android.os.Environment.getExternalStorageDirectory(),
                            "NFSU2/scripts/NFSUnderground2.WidescreenFix.ini");
                }
            }
            if (!iniFile.isFile()) return;

            String menuSize = get(context, MENU_BUTTON_SIZE);
            String feScale = "2.00";
            if ("100".equals(menuSize)) feScale = "1.00";
            else if ("125".equals(menuSize)) feScale = "1.25";
            else if ("150".equals(menuSize)) feScale = "1.50";
            else if ("175".equals(menuSize)) feScale = "1.75";
            else if ("200".equals(menuSize) || "auto".equals(menuSize)) feScale = "2.00";

            String hudSafe = get(context, HUD_SAFE);
            String aspect = "on".equals(hudSafe) ? "16:9" : "Auto";

            String intro = get(context, STARTUP_MOVIES);
            String skipIntro = "original".equals(intro) ? "0" : "1";

            String borders = get(context, CUTSCENE_BORDERS);
            String cutsceneBorders = "full".equals(borders) ? "0" : "1";

            java.util.List<String> lines = new java.util.ArrayList<>();
            boolean changed = false;
            try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(iniFile))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    String trimmed = line.trim();
                    if (trimmed.startsWith("FEScale")) {
                        String target = "FEScale = " + feScale + "                           ; Set the size of UI elements here. (Default = 1.00, Xbox = 0.92)";
                        if (!line.startsWith("FEScale = " + feScale)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("HudAspectRatioConstraint")) {
                        String target = "HudAspectRatioConstraint = " + aspect + "          ; Constrains the HUD to an aspect ratio. Use Auto, 16:9, 21:9, or a custom ratio.";
                        if (!line.startsWith("HudAspectRatioConstraint = " + aspect)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("AutoFitFE")) {
                        String target = "AutoFitFE = 1                            ; Automatically fits 16:9 UI content to smaller aspect ratios.";
                        if (!line.startsWith("AutoFitFE = 1")) { line = target; changed = true; }
                    } else if (trimmed.startsWith("SingleCoreAffinity")) {
                        String target = "SingleCoreAffinity = 1                   ; Limits game to one CPU core to prevent crashing.";
                        if (!line.startsWith("SingleCoreAffinity = 1")) { line = target; changed = true; }
                    } else if (trimmed.startsWith("SkipIntro")) {
                        String target = "SkipIntro = " + skipIntro + "                            ; Skips FMVs that play when you launch the game.";
                        if (!line.startsWith("SkipIntro = " + skipIntro)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("DisableCutsceneBorders")) {
                        String target = "DisableCutsceneBorders = " + cutsceneBorders + "               ; Removes letterboxing that appears during cutscenes.";
                        if (!line.startsWith("DisableCutsceneBorders = " + cutsceneBorders)) { line = target; changed = true; }
                    }
                    lines.add(line);
                }
            }

            if (changed) {
                try (java.io.PrintWriter writer = new java.io.PrintWriter(new java.io.FileWriter(iniFile))) {
                    for (String l : lines) {
                        writer.println(l);
                    }
                }
                android.util.Log.i("NFSU2", "Synchronized WidescreenFix.ini (FEScale=" + feScale + ", Aspect=" + aspect + ")");
            }
        } catch (Exception e) {
            android.util.Log.w("NFSU2", "Failed to synchronize WidescreenFix.ini", e);
        }
    }

    /**
     * Synchronizes scripts/NFSUnderground2.WidescreenFix.dat layout.
     * When displayLogs is enabled, repositions the Bank money GUI to the left and
     * offsets the SMS and Navigation GUI downward to keep the right telemetry HUD clear.
     */
    static void syncWidescreenHudDat(Context context, boolean logsEnabled) {
        try {
            java.io.File datFile = new java.io.File(android.os.Environment.getExternalStorageDirectory(),
                    "nfsu2/scripts/NFSUnderground2.WidescreenFix.dat");
            if (!datFile.isFile()) return;

            java.util.List<String> lines = new java.util.ArrayList<>();
            boolean changed = false;
            try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(datFile))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    String trimmed = line.trim();
                    if (trimmed.startsWith("Bank_Bar_Pos")) {
                        String target = logsEnabled
                                ? "Bank_Bar_Pos                         227.0  -184.0               -485.0  0.0"
                                : "Bank_Bar_Pos                         227.0  -184.0                119.0  0.0";
                        if (!line.equals(target)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("Bank_Text_Pos")) {
                        String target = logsEnabled
                                ? "Bank_Text_Pos                        193.0  -194.0               -485.0  0.0"
                                : "Bank_Text_Pos                        193.0  -194.0                119.0  0.0";
                        if (!line.equals(target)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("Bank_Reward_Text_Pos")) {
                        String target = logsEnabled
                                ? "Bank_Reward_Text_Pos                 283.0  -163.0               -485.0  0.0"
                                : "Bank_Reward_Text_Pos                 283.0  -163.0                119.0  0.0";
                        if (!line.equals(target)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("SMS_Bar_Pos")) {
                        String target = logsEnabled
                                ? "SMS_Bar_Pos                         -248.0  -122.0               -109.0  50.0"
                                : "SMS_Bar_Pos                         -248.0  -122.0               -109.0  0.0";
                        if (!line.equals(target)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("SMS_Icon_Pos")) {
                        String target = logsEnabled
                                ? "SMS_Icon_Pos                        -195.0  -112.0               -109.0  50.0"
                                : "SMS_Icon_Pos                        -195.0  -112.0               -109.0  0.0";
                        if (!line.equals(target)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("Satellite_Signal_Pos")) {
                        String target = logsEnabled
                                ? "Satellite_Signal_Pos                -263.0   36.0                 -109.0  50.0"
                                : "Satellite_Signal_Pos                -263.0   36.0                 -109.0  0.0";
                        if (!line.equals(target)) { line = target; changed = true; }
                    }
                    lines.add(line);
                }
            }

            if (changed) {
                try (java.io.PrintWriter writer = new java.io.PrintWriter(new java.io.FileWriter(datFile))) {
                    for (String l : lines) {
                        writer.println(l);
                    }
                }
                android.util.Log.i("NFSU2", "Synchronized WidescreenFix.dat layout (logsEnabled=" + logsEnabled + ")");
            }
        } catch (Exception e) {
            android.util.Log.w("NFSU2", "Failed to synchronize WidescreenFix.dat layout", e);
        }
    }
}
