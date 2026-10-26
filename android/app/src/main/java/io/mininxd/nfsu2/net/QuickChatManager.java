package io.mininxd.nfsu2.net;

import android.app.AlertDialog;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import org.json.JSONObject;

/**
 * Handles Quick Chat dialogs, radial phrases, right-stick input gestures, and received chat toasts.
 */
public final class QuickChatManager {
    private static final String TAG = "QuickChatManager";

    public static final String[][] QUICK_CHAT_GROUPS = {
        { "Hey!", "Nice car!", "Thanks!", "Bye!" },           // Group 0: Up / Hi
        { "Yes", "No", "Sorry!", "lol" },                     // Group 1: Right / Reply
        { "Follow me!", "Wait up!", "Let's go!", "Nice!" },   // Group 2: Down / Drive
        { "Race?", "Join my race!", "gg", "One more?" }       // Group 3: Left / Race
    };
    public static final String[] GROUP_NAMES = { "Hi (↑)", "Reply (→)", "Drive (↓)", "Race (←)" };

    private static volatile Context currentContext = null;

    public static void setContext(Context context) {
        currentContext = context;
    }

    public static void handleRightStick(float x, float y) {
        RoamNativeBridge.nativeRoamStick(x, y);
    }

    public static void showQuickChatDialog(Context context) {
        if (context == null) context = currentContext;
        if (context == null) return;
        final Context ctx = context;
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                String[] categories = {
                    "1. Hi (↑)",
                    "2. Reply (→)",
                    "3. Drive (↓)",
                    "4. Race (←)"
                };
                new AlertDialog.Builder(ctx)
                    .setTitle("Quick Chat")
                    .setItems(categories, (dialog, which) -> showGroupPhrasesDialog(ctx, which))
                    .setNegativeButton("Cancel", null)
                    .show();
            } catch (Exception e) {
                Log.d(TAG, "QuickChat dialog: " + e.getMessage());
            }
        });
    }

    private static void showGroupPhrasesDialog(Context context, int groupIndex) {
        if (groupIndex < 0 || groupIndex >= QUICK_CHAT_GROUPS.length) return;
        String[] phrases = QUICK_CHAT_GROUPS[groupIndex];
        new AlertDialog.Builder(context)
            .setTitle(GROUP_NAMES[groupIndex])
            .setItems(phrases, (dialog, which) -> {
                int phraseIndex = groupIndex * 4 + which;
                sendQuickChat(phraseIndex);
            })
            .setNegativeButton("Back", (dialog, which) -> showQuickChatDialog(context))
            .show();
    }

    public static void sendQuickChat(int phraseIndex) {
        try {
            RoamNativeBridge.nativeRoamSay(phraseIndex);
            Log.i(TAG, "Sent Quick Chat phrase: " + phraseIndex);
        } catch (Throwable t) {
            Log.w(TAG, "Failed sending quick chat: " + t.getMessage());
        }
    }

    public static void onQuickChatReceived(String json) {
        if (json == null || json.isEmpty()) return;
        try {
            JSONObject obj = new JSONObject(json);
            if (obj.has("group")) {
                int group = obj.getInt("group");
                if (group >= 0 && group < GROUP_NAMES.length) {
                    StringBuilder sb = new StringBuilder();
                    sb.append("💬 Chat ").append(GROUP_NAMES[group]).append(":\n");
                    String[] phrases = QUICK_CHAT_GROUPS[group];
                    String[] arrows = { "↑", "→", "↓", "←" };
                    for (int i = 0; i < 4; i++) {
                        sb.append(arrows[i]).append(" ").append(phrases[i]).append(i < 3 ? "  |  " : "");
                    }
                    showToast(sb.toString());
                }
            } else if (obj.has("p")) {
                int p = obj.getInt("p");
                String name = obj.optString("name", "Racer");
                boolean isMe = obj.optInt("me", 0) == 1;
                int group = p / 4;
                int idx = p % 4;
                if (group >= 0 && group < QUICK_CHAT_GROUPS.length && idx >= 0 && idx < 4) {
                    String phraseText = QUICK_CHAT_GROUPS[group][idx];
                    String display = (isMe ? "You" : name) + ": \"" + phraseText + "\"";
                    showToast("💬 " + display);
                }
            }
        } catch (Exception e) {
            Log.d(TAG, "QuickChat json parse: " + e.getMessage());
        }
    }

    public static void showToast(String text) {
        Log.i(TAG, text);
        Context ctx = currentContext;
        if (ctx != null) {
            new Handler(Looper.getMainLooper()).post(() -> {
                try {
                    Toast.makeText(ctx, text, Toast.LENGTH_SHORT).show();
                } catch (Exception ignored) {}
            });
        }
    }
}
