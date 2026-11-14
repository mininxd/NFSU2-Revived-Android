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
            GRAPHICS_BACKEND = "graphics_backend", DRAW_DISTANCE = "draw_distance",
            TRACKSIDE_CROWDS = "trackside_crowds", REGION = "region", SPEEDOMETER = "speedometer", PAD_LAYOUT = "pad_layout",
            UPDATE_CHANNEL = "update_channel";

    public static final String CHANNEL_STABLE = "stable", CHANNEL_BETA = "beta";

    static final Option[] ALL = {
            new Option(GRAPHICS_BACKEND, "Graphics API", "auto",
                    new String[] {"auto", "vulkan", "opengl"},
                    new String[] {"Auto", "Vulkan (DXVK)", "OpenGL (ES)"}),
            new Option(RESOLUTION, "Internal Resolution", "75",
                    new String[] {"25", "30", "35", "40", "45", "50", "55", "60", "65", "70", "75", "80", "85", "90", "95", "100", "125", "150", "200"},
                    new String[] {"25%", "30%", "35%", "40%", "45%", "50%", "55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100%", "125%", "150%", "200%"}),
            new Option(DRAW_DISTANCE, "Draw Distance", "performance",
                    new String[] {"ultra_performance", "performance", "balanced", "extended", "original"},
                    new String[] {"Ultra Performance", "Performance", "Balanced", "Extended", "Original"}),
            new Option(FPS, "FPS Limit", "30",
                    new String[] {"30", "60", "90", "120", "0"},
                    new String[] {"30 FPS", "60 FPS", "90 FPS", "120 FPS", "Uncapped"}),
            new Option(DISPLAY_LOGS, "Display Logs", "off",
                    new String[] {"off", "compact", "full"},
                    new String[] {"Off", "Compact", "Full"}),
            new Option(WIDESCREEN, "Widescreen HUD", "off",
                    new String[] {"on", "off"},
                    new String[] {"On", "Off"}),
            new Option(MINIMAP, "Minimap", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Top left", "Bottom left (original)"}),
            new Option(PAD_LAYOUT, "Gamepad Layout", "port",
                    new String[] {"port", "console"},
                    new String[] {"Default Xbox", "PS2 style"}),
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

    /** Speedometer for nativeSpeedometerPosition: 0 bottom right (original). */
    static int speedometerPosition(Context context) {
        return 0;
    }

    /**
     * Display is always full-screen. This only places the .dat HUD:
     * 0 = Widescreen (HUD at the edges), 2 = 4:3 (HUD centred, not stretched).
     */
    static int widescreenMode(Context context) {
        return "on".equals(get(context, WIDESCREEN)) ? 0 : 2;
    }

    /** Returns true if Widescreen HUD (offsets to edges) is selected, false if 4:3 centered HUD. */
    static boolean usesWidescreenPatch(Context context) {
        return "on".equals(get(context, WIDESCREEN));
    }

    /**
     * Applies the correct HUD layout file based on the Widescreen HUD option.
     * Widescreen ON: ensures full scripts/NFSUnderground2.WidescreenFix.dat is present so elements are placed at display edges.
     * Widescreen OFF: parks the .dat file as .hold so no offsets are applied and the entire HUD stays in original 4:3 layout
     *                 (minimap top offset is applied directly in widescreen.c without horizontal shift).
     */
    static void applyWidescreenPatchFile(Context context) {
        java.io.File[] roots = new java.io.File[] {
                LauncherActivity.gameRoot(),
                new java.io.File(android.os.Environment.getExternalStorageDirectory(), "nfsu2"),
                new java.io.File(android.os.Environment.getExternalStorageDirectory(), "NFSU2")
        };
        boolean widescreen = usesWidescreenPatch(context);
        for (java.io.File root : roots) {
            if (root == null) continue;
            java.io.File scripts = new java.io.File(root, "scripts");
            if (!scripts.isDirectory() && !scripts.mkdirs()) continue;
            java.io.File dat = new java.io.File(scripts, "NFSUnderground2.WidescreenFix.dat");
            java.io.File held = new java.io.File(scripts, "NFSUnderground2.WidescreenFix.dat.hold");
            try {
                if (widescreen) {
                    if (held.isFile() && held.length() > 1000) {
                        if (dat.isFile()) dat.delete();
                        held.renameTo(dat);
                    }
                    if (!dat.isFile() || dat.length() < 1000) {
                        if (dat.isFile()) dat.delete();
                        LauncherActivity.copyAssetFile(context, "scripts/NFSUnderground2.WidescreenFix.dat", dat);
                    }
                } else {
                    if (dat.isFile()) {
                        if (dat.length() > 1000) {
                            if (held.isFile()) held.delete();
                            dat.renameTo(held);
                        } else {
                            dat.delete();
                        }
                    }
                }
            } catch (Throwable t) {
                android.util.Log.w("NFSU2", "WidescreenFix.dat (" + root + "): " + t.getMessage());
            }
        }
    }

    /** Game SKU for nativeRegion: -1 game default, 0 North America, 1 Europe.
     *  When set to "auto", detects the region by checking for GLOBALTEXTURESROW.BIN
     *  (EU-only asset) in the game directory. Falls back to -1 (NA default) if absent. */
    static int regionCode(Context context) {
        String region = get(context, REGION);
        if ("eu".equals(region)) return 1;
        if ("na".equals(region)) return 0;
        // "auto": probe the game folder for the EU-exclusive texture row file
        return detectRegionFromFiles(context);
    }

    /**
     * Auto-detects region by checking for GLOBALTEXTURESROW.BIN, which only ships
     * with the European SKU. Returns 1 (EU) if found, -1 (game default / NA) otherwise.
     */
    static int detectRegionFromFiles(Context context) {
        try {
            String[] candidates = {
                LauncherActivity.gameRoot() + "/GLOBALTEXTURESROW.BIN",
                android.os.Environment.getExternalStorageDirectory() + "/nfsu2/GLOBALTEXTURESROW.BIN",
                android.os.Environment.getExternalStorageDirectory() + "/NFSU2/GLOBALTEXTURESROW.BIN",
            };
            for (String path : candidates) {
                if (new java.io.File(path).isFile()) {
                    android.util.Log.i("NFSU2", "Region auto-detect: found GLOBALTEXTURESROW.BIN → EU (1)");
                    return 1;
                }
            }
        } catch (Throwable ignored) {}
        android.util.Log.d("NFSU2", "Region auto-detect: GLOBALTEXTURESROW.BIN not found → game default (-1)");
        return -1;
    }

    static String get(Context context, String key) {
        Option o = find(key);
        if (o == null) {
            if (UPDATE_CHANNEL.equals(key)) return updateChannel(context);
            if (TOUCH_SIZE.equals(key)) return prefs(context).getString(key, "auto");
            if (MENU_BUTTON_SIZE.equals(key)) return prefs(context).getString(key, "100");
            return prefs(context).getString(key, "off");
        }
        String value = prefs(context).getString(key, o.defaultValue);
        if (PAD_LAYOUT.equals(key)) {
            if ("ps2".equals(value)) value = "console";
            else if ("default".equals(value) || "xbox".equals(value)) value = "port";
        }
        if (WIDESCREEN.equals(key) && ("stretched".equals(value) || "hud".equals(value))) value = "off";
        for (String allowed : o.values) if (allowed.equals(value)) return value;
        return o.defaultValue;
    }

    /** True when Gamepad Layout shows PS2 names (Cross/Circle/Square/Triangle). Mapping is unchanged. */
    static boolean isConsolePad(Context context) {
        return "console".equals(get(context, PAD_LAYOUT));
    }

    static void set(Context context, String key, String value) {
        prefs(context).edit().putString(key, value).apply();
    }

    public static String updateChannel(Context context) {
        return prefs(context).getString(UPDATE_CHANNEL, CHANNEL_STABLE);
    }

    public static boolean isBetaChannel(Context context) {
        return CHANNEL_BETA.equalsIgnoreCase(updateChannel(context));
    }

    public static void setUpdateChannel(Context context, String channel) {
        prefs(context).edit().putString(UPDATE_CHANNEL, channel != null ? channel.toLowerCase(java.util.Locale.ROOT) : CHANNEL_STABLE).apply();
    }

    static final String ONLINE_SERVER = "online_server", DEFAULT_ONLINE_SERVER = "eahub.eu";

    /**
     * Returns true if the configured server is eahub.eu or adhoc.eahub.eu —
     * the classic EA Nation / Adhoc Hub running original DirtySock TCP protocol.
     * Classic servers must NOT go through LocalOnlineBridge (WebSocket path).
     */
    public static boolean isEahubClassic(String server) {
        if (server == null) return false;
        String s = server.trim().toLowerCase(java.util.Locale.US);
        return s.equals("eahub.eu") || s.equals("adhoc.eahub.eu")
                || s.endsWith(".eahub.eu");
    }

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
        // For eahub classic, the adhoc/roam service lives on adhoc.eahub.eu
        if (isEahubClassic(server)) {
            return DEFAULT_ROAM_SERVER;
        }
        // For custom servers, use the same host (they host both online and roam)
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
        float scale = Integer.parseInt(value) / 100f;
        return new int[] {Math.round(width * scale / 2f) * 2, Math.round(height * scale / 2f) * 2};
    }

    /**
     * Resolves the effective graphics backend to use.
     * When "auto" is selected, checks config/graphics_api.json against the device CPU/SoC.
     */
    public static String getEffectiveGraphicsBackend(Context context) {
        String selected = get(context, GRAPHICS_BACKEND);
        if (!"auto".equalsIgnoreCase(selected)) {
            return selected;
        }

        String jsonStr = loadGraphicsApiJson(context);
        if (jsonStr != null && !jsonStr.trim().isEmpty()) {
            try {
                org.json.JSONObject obj = new org.json.JSONObject(jsonStr);
                java.util.List<String> cpuIds = getCpuIdentifiers();

                if (obj.has("opengl")) {
                    org.json.JSONArray gl = obj.getJSONArray("opengl");
                    for (int i = 0; i < gl.length(); i++) {
                        String target = gl.getString(i);
                        for (String id : cpuIds) {
                            if (matchesCpu(id, target)) return "opengl";
                        }
                    }
                }

                if (obj.has("vulkan")) {
                    org.json.JSONArray vk = obj.getJSONArray("vulkan");
                    for (int i = 0; i < vk.length(); i++) {
                        String target = vk.getString(i);
                        for (String id : cpuIds) {
                            if (matchesCpu(id, target)) return "vulkan";
                        }
                    }
                }
            } catch (Throwable t) {
                android.util.Log.w("NFSU2", "Failed to parse graphics_api.json: " + t.getMessage());
            }
        }

        return "vulkan";
    }

    private static boolean matchesCpu(String cpuId, String target) {
        if (cpuId == null || target == null || target.isEmpty()) return false;
        String c = cpuId.toLowerCase(java.util.Locale.US).replaceAll("[^a-z0-9]", "");
        String t = target.toLowerCase(java.util.Locale.US).replaceAll("[^a-z0-9]", "");
        if (c.contains(t) || t.contains(c)) return true;
        if (t.startsWith("mt") && t.length() > 4) {
            String num = t.substring(2);
            if (c.contains("mt") && c.contains(num)) return true;
        }
        return false;
    }

    private static java.util.List<String> getCpuIdentifiers() {
        java.util.List<String> ids = new java.util.ArrayList<>();
        if (android.os.Build.HARDWARE != null && !android.os.Build.HARDWARE.isEmpty()) {
            ids.add(android.os.Build.HARDWARE.toLowerCase(java.util.Locale.US).trim());
        }
        if (android.os.Build.BOARD != null && !android.os.Build.BOARD.isEmpty()) {
            ids.add(android.os.Build.BOARD.toLowerCase(java.util.Locale.US).trim());
        }
        if (android.os.Build.MODEL != null && !android.os.Build.MODEL.isEmpty()) {
            ids.add(android.os.Build.MODEL.toLowerCase(java.util.Locale.US).trim());
        }
        if (android.os.Build.DEVICE != null && !android.os.Build.DEVICE.isEmpty()) {
            ids.add(android.os.Build.DEVICE.toLowerCase(java.util.Locale.US).trim());
        }
        if (android.os.Build.PRODUCT != null && !android.os.Build.PRODUCT.isEmpty()) {
            ids.add(android.os.Build.PRODUCT.toLowerCase(java.util.Locale.US).trim());
        }
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            try {
                if (android.os.Build.SOC_MODEL != null && !android.os.Build.SOC_MODEL.isEmpty()) {
                    ids.add(android.os.Build.SOC_MODEL.toLowerCase(java.util.Locale.US).trim());
                }
            } catch (Throwable ignored) {}
        }
        String[] propKeys = {"ro.board.platform", "ro.hardware", "ro.mediatek.platform", "ro.soc.model", "ro.boot.hardware", "ro.hardware.egl", "ro.opengles.version"};
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            java.lang.reflect.Method getMethod = sp.getMethod("get", String.class);
            for (String key : propKeys) {
                String val = (String) getMethod.invoke(null, key);
                if (val != null && !val.trim().isEmpty()) {
                    ids.add(val.toLowerCase(java.util.Locale.US).trim());
                }
            }
        } catch (Throwable ignored) {}

        try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.FileReader("/proc/cpuinfo"))) {
            String line;
            while ((line = br.readLine()) != null) {
                if (line.toLowerCase(java.util.Locale.US).startsWith("hardware")) {
                    int colon = line.indexOf(':');
                    if (colon >= 0 && colon + 1 < line.length()) {
                        String hw = line.substring(colon + 1).toLowerCase(java.util.Locale.US).trim();
                        if (!hw.isEmpty()) ids.add(hw);
                    }
                }
            }
        } catch (Throwable ignored) {}

        return ids;
    }

    private static String loadGraphicsApiJson(Context context) {
        try {
            java.io.File[] candidates = {
                new java.io.File(LauncherActivity.gameRoot(), "config/graphics_api.json"),
                new java.io.File(android.os.Environment.getExternalStorageDirectory(), "NFSU2/config/graphics_api.json"),
                new java.io.File(android.os.Environment.getExternalStorageDirectory(), "nfsu2/config/graphics_api.json")
            };
            for (java.io.File f : candidates) {
                if (f != null && f.isFile()) {
                    return readFileToString(f);
                }
            }
        } catch (Throwable ignored) {}

        if (context != null) {
            String[] assetPaths = {"config/graphics_api.json", "graphics_api.json"};
            for (String path : assetPaths) {
                try (java.io.InputStream is = context.getAssets().open(path)) {
                    java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
                    byte[] buf = new byte[4096];
                    int r;
                    while ((r = is.read(buf)) != -1) bos.write(buf, 0, r);
                    return bos.toString("UTF-8");
                } catch (Throwable ignored) {}
            }
        }
        return null;
    }

    private static String readFileToString(java.io.File file) {
        try (java.io.FileInputStream fis = new java.io.FileInputStream(file);
             java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream()) {
            byte[] buf = new byte[4096];
            int r;
            while ((r = fis.read(buf)) != -1) bos.write(buf, 0, r);
            return bos.toString("UTF-8");
        } catch (Throwable ignored) {
            return null;
        }
    }

    static final class Cheat {
        final String id, title, description;
        final String[] codes;
        Cheat(String id, String title, String description, String... codes) {
            this.id = id; this.title = title; this.description = description; this.codes = codes;
        }
    }

    static final Cheat[] CHEATS = {
        new Cheat("dumb_ai", "Dumb AI (AutoDrive)", "Enable AI autopilot / Drunk Driver to steer and drive player vehicle"),
        new Cheat("cash", "Starting Cash (+$20,000)", "Starting bank boost in Career mode (regmebaby)", "regmebaby")
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

    public static native int nativeSyncSettings(String gameRoot, String prefsXmlPath);

    /**
     * Synchronizes scripts/NFSUnderground2.WidescreenFix.ini settings.
     * Updates FEScale (button & UI sizing up to 200%), HudAspectRatioConstraint,
     * AutoFitFE, SingleCoreAffinity, SkipIntro, and DisableCutsceneBorders.
     */
    static void syncWidescreenIni(Context context) {
        try {
            String root = LauncherActivity.gameRoot().getAbsolutePath();
            String prefs = "/data/data/" + context.getPackageName() + "/shared_prefs/nfsu2_game.xml";
            if (nativeSyncSettings(root, prefs) == 0) {
                return;
            }
        } catch (Throwable ignored) {}
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
            String feScale = "1.00";
            if ("125".equals(menuSize)) feScale = "1.25";
            else if ("150".equals(menuSize)) feScale = "1.50";
            else if ("175".equals(menuSize)) feScale = "1.75";
            else if ("200".equals(menuSize)) feScale = "2.00";

            String aspect = "Auto";
            String skipIntro = "0";
            String cutsceneBorders = "0";

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
                        String target = "SingleCoreAffinity = 0                   ; Performance core affinity manager handles CPU allocation.";
                        if (!line.startsWith("SingleCoreAffinity = 0")) { line = target; changed = true; }
                    } else if (trimmed.startsWith("SkipIntro")) {
                        String target = "SkipIntro = " + skipIntro + "                            ; Skips FMVs that play when you launch the game.";
                        if (!line.startsWith("SkipIntro = " + skipIntro)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("DisableCutsceneBorders")) {
                        String target = "DisableCutsceneBorders = " + cutsceneBorders + "               ; Removes letterboxing that appears during cutscenes.";
                        if (!line.startsWith("DisableCutsceneBorders = " + cutsceneBorders)) { line = target; changed = true; }
                    } else if (trimmed.startsWith("ImproveGamepadSupport")) {
                        String padSupport = isConsolePad(context) ? "2" : "1";
                        String target = "ImproveGamepadSupport = " + padSupport + "                ; Adds text to buttons and assigns front-end actions. Requires an XInput gamepad. (1 = Xbox Text | 2 = PlayStation Text | 3 = PC Text | 4 = None)";
                        if (!line.startsWith("ImproveGamepadSupport = " + padSupport)) { line = target; changed = true; }
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
            java.io.File scripts = new java.io.File(LauncherActivity.gameRoot(), "scripts");
            java.io.File datFile = new java.io.File(scripts, "NFSUnderground2.WidescreenFix.dat");
            if (!datFile.isFile()) {
                datFile = new java.io.File(scripts, "NFSUnderground2.WidescreenFix.dat.hold");
            }
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

    // ---- Extra Options (ExOpts Team mod) ----------------------------------------------------
    public static final String EXTRA_OPTIONS_ENABLED = "extra_options_enabled";

    public static boolean isExtraOptionsEnabled(Context context) {
        return prefs(context).getBoolean(EXTRA_OPTIONS_ENABLED, true);
    }

    public static void setExtraOptionsEnabled(Context context, boolean enabled) {
        prefs(context).edit().putBoolean(EXTRA_OPTIONS_ENABLED, enabled).apply();
    }

    public static boolean getExtraOption(Context context, String key, boolean defVal) {
        return prefs(context).getBoolean("exopts_" + key, defVal);
    }

    public static void setExtraOption(Context context, String key, boolean val) {
        prefs(context).edit().putBoolean("exopts_" + key, val).apply();
    }

    public static void resetExtraOptions(Context context) {
        prefs(context).edit()
                .putBoolean("exopts_regional_cars", true)
                .putBoolean("exopts_lap_controller", true)
                .putBoolean("exopts_opponent_controller", true)
                .putBoolean("exopts_lap_percent", true)
                .putBoolean("exopts_drift_tweaks", true)
                .putBoolean("exopts_fixes_3d", true)
                .putBoolean("exopts_menu_enhancements", true)
                .apply();
    }

    public static void syncExtraOptionsIni(Context context) {
        try {
            java.io.File iniFile = new java.io.File(LauncherActivity.gameRoot(),
                    "scripts/NFSU2ExtraOptionsSettings.ini");
            if (!iniFile.isFile()) {
                iniFile = new java.io.File(android.os.Environment.getExternalStorageDirectory(),
                        "nfsu2/scripts/NFSU2ExtraOptionsSettings.ini");
                if (!iniFile.isFile()) {
                    iniFile = new java.io.File(android.os.Environment.getExternalStorageDirectory(),
                            "cache/scripts/NFSU2ExtraOptionsSettings.ini");
                }
            }
            if (!iniFile.isFile()) {
                java.io.File target = new java.io.File(LauncherActivity.gameRoot(), "scripts/NFSU2ExtraOptionsSettings.ini");
                if (target.getParentFile() != null && !target.getParentFile().exists()) target.getParentFile().mkdirs();
                LauncherActivity.copyAssetFile(context, "scripts/NFSU2ExtraOptionsSettings.ini", target);
                iniFile = target;
            }
            if (!iniFile.isFile()) return;

            java.util.List<String> lines = new java.util.ArrayList<>();
            try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.FileReader(iniFile))) {
                String line;
                while ((line = br.readLine()) != null) lines.add(line);
            }

            boolean regional = getExtraOption(context, "regional_cars", true);
            boolean lapCtrl = getExtraOption(context, "lap_controller", true);
            boolean oppCtrl = getExtraOption(context, "opponent_controller", true);
            boolean lapPct = getExtraOption(context, "lap_percent", true);
            boolean drift = getExtraOption(context, "drift_tweaks", true);
            boolean fixes = getExtraOption(context, "fixes_3d", true);
            boolean menu = getExtraOption(context, "menu_enhancements", true);

            java.util.List<String> out = new java.util.ArrayList<>();
            for (String l : lines) {
                String trimmed = l.trim();
                if (trimmed.startsWith("UnlockRegionalCars")) {
                    out.add("UnlockRegionalCars = " + (regional ? "1" : "0"));
                } else if (trimmed.startsWith("ShowPercentOn1LapRaces")) {
                    out.add("ShowPercentOn1LapRaces = " + (lapPct ? "1" : "0"));
                } else if (trimmed.startsWith("DisappearingWheelsFix")) {
                    out.add("DisappearingWheelsFix = " + (fixes ? "1" : "0"));
                } else if (trimmed.startsWith("DamageFix")) {
                    out.add("DamageFix = " + (fixes ? "1" : "0"));
                } else if (trimmed.startsWith("MegalowPartsFix")) {
                    out.add("MegalowPartsFix = " + (fixes ? "1" : "0"));
                } else if (trimmed.startsWith("ShowOutrun")) {
                    out.add("ShowOutrun = " + (menu ? "1" : "0"));
                } else if (trimmed.startsWith("FreeRunTrackSelect")) {
                    out.add("FreeRunTrackSelect = " + (menu ? "1" : "0"));
                } else if (trimmed.startsWith("OutrunTrackSelect")) {
                    out.add("OutrunTrackSelect = " + (menu ? "1" : "0"));
                } else if (trimmed.startsWith("ShowWithoutMultiplying")) {
                    out.add("ShowWithoutMultiplying = " + (drift ? "1" : "0"));
                } else if (trimmed.startsWith("PlusSign")) {
                    out.add("PlusSign = " + (drift ? "1" : "0"));
                } else if (trimmed.startsWith("MaximumMultiplier")) {
                    out.add("MaximumMultiplier = " + (drift ? "9" : "5"));
                } else if (trimmed.startsWith("Minimum") && !l.contains("OpponentControllers")) {
                    out.add("Minimum = " + (lapCtrl ? "0" : "1"));
                } else if (trimmed.startsWith("Maximum") && !l.contains("OpponentControllers")) {
                    out.add("Maximum = " + (lapCtrl ? "127" : "10"));
                } else {
                    out.add(l);
                }
            }

            try (java.io.PrintWriter pw = new java.io.PrintWriter(new java.io.FileWriter(iniFile))) {
                for (String ol : out) pw.println(ol);
            }
            android.util.Log.i("NFSU2", "Synchronized Extra Options settings in " + iniFile.getAbsolutePath());
        } catch (Throwable t) {
            android.util.Log.w("NFSU2", "Failed to synchronize Extra Options INI", t);
        }
    }
}
