package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Launcher options. GameActivity reads them when the game process starts and hands them to the native
 * runtime before the guest boots; changing one takes effect on the next PLAY.
 */
final class GameOptions {
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

    static final String RESOLUTION = "resolution", FPS = "fps", WIDESCREEN = "widescreen", LATENCY = "latency",
            TEXTURES = "textures", MINIMAP = "minimap", LANGUAGE = "language",
            CUTOUT = "cutout", HUD_SAFE = "hud_safe", DISPLAY_LOGS = "display_logs";

    static final Option[] ALL = {
            new Option(RESOLUTION, "Internal Resolution", "75",
                    new String[] {"25", "30", "35", "40", "45", "50", "55", "60", "65", "70", "75", "80", "85", "90", "95", "100"},
                    new String[] {"25%", "30%", "35%", "40%", "45%", "50%", "55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100%"}),
            new Option(CUTOUT, "Display Cutout (Notch)", "fullscreen",
                    new String[] {"fullscreen", "safe"},
                    new String[] {"Full Screen · bypass notch", "Letterbox · avoid cutout"}),
            new Option(HUD_SAFE, "HUD Safe Area (20:9 / Notch)", "on",
                    new String[] {"on", "off"},
                    new String[] {"16:9 Centered · safe from notch", "Screen edges · ultra-wide"}),
            new Option(FPS, "FPS Limit", "120",
                    new String[] {"30", "60", "90", "120", "0"},
                    new String[] {"30 FPS · battery saver", "60 FPS", "90 FPS", "120 FPS", "Uncapped"}),
            new Option(DISPLAY_LOGS, "Display Logs", "off",
                    new String[] {"off", "compact", "full"},
                    new String[] {"Off", "Compact", "Full"}),
            new Option(WIDESCREEN, "Widescreen", "on",
                    new String[] {"on", "off"},
                    new String[] {"Corrected HUD and camera", "Original 4:3 stretched"}),
            new Option(MINIMAP, "Minimap", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Top left", "Bottom left (original)"}),
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
        for (Option o : ALL) if (o.key.equals(key)) return o;
        throw new IllegalArgumentException(key);
    }

    static String get(Context context, String key) {
        Option o = find(key);
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
    static final String DEFAULT_ROAM_SERVER = "adhoc.eahub.eu";
    static final String DEFAULT_ROAM_ROOM = "FREEROAM";
    static final int ADHOCCTL_PORT = 27312;
    static final int POSTOFFICE_PORT = 27313;

    static boolean isRoamEnabled(Context context) {
        return prefs(context).getBoolean(ROAM_ENABLED, false);
    }

    static void setRoamEnabled(Context context, boolean enabled) {
        prefs(context).edit().putBoolean(ROAM_ENABLED, enabled).apply();
    }

    static String roamServer(Context context) {
        return DEFAULT_ROAM_SERVER;
    }

    static String roamRoom(Context context) {
        return DEFAULT_ROAM_ROOM;
    }

    static String roamName(Context context) {
        return detectSavegamePlayerName();
    }

    static String detectSavegamePlayerName() {
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

    /** EA Nation replacement server: the game's *.ea.com hosts resolve to this name or IPv4 address. */
    static String onlineServer(Context context) {
        String value = prefs(context).getString(ONLINE_SERVER, DEFAULT_ONLINE_SERVER);
        return validServer(value) ? value : DEFAULT_ONLINE_SERVER;
    }

    static boolean validServer(String value) {
        return value != null && value.length() <= 253 && value.matches("[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*");
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
        return prefs(context).getBoolean("cheat_" + id, false);
    }

    static void setCheatEnabled(Context context, String id, boolean enabled) {
        prefs(context).edit().putBoolean("cheat_" + id, enabled).apply();
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
}
