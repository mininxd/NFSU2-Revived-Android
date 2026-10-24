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
            CUTOUT = "cutout", HUD_SAFE = "hud_safe";

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
}
