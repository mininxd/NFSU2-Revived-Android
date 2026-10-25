package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;
import android.app.AlertDialog;
import android.widget.EditText;
import android.widget.Toast;
import org.json.JSONObject;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * On-screen touch controls matching the web port (nfsu2-web):
 * - Translucent dark surfaces with lime accent (var(--lime) #c8f088)
 * - Gas, Brake, Nitro, and Handbrake pedals on the right
 * - Buttons or Thumbstick on the left, with Start and Gear shifting
 * - Free roam action icons along the top (Pause, Camera, Look back, Reset, Map, Messages, Horn, Quick Chat)
 * - Clean D-pad and Back/OK pills in menu mode
 */
final class RacingControlsView extends View {
    interface Keys { void set(int scan, boolean down); }
    /** Touches outside every control drive the game's mouse: x/y as fractions of the view, phase 0 down, 1 move, 2 up. */
    interface Pointer { void touch(float x, float y, int phase); }

    // Special controls that do not map to a DirectInput scan code
    private static final int MODE = -1, TEXT = -2, STICK = -3, EDIT = -5, CHAT = -6;
    private static final int ROUND = 0, PEDAL = 1, WHEEL = 2, PILL = 3, DPAD = 4;

    // nfsu2-web color scheme
    private static final int COLOR_BG = 0x66000000;           // rgba(0, 0, 0, 0.4)
    private static final int COLOR_BG_PRESSED = 0xB8000000;   // rgba(0, 0, 0, 0.72)
    private static final int COLOR_STROKE = 0x33FFFFFF;       // rgba(255, 255, 255, 0.2)
    private static final int COLOR_TEXT = 0xB8FFFFFF;         // rgba(255, 255, 255, 0.72)
    private static final int COLOR_LIME = 0xFFC8F088;         // var(--lime) #c8f088
    private static final int COLOR_STROKE_PRESSED = 0x99C8F088;

    private final Keys keys;
    private Pointer pointer;
    private int pointerId = -1;
    private float pointerX, pointerY;
    private final Runnable textInput;
    private final Runnable configureTilt;
    private final Runnable configureFrameLimit;
    private String style;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    private final ArrayList<Control> controls = new ArrayList<>();
    private final HashMap<Integer, Control> fingers = new HashMap<>();

    private boolean racing;
    private boolean lastGameMode;
    private boolean editing;
    private boolean hiddenByGamepad;
    private boolean gamepadConnected;
    private Control selected;
    private int dragPointer = -1;
    private float dragX, dragY;
    private final SharedPreferences layouts;
    private float scale = 1.0f;

    private static final class Control {
        final RectF area;
        final String icon;
        final int scan, shape;
        final RectF original;
        String id;
        int held;
        float steering;

        Control(float x, float y, float w, float h, int shape, String icon, int scan) {
            area = new RectF(x, y, x + w, y + h);
            original = new RectF(area);
            this.shape = shape;
            this.icon = icon;
            this.scan = scan;
        }
    }

    RacingControlsView(Context context, Keys keys, Runnable textInput, Runnable configureTilt, Runnable configureFrameLimit) {
        super(context);
        this.keys = keys;
        this.textInput = textInput;
        this.layouts = context.getSharedPreferences("touch_layouts_v2", Context.MODE_PRIVATE);
        this.style = layouts.getString("style", "classic");
        this.configureTilt = configureTilt;
        this.configureFrameLimit = configureFrameLimit;

        setContentDescription("Game controls");
        setHapticFeedbackEnabled(true);

        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeJoin(Paint.Join.ROUND);

        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create("sans-serif", Typeface.BOLD_ITALIC));
    }

    private void add(float x, float y, float size, String icon, int scan) {
        add(x, y, size, size, ROUND, icon, scan);
    }

    private void add(float x, float y, float w, float h, int shape, String icon, int scan) {
        Control c = new Control(x, y, w, h, shape, icon, scan);
        c.id = "web." + (racing ? "race." + (style.equals("classic") ? "" : style + ".") : "menu.") + scan;
        float factor = layouts.getFloat(c.id + ".size", 1.0f);
        float cx = layouts.getFloat(c.id + ".x", c.area.centerX() / (getWidth() / scale)) * (getWidth() / scale);
        float cy = layouts.getFloat(c.id + ".y", c.area.centerY() / (getHeight() / scale)) * (getHeight() / scale);
        c.area.set(cx - w * factor / 2f, cy - h * factor / 2f, cx + w * factor / 2f, cy + h * factor / 2f);
        clamp(c);
        controls.add(c);
    }

    private void clamp(Control c) {
        float w = getWidth() / scale, h = getHeight() / scale;
        float dx = c.area.left < 4 ? 4 - c.area.left : c.area.right > w - 4 ? w - 4 - c.area.right : 0;
        float dy = c.area.top < 4 ? 4 - c.area.top : c.area.bottom > h - 4 ? h - 4 - c.area.bottom : 0;
        c.area.offset(dx, dy);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        layoutControls();
    }

    void setGameMode(boolean driving) {
        if (editing || lastGameMode == driving) return;
        lastGameMode = driving;
        racing = driving;
        layoutControls();
    }

    boolean isDriving() {
        return racing && !editing && !hiddenByGamepad;
    }

    void gamepadUsed() {
        if (!gamepadConnected || hiddenByGamepad || editing) return;
        releaseAll();
        hiddenByGamepad = true;
        invalidate();
    }

    void setGamepadConnected(boolean connected) {
        if (gamepadConnected == connected) return;
        gamepadConnected = connected;
        hiddenByGamepad = connected && !editing;
        releaseAll();
    }

    private void layoutControls() {
        releaseAll();
        controls.clear();
        selected = null;
        dragPointer = -1;
        if (getWidth() == 0 || getHeight() == 0) return;

        scale = Math.min(getResources().getDisplayMetrics().density, getWidth() / 820f);
        float w = getWidth() / scale, h = getHeight() / scale;
        float bottom = h - 16;

        // Top-right header buttons
        add(w - 56, 16, 44, "settings", EDIT);
        add(w - 110, 16, 44, "mode", MODE);

        if (racing) {
            // Top row small buttons (44dp circular)
            add(16, 16, 44, "pause", 0x01);     // Esc: Pause
            add(70, 16, 44, "camera", 0x2e);    // C: Camera
            add(124, 16, 44, "look", 0x30);     // B: Look back
            add(178, 16, 44, "reset", 0x13);    // R: Reset
            add(232, 16, 44, "map", 0x32);      // M: World Map
            add(286, 16, 44, "msgs", 0x0f);     // Tab: Messages
            add(340, 16, 44, "horn", 0x31);     // N: Horn
            add(394, 16, 44, "chat", CHAT);     // Quick chat

            // Start event button (Pill, Enter: 0x1c)
            add(20, bottom - 148, 96, 40, PILL, "start", 0x1c);

            // Manual gear shift buttons
            add(20, bottom - 200, 76, 44, ROUND, "shiftDown", 0x1d); // Left Ctrl
            add(104, bottom - 200, 76, 44, ROUND, "shiftUp", 0x2a);   // Left Shift

            if (style.equals("stick")) {
                // Thumbstick steering
                add(24, bottom - 144, 140, 140, WHEEL, "stick", STICK);
            } else {
                // Classic buttons steering (◀ ▶)
                add(18, bottom - 88, 84, 84, DPAD, "left", 0xcb);
                add(110, bottom - 88, 84, 84, DPAD, "right", 0xcd);
            }

            // Right side: Pedals and Boost (exact nfsu2-web layout)
            // Gas pedal (ArrowUp: 0xc8)
            add(w - 96, bottom - 160, 84, 156, PEDAL, "gas", 0xc8);
            // Brake pedal (ArrowDown: 0xd0)
            add(w - 188, bottom - 112, 84, 108, PEDAL, "brake", 0xd0);
            // Nitrous button (Left Alt: 0x38) - above gas
            add(w - 86, bottom - 232, 64, 64, ROUND, "nitro", 0x38);
            // Handbrake button (Space: 0x39) - beside brake
            add(w - 180, bottom - 182, 58, 58, ROUND, "hb", 0x39);
        } else {
            // Menu mode: D-pad on left, Back/OK pills on right, Keyboard center
            float pad = 52, cx = 24 + pad * 1.5f, cy = bottom - pad * 1.5f;
            add(cx - pad / 2f, cy - pad * 1.5f, pad, pad, DPAD, "up", 0xc8);
            add(cx - pad / 2f, cy + pad * 0.5f, pad, pad, DPAD, "down", 0xd0);
            add(cx - pad * 1.5f, cy - pad / 2f, pad, pad, DPAD, "left", 0xcb);
            add(cx + pad * 0.5f, cy - pad / 2f, pad, pad, DPAD, "right", 0xcd);

            // Menu actions (Pills)
            add(w - 130, bottom - 52, 114, 48, PILL, "accept", 0x1c); // Enter / OK
            add(w - 130, bottom - 112, 114, 48, PILL, "back", 0x01);   // Esc / Back
            add(w / 2f - 26, bottom - 46, 52, 42, ROUND, "keyboard", TEXT);
        }
        invalidate();
    }

    // ---- Drawing -----------------------------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (hiddenByGamepad && !editing) return;
        canvas.save();
        canvas.scale(scale, scale);
        if (editing) canvas.drawColor(0x77000000);

        for (Control c : controls) {
            drawControl(canvas, c);
        }

        if (editing) {
            float w = getWidth() / scale;
            drawToolbarButton(canvas, new RectF(w / 2f - 250, 18, w / 2f - 150, 58), "RESET");
            drawToolbarButton(canvas, new RectF(w / 2f - 142, 18, w / 2f - 102, 58), "−");
            drawToolbarButton(canvas, new RectF(w / 2f - 94, 18, w / 2f - 54, 58), "+");
            drawToolbarButton(canvas, new RectF(w / 2f - 46, 18, w / 2f + 58, 58), "SAVE");
            drawToolbarButton(canvas, new RectF(w / 2f + 66, 18, w / 2f + 170, 58), "CANCEL");
            drawToolbarButton(canvas, new RectF(w / 2f - 65, 70, w / 2f + 65, 106), "OPTIONS");
        }
        canvas.restore();
    }

    private void drawControl(Canvas canvas, Control c) {
        boolean on = c.held > 0, picked = editing && c == selected;
        float cx = c.area.centerX(), cy = c.area.centerY();
        float r = Math.min(c.area.width(), c.area.height()) / 2f;

        // Background
        fill.setColor(on ? COLOR_BG_PRESSED : COLOR_BG);
        line.setColor(picked ? 0xFFFFD18A : on ? COLOR_STROKE_PRESSED : COLOR_STROKE);
        line.setStrokeWidth(Math.max(1.5f, 2.0f));

        if (c.shape == WHEEL) {
            // Thumbstick base & knob
            canvas.drawCircle(cx, cy, r, fill);
            canvas.drawCircle(cx, cy, r - 1f, line);
            float knobR = r * 0.42f;
            float kx = cx + c.steering * (r - knobR);
            fill.setColor(on ? 0xD0182414 : 0x88000000);
            canvas.drawCircle(kx, cy, knobR, fill);
            line.setColor(on ? COLOR_LIME : 0x88FFFFFF);
            canvas.drawCircle(kx, cy, knobR, line);
            return;
        }

        float corner = 18f;
        if (c.shape == PEDAL) corner = 22f;
        else if (c.shape == PILL) corner = c.area.height() / 2f;
        else if (c.shape == DPAD) corner = 14f;
        else if (c.shape == ROUND) corner = r;

        canvas.drawRoundRect(c.area, corner, corner, fill);
        canvas.drawRoundRect(c.area, corner, corner, line);

        // Foreground content (Text or SVG icon)
        int ink = on ? COLOR_LIME : COLOR_TEXT;
        line.setColor(ink);
        fill.setColor(ink);
        text.setColor(ink);

        drawIcon(canvas, c, cx, cy, r, on);
    }

    private void drawIcon(Canvas canvas, Control c, float cx, float cy, float r, boolean on) {
        float s = r * 0.44f;
        line.setStrokeWidth(Math.max(2.0f, r * 0.08f));

        switch (c.icon) {
            case "gas":
                glyph(canvas, "GAS", cx, cy + 2f, 18f, on);
                chevron(canvas, cx, cy - s * 0.9f, s * 0.6f, true);
                break;
            case "brake":
                glyph(canvas, "BRAKE", cx, cy + 2f, 15f, on);
                chevron(canvas, cx, cy - s * 0.75f, s * 0.55f, false);
                break;
            case "nitro":
                glyph(canvas, "NOS", cx, cy, 17f, on);
                break;
            case "hb":
                text.setTextSize(12f);
                canvas.drawText("HAND", cx, cy - 3f, text);
                canvas.drawText("BRAKE", cx, cy + 12f, text);
                break;
            case "start":
                glyph(canvas, "START", cx, cy, 15f, on);
                break;
            case "accept":
                glyph(canvas, "OK", cx, cy, 16f, on);
                break;
            case "back":
                glyph(canvas, "BACK", cx, cy, 16f, on);
                break;
            case "left":
                filledTriangle(canvas, cx, cy, s * 1.1f, true);
                break;
            case "right":
                filledTriangle(canvas, cx, cy, s * 1.1f, false);
                break;
            case "up":
                chevron(canvas, cx, cy, s * 0.9f, true);
                break;
            case "down":
                chevron(canvas, cx, cy, s * 0.9f, false);
                break;
            case "shiftUp":
                chevron(canvas, cx, cy - s * 0.3f, s * 0.7f, true);
                chevron(canvas, cx, cy + s * 0.35f, s * 0.7f, true);
                break;
            case "shiftDown":
                chevron(canvas, cx, cy - s * 0.35f, s * 0.7f, false);
                chevron(canvas, cx, cy + s * 0.3f, s * 0.7f, false);
                break;
            case "pause":
                canvas.drawRoundRect(cx - s * 0.55f, cy - s * 0.7f, cx - s * 0.18f, cy + s * 0.7f, 2f, 2f, fill);
                canvas.drawRoundRect(cx + s * 0.18f, cy - s * 0.7f, cx + s * 0.55f, cy + s * 0.7f, 2f, 2f, fill);
                break;
            case "camera":
                canvas.drawRoundRect(cx - s * 0.8f, cy - s * 0.45f, cx + s * 0.8f, cy + s * 0.6f, 3f, 3f, line);
                canvas.drawRect(cx - s * 0.3f, cy - s * 0.7f, cx + s * 0.1f, cy - s * 0.45f, fill);
                canvas.drawCircle(cx, cy + s * 0.08f, s * 0.3f, line);
                break;
            case "look":
                canvas.drawLine(cx - s * 0.6f, cy, cx + s * 0.7f, cy, line);
                path.reset();
                path.moveTo(cx - s * 0.1f, cy - s * 0.5f);
                path.lineTo(cx - s * 0.6f, cy);
                path.lineTo(cx - s * 0.1f, cy + s * 0.5f);
                canvas.drawPath(path, line);
                break;
            case "reset":
                canvas.drawArc(cx - s * 0.75f, cy - s * 0.75f, cx + s * 0.75f, cy + s * 0.75f, -60, 290, false, line);
                path.reset();
                path.moveTo(cx + s * 0.2f, cy - s * 0.95f);
                path.lineTo(cx + s * 0.55f, cy - s * 0.62f);
                path.lineTo(cx + s * 0.08f, cy - s * 0.42f);
                canvas.drawPath(path, line);
                break;
            case "map":
                path.reset();
                path.moveTo(cx - s * 0.9f, cy - s * 0.55f);
                path.lineTo(cx - s * 0.3f, cy - s * 0.8f);
                path.lineTo(cx + s * 0.3f, cy - s * 0.55f);
                path.lineTo(cx + s * 0.9f, cy - s * 0.8f);
                path.lineTo(cx + s * 0.9f, cy + s * 0.55f);
                path.lineTo(cx + s * 0.3f, cy + s * 0.8f);
                path.lineTo(cx - s * 0.3f, cy + s * 0.55f);
                path.lineTo(cx - s * 0.9f, cy + s * 0.8f);
                path.close();
                canvas.drawPath(path, line);
                canvas.drawLine(cx - s * 0.3f, cy - s * 0.8f, cx - s * 0.3f, cy + s * 0.55f, line);
                canvas.drawLine(cx + s * 0.3f, cy - s * 0.55f, cx + s * 0.3f, cy + s * 0.8f, line);
                break;
            case "msgs":
                canvas.drawRoundRect(cx - s * 0.9f, cy - s * 0.6f, cx + s * 0.9f, cy + s * 0.6f, 3f, 3f, line);
                path.reset();
                path.moveTo(cx - s * 0.9f, cy - s * 0.55f);
                path.lineTo(cx, cy + s * 0.1f);
                path.lineTo(cx + s * 0.9f, cy - s * 0.55f);
                canvas.drawPath(path, line);
                break;
            case "horn":
                path.reset();
                path.moveTo(cx - s * 0.65f, cy - s * 0.3f);
                path.lineTo(cx - s * 0.25f, cy - s * 0.3f);
                path.lineTo(cx + s * 0.35f, cy - s * 0.7f);
                path.lineTo(cx + s * 0.35f, cy + s * 0.7f);
                path.lineTo(cx - s * 0.25f, cy + s * 0.3f);
                path.lineTo(cx - s * 0.65f, cy + s * 0.3f);
                path.close();
                canvas.drawPath(path, line);
                canvas.drawArc(cx + s * 0.45f, cy - s * 0.45f, cx + s * 0.85f, cy + s * 0.45f, -40, 80, false, line);
                break;
            case "chat":
                path.reset();
                path.moveTo(cx - s * 0.75f, cy - s * 0.65f);
                path.lineTo(cx + s * 0.75f, cy - s * 0.65f);
                path.lineTo(cx + s * 0.75f, cy + s * 0.35f);
                path.lineTo(cx - s * 0.15f, cy + s * 0.35f);
                path.lineTo(cx - s * 0.55f, cy + s * 0.75f);
                path.lineTo(cx - s * 0.45f, cy + s * 0.35f);
                path.lineTo(cx - s * 0.75f, cy + s * 0.35f);
                path.close();
                canvas.drawPath(path, line);
                break;
            case "mode":
                if (style.equals("stick")) {
                    canvas.drawCircle(cx, cy, s * 0.8f, line);
                    canvas.drawCircle(cx, cy, s * 0.28f, fill);
                } else {
                    filledTriangle(canvas, cx - s * 0.35f, cy, s * 0.55f, true);
                    filledTriangle(canvas, cx + s * 0.35f, cy, s * 0.55f, false);
                }
                break;
            case "settings":
                for (int i = -1; i <= 1; ++i) {
                    float y = cy + i * s * 0.6f;
                    float k = cx + (i == 0 ? s * 0.35f : -s * 0.35f);
                    canvas.drawLine(cx - s * 0.8f, y, cx + s * 0.8f, y, line);
                    canvas.drawCircle(k, y, s * 0.22f, fill);
                }
                break;
            case "keyboard":
                canvas.drawRoundRect(cx - s * 0.95f, cy - s * 0.6f, cx + s * 0.95f, cy + s * 0.6f, 3f, 3f, line);
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        canvas.drawCircle(cx - s * 0.54f + col * s * 0.36f, cy - s * 0.22f + row * s * 0.3f, s * 0.08f, fill);
                    }
                }
                canvas.drawLine(cx - s * 0.4f, cy + s * 0.34f, cx + s * 0.4f, cy + s * 0.34f, line);
                break;
            default:
                glyph(canvas, c.icon, cx, cy, r * (c.icon.length() == 1 ? 1.0f : 0.6f), on);
                break;
        }
    }

    private void chevron(Canvas canvas, float x, float y, float s, boolean up) {
        float d = up ? -1 : 1;
        path.reset();
        path.moveTo(x - s, y - d * s * 0.45f);
        path.lineTo(x, y + d * s * 0.45f);
        path.lineTo(x + s, y - d * s * 0.45f);
        canvas.drawPath(path, line);
    }

    private void filledTriangle(Canvas canvas, float x, float y, float s, boolean left) {
        float d = left ? -1 : 1;
        path.reset();
        path.moveTo(x + d * s * 0.5f, y);
        path.lineTo(x - d * s * 0.5f, y - s * 0.6f);
        path.lineTo(x - d * s * 0.5f, y + s * 0.6f);
        path.close();
        canvas.drawPath(path, fill);
    }

    private void glyph(Canvas canvas, String value, float x, float y, float size, boolean on) {
        text.setTextSize(size);
        text.setColor(on ? COLOR_LIME : COLOR_TEXT);
        canvas.drawText(value, x, y - (text.ascent() + text.descent()) / 2f, text);
    }

    private void drawToolbarButton(Canvas canvas, RectF area, String label) {
        fill.setColor(0xE014202C);
        canvas.drawRoundRect(area, 12, 12, fill);
        text.setColor(COLOR_LIME);
        text.setTextSize(label.length() > 2 ? 12 : 24);
        canvas.drawText(label, area.centerX(), area.centerY() - (text.ascent() + text.descent()) / 2f, text);
    }

    // ---- Editor ------------------------------------------------------------------------------------------

    private void resizeSelected(float change) {
        if (selected == null) return;
        float factor = Math.max(0.55f, Math.min(2.5f, selected.area.width() / selected.original.width() + change));
        float cx = selected.area.centerX(), cy = selected.area.centerY();
        float w = selected.original.width() * factor, h = selected.original.height() * factor;
        selected.area.set(cx - w / 2f, cy - h / 2f, cx + w / 2f, cy + h / 2f);
        clamp(selected);
    }

    private void saveLayout() {
        SharedPreferences.Editor save = layouts.edit();
        for (Control c : controls) {
            save.putFloat(c.id + ".x", c.area.centerX() / (getWidth() / scale))
                .putFloat(c.id + ".y", c.area.centerY() / (getHeight() / scale))
                .putFloat(c.id + ".size", c.area.width() / c.original.width());
        }
        save.apply();
    }

    private boolean editTouch(MotionEvent e) {
        int action = e.getActionMasked(), index = e.getActionIndex();
        float x = e.getX(index) / scale, y = e.getY(index) / scale, w = getWidth() / scale;
        if (action == MotionEvent.ACTION_DOWN) {
            if (x >= w / 2f - 65 && x <= w / 2f + 65 && y >= 70 && y <= 106) {
                showOptions();
                return true;
            }
            if (y >= 18 && y <= 58) {
                boolean handled = true;
                if (x >= w / 2f - 250 && x <= w / 2f - 150) {
                    for (Control c : controls) c.area.set(c.original);
                    selected = null;
                } else if (x >= w / 2f - 142 && x <= w / 2f - 102) {
                    resizeSelected(-0.1f);
                } else if (x >= w / 2f - 94 && x <= w / 2f - 54) {
                    resizeSelected(0.1f);
                } else if (x >= w / 2f - 46 && x <= w / 2f + 58) {
                    saveLayout();
                    editing = false;
                    layoutControls();
                } else if (x >= w / 2f + 66 && x <= w / 2f + 170) {
                    editing = false;
                    layoutControls();
                } else {
                    handled = false;
                }
                if (handled) {
                    invalidate();
                    return true;
                }
            }
            selected = hit(e.getX(index), e.getY(index));
            dragPointer = e.getPointerId(index);
            dragX = x;
            dragY = y;
        } else if (action == MotionEvent.ACTION_MOVE && selected != null && dragPointer >= 0) {
            int i = e.findPointerIndex(dragPointer);
            if (i >= 0) {
                x = e.getX(i) / scale;
                y = e.getY(i) / scale;
                selected.area.offset(x - dragX, y - dragY);
                clamp(selected);
                dragX = x;
                dragY = y;
            }
        } else if (action == MotionEvent.ACTION_CANCEL || action == MotionEvent.ACTION_UP ||
                (action == MotionEvent.ACTION_POINTER_UP && e.getPointerId(index) == dragPointer)) {
            dragPointer = -1;
        }
        invalidate();
        return true;
    }

    private void showOptions() {
        new AlertDialog.Builder(getContext()).setTitle("Control Options")
                .setItems(new String[]{"Steering: Buttons / Thumb stick", "Tilt steering", "FPS limit", "Save layout preset", "Load layout preset"}, (dialog, which) -> {
                    if (which == 0) chooseStyle();
                    else if (which == 1) configureTilt.run();
                    else if (which == 2) configureFrameLimit.run();
                    else if (which == 3) saveNamedLayout();
                    else loadNamedLayout();
                }).setNegativeButton("Close", null).show();
    }

    private void chooseStyle() {
        String[] ids = {"classic", "stick"};
        new AlertDialog.Builder(getContext()).setTitle("Steering Mode")
                .setSingleChoiceItems(new String[]{"Buttons (◀ ▶ arrows)", "Thumb stick (Analog stick)"},
                        style.equals("stick") ? 1 : 0, (dialog, which) -> {
                    style = ids[which];
                    layouts.edit().putString("style", style).apply();
                    layoutControls();
                    dialog.dismiss();
                }).setNegativeButton("Cancel", null).show();
    }

    private void saveNamedLayout() {
        EditText name = new EditText(getContext());
        name.setSingleLine();
        name.setHint("Layout name");
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(24)});
        AlertDialog dialog = new AlertDialog.Builder(getContext()).setTitle("Save " + (racing ? "driving" : "menu") + " layout")
                .setView(name).setPositiveButton("Save", null).setNegativeButton("Cancel", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String value = name.getText().toString().trim();
            if (value.isEmpty()) {
                name.setError("Enter a name");
                return;
            }
            try {
                JSONObject data = new JSONObject();
                data.put("style", style);
                for (Control c : controls) {
                    JSONObject item = new JSONObject();
                    item.put("x", c.area.centerX() / (getWidth() / scale));
                    item.put("y", c.area.centerY() / (getHeight() / scale));
                    item.put("size", c.area.width() / c.original.width());
                    data.put(c.id, item);
                }
                layouts.edit().putString("preset." + (racing ? "race." : "menu.") + value, data.toString()).apply();
                saveLayout();
                Toast.makeText(getContext(), "Layout saved: " + value, Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            } catch (org.json.JSONException exception) {
                name.setError("Could not save layout");
            }
        }));
        dialog.show();
    }

    private void loadNamedLayout() {
        String prefix = "preset." + (racing ? "race." : "menu.");
        ArrayList<String> names = new ArrayList<>();
        for (String key : layouts.getAll().keySet()) {
            if (key.startsWith(prefix)) names.add(key.substring(prefix.length()));
        }
        java.util.Collections.sort(names);
        if (names.isEmpty()) {
            Toast.makeText(getContext(), "No saved layouts for this mode yet", Toast.LENGTH_SHORT).show();
            return;
        }
        new AlertDialog.Builder(getContext()).setTitle("Load layout").setItems(names.toArray(new String[0]), (dialog, which) -> {
            try {
                JSONObject data = new JSONObject(layouts.getString(prefix + names.get(which), "{}"));
                style = data.optString("style", "classic");
                SharedPreferences.Editor save = layouts.edit().putString("style", style);
                java.util.Iterator<String> ids = data.keys();
                while (ids.hasNext()) {
                    String id = ids.next();
                    if (id.equals("style")) continue;
                    JSONObject item = data.getJSONObject(id);
                    save.putFloat(id + ".x", (float) item.getDouble("x"))
                        .putFloat(id + ".y", (float) item.getDouble("y"))
                        .putFloat(id + ".size", (float) item.getDouble("size"));
                }
                save.apply();
                layoutControls();
            } catch (org.json.JSONException exception) {
                Toast.makeText(getContext(), "Could not load layout", Toast.LENGTH_SHORT).show();
            }
        }).setNegativeButton("Cancel", null).show();
    }

    // ---- Input -------------------------------------------------------------------------------------------

    private Control hit(float x, float y) {
        x /= scale;
        y /= scale;
        Control best = null;
        double bestDistance = Double.MAX_VALUE;
        for (Control c : controls) {
            float cx = c.area.centerX(), cy = c.area.centerY();
            double d;
            if (c.shape == PEDAL || c.shape == DPAD || c.shape == PILL) {
                if (Math.abs(x - cx) > c.area.width() * 0.58f || Math.abs(y - cy) > c.area.height() * 0.58f) continue;
                d = Math.hypot((x - cx) / c.area.width(), (y - cy) / c.area.height());
            } else {
                float r = Math.min(c.area.width(), c.area.height()) / 2f;
                d = Math.hypot(x - cx, y - cy) / r;
                if (d > (c.shape == WHEEL ? 1.25 : 1.15)) continue;
            }
            if (d < bestDistance) {
                bestDistance = d;
                best = c;
            }
        }
        return best;
    }

    private void hold(Control c) {
        if (c == null || ++c.held != 1) return;
        if (c.scan >= 0) {
            keys.set(c.scan, true);
            // Nitro (0x38 / Alt) holds Gas (0xc8 / Up) as well, matching web touch controls!
            if (c.scan == 0x38) {
                keys.set(0xc8, true);
            }
        }
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private void release(Control c) {
        if (c != null && --c.held == 0) {
            if (c.scan >= 0) {
                keys.set(c.scan, false);
                if (c.scan == 0x38) {
                    // Release Gas unless another finger is holding it directly
                    boolean gasHeld = false;
                    for (Control other : fingers.values()) {
                        if (other != null && other.scan == 0xc8 && other.held > 0) {
                            gasHeld = true;
                            break;
                        }
                    }
                    if (!gasHeld) keys.set(0xc8, false);
                }
            } else if (c.scan == STICK) {
                c.steering = 0;
                keys.set(0xcb, false);
                keys.set(0xcd, false);
            }
        }
    }

    private void steer(Control c, float x) {
        c.steering = Math.max(-1f, Math.min(1f, (x / scale - c.area.centerX()) / (c.area.width() / 2f)));
        keys.set(0xcb, c.steering < -0.18f);
        keys.set(0xcd, c.steering > 0.18f);
    }

    void setPointer(Pointer pointer) {
        this.pointer = pointer;
    }

    private void pointerTouch(float x, float y, int phase) {
        if (pointer == null || getWidth() == 0 || getHeight() == 0) return;
        pointerX = x;
        pointerY = y;
        pointer.touch(x / getWidth(), y / getHeight(), phase);
    }

    private void pointerRelease() {
        if (pointerId >= 0) {
            pointerId = -1;
            pointerTouch(pointerX, pointerY, 2);
        }
    }

    private void releaseLiftedFingers(MotionEvent e) {
        java.util.Iterator<java.util.Map.Entry<Integer, Control>> it = fingers.entrySet().iterator();
        while (it.hasNext()) {
            java.util.Map.Entry<Integer, Control> finger = it.next();
            if (e.findPointerIndex(finger.getKey()) < 0) {
                release(finger.getValue());
                it.remove();
            }
        }
        if (pointerId >= 0 && e.findPointerIndex(pointerId) < 0) {
            pointerRelease();
        }
    }

    void releaseAll() {
        for (Control c : fingers.values()) release(c);
        fingers.clear();
        pointerRelease();
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (editing) return editTouch(e);
        int action = e.getActionMasked(), index = e.getActionIndex(), id = e.getPointerId(index);

        if (hiddenByGamepad) {
            if (action == MotionEvent.ACTION_DOWN) {
                hiddenByGamepad = false;
                invalidate();
            }
            return true;
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            releaseAll();
            return true;
        }

        if (action == MotionEvent.ACTION_DOWN) {
            releaseAll();
        } else {
            releaseLiftedFingers(e);
        }

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            Control c = hit(e.getX(index), e.getY(index));
            if (c != null && c.scan == STICK && c.held > 0) c = null;
            if (c == null && pointerId < 0 && hit(e.getX(index), e.getY(index)) == null) {
                pointerId = id;
                pointerTouch(e.getX(index), e.getY(index), 0);
            }
            release(fingers.put(id, c));
            hold(c);
            if (c != null && c.scan == STICK) steer(c, e.getX(index));
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < e.getPointerCount(); ++i) {
                int pointer = e.getPointerId(i);
                if (pointer == pointerId) {
                    pointerTouch(e.getX(i), e.getY(i), 1);
                    continue;
                }
                Control old = fingers.get(pointer);
                Control next = hit(e.getX(i), e.getY(i));
                if (old != null && old.scan == STICK) {
                    steer(old, e.getX(i));
                    continue;
                }
                // Menu-style buttons do not slide; pedals and arrows follow thumb
                if (old != null && old.scan < 0) continue;
                if (next != null && (next.scan == STICK && next.held > 0 || next.scan < 0 && next.scan != STICK)) {
                    next = null;
                }
                if (old != next) {
                    release(old);
                    fingers.put(pointer, next);
                    hold(next);
                    if (next != null && next.scan == STICK) steer(next, e.getX(i));
                }
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            if (id == pointerId) {
                pointerId = -1;
                pointerTouch(e.getX(index), e.getY(index), 2);
            }
            Control c = fingers.remove(id);
            release(c);
            if (c != null && c == hit(e.getX(index), e.getY(index))) {
                if (c.scan == MODE) {
                    style = style.equals("stick") ? "classic" : "stick";
                    layouts.edit().putString("style", style).apply();
                    layoutControls();
                } else if (c.scan == TEXT) {
                    releaseAll();
                    textInput.run();
                } else if (c.scan == EDIT) {
                    releaseAll();
                    editing = true;
                    selected = null;
                } else if (c.scan == CHAT) {
                    releaseAll();
                    AdhocRoamClient.showQuickChatDialog(getContext());
                }
                performClick();
            }
            if (action == MotionEvent.ACTION_UP) releaseAll();
        }
        invalidate();
        return true;
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }
}
