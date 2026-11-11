package io.mininxd.nfsu2;

import android.content.Context;
import android.content.SharedPreferences;
import android.app.Activity;
import android.app.AlertDialog;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONObject;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
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

    @FunctionalInterface
    interface PointerMiddle {
        void touch(float x, float y, int phase, float dx, float dy);
    }
    private PointerMiddle pointerMiddle;
    private boolean twoFingerActive = false;
    private int twoFingerId1 = -1;
    private int twoFingerId2 = -1;
    private float twoFingerLastMidX, twoFingerLastMidY;

    private int cameraTouchId = -1;
    private float cameraStartX, cameraStartY;
    private final Runnable textInput;
    private final Runnable configureTilt;
    private final Runnable configureFrameLimit;
    private String style;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    private final ArrayList<Control> controls = new ArrayList<>();
    // Zero-allocation flat primitive pointer array (0 GC overhead during 60/90/120Hz multi-touch)
    private final Control[] fingerMap = new Control[32];

    private boolean hasActiveFingers() {
        for (int i = 0; i < 32; i++) {
            if (fingerMap[i] != null) return true;
        }
        return false;
    }

    private Control getFinger(int id) {
        return (id >= 0 && id < 32) ? fingerMap[id] : null;
    }

    private Control putFinger(int id, Control c) {
        if (id < 0 || id >= 32) return null;
        Control old = fingerMap[id];
        fingerMap[id] = c;
        return old;
    }

    private Control removeFinger(int id) {
        if (id < 0 || id >= 32) return null;
        Control old = fingerMap[id];
        fingerMap[id] = null;
        return old;
    }

    private boolean racing;
    private boolean lastGameMode;
    private boolean editing;
    private boolean hiddenByGamepad;
    private boolean gamepadConnected;
    private Control selected;
    private int dragPointer = -1;
    private float dragX, dragY;
    private float dragStartX, dragStartY;
    private boolean hasMovedControl = false;
    private float controlOpacity = 1.0f;
    private float toolbarX = Float.NaN, toolbarY = Float.NaN;
    private int toolbarDragPointer = -1;
    private float toolbarDragStartX, toolbarDragStartY;
    private float toolbarDragInitX, toolbarDragInitY;
    private boolean isDraggingToolbar = false;

    public interface OnEditingChangeListener {
        void onEditingChanged(boolean editing);
    }
    private OnEditingChangeListener onEditingChangeListener;

    public void setOnEditingChangeListener(OnEditingChangeListener listener) {
        this.onEditingChangeListener = listener;
    }

    public void setEditing(boolean edit) {
        if (this.editing == edit) return;
        this.editing = edit;
        if (edit) {
            this.hasMovedControl = false;
        } else {
            this.selected = null;
            this.dragPointer = -1;
            this.isDraggingToolbar = false;
            this.toolbarDragPointer = -1;
        }
        if (onEditingChangeListener != null) {
            onEditingChangeListener.onEditingChanged(edit);
        }
    }

    public boolean isEditing() {
        return editing;
    }
    private final SharedPreferences layouts;
    private float scale = 1.0f;
    private Bitmap nitroBitmap;
    private Bitmap statusBitmap;
    private Bitmap driftBitmap;
    private Bitmap liftBitmap;
    private Bitmap bounceBitmap;
    private Bitmap resetBitmap;
    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF nitroDestRect = new RectF();
    private final RectF statusDestRect = new RectF();
    private final RectF driftDestRect = new RectF();
    private final RectF liftDestRect = new RectF();
    private final RectF bounceDestRect = new RectF();
    private final RectF resetDestRect = new RectF();
    private final PorterDuffColorFilter filterIdle = new PorterDuffColorFilter(COLOR_TEXT, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter filterActive = new PorterDuffColorFilter(COLOR_LIME, PorterDuff.Mode.SRC_IN);
    private boolean driftActive = false;
    private boolean controlsHidden = false;

    private int keyGas = 0xc8;
    private int keyBrake = 0xd0;
    private int keyLeft = 0xcb;
    private int keyRight = 0xcd;
    private int keyHandbrake = 0x39;
    private int keyNitro = 0x38;
    private int keyShiftUp = 0x2a;
    private int keyShiftDown = 0x1d;
    private int keyLookBack = 0x30;
    private int keyCamera = 0x2e;
    private int keyReset = 0x13;

    private static final class Control {
        final RectF area;
        final String icon;
        int scan;
        final int shape;
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

        try {
            int resId = getResources().getIdentifier("nitro", "drawable", getContext().getPackageName());
            if (resId != 0) {
                nitroBitmap = BitmapFactory.decodeResource(getResources(), resId);
            }
            if (nitroBitmap == null) {
                java.io.InputStream is = getContext().getAssets().open("nitro.png");
                nitroBitmap = BitmapFactory.decodeStream(is);
                is.close();
            }
        } catch (Throwable ignored) {}

        try {
            int resId = getResources().getIdentifier("status", "drawable", getContext().getPackageName());
            if (resId != 0) {
                statusBitmap = BitmapFactory.decodeResource(getResources(), resId);
            }
            if (statusBitmap == null) {
                java.io.InputStream is = getContext().getAssets().open("status.png");
                statusBitmap = BitmapFactory.decodeStream(is);
                is.close();
            }
        } catch (Throwable ignored) {}

        try {
            int resId = getResources().getIdentifier("drift", "drawable", getContext().getPackageName());
            if (resId != 0) {
                driftBitmap = BitmapFactory.decodeResource(getResources(), resId);
            }
            if (driftBitmap == null) {
                java.io.InputStream is = getContext().getAssets().open("drift.png");
                driftBitmap = BitmapFactory.decodeStream(is);
                is.close();
            }
        } catch (Throwable ignored) {}

        try {
            int resId = getResources().getIdentifier("lift", "drawable", getContext().getPackageName());
            if (resId != 0) {
                liftBitmap = BitmapFactory.decodeResource(getResources(), resId);
            }
            if (liftBitmap == null) {
                java.io.InputStream is = getContext().getAssets().open("lift.png");
                liftBitmap = BitmapFactory.decodeStream(is);
                is.close();
            }
        } catch (Throwable ignored) {}

        try {
            int resId = getResources().getIdentifier("bounce", "drawable", getContext().getPackageName());
            if (resId != 0) {
                bounceBitmap = BitmapFactory.decodeResource(getResources(), resId);
            }
            if (bounceBitmap == null) {
                java.io.InputStream is = getContext().getAssets().open("bounce.png");
                bounceBitmap = BitmapFactory.decodeStream(is);
                is.close();
            }
        } catch (Throwable ignored) {}

        try {
            int resId = getResources().getIdentifier("reset", "drawable", getContext().getPackageName());
            if (resId != 0) {
                resetBitmap = BitmapFactory.decodeResource(getResources(), resId);
            }
            if (resetBitmap == null) {
                java.io.InputStream is = getContext().getAssets().open("reset.png");
                resetBitmap = BitmapFactory.decodeStream(is);
                is.close();
            }
        } catch (Throwable ignored) {}

        controlsHidden = layouts.getBoolean("controls_hidden", false);
        controlOpacity = layouts.getFloat("control_opacity", 1.0f);
        if (controlOpacity < 0.10f || controlOpacity > 1.0f) {
            controlOpacity = Math.max(0.10f, Math.min(1.0f, controlOpacity));
        }

        setContentDescription("Game controls");
        setHapticFeedbackEnabled(true);

        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeJoin(Paint.Join.ROUND);

        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create("sans-serif", Typeface.BOLD_ITALIC));
    }

    private float getButtonScaleMultiplier() {
        String val = layouts.getString("control_size", "auto");
        if ("auto".equals(val)) {
            val = GameOptions.get(getContext(), GameOptions.TOUCH_SIZE);
        }
        if ("auto".equals(val)) {
            float minDim = Math.min(getWidth(), getHeight()) / getResources().getDisplayMetrics().density;
            if (minDim >= 600f) {
                return Math.min(1.25f, 1.0f + (minDim - 600f) / 400f * 0.25f);
            }
            return 1.0f;
        }
        try {
            return Integer.parseInt(val) / 100f;
        } catch (Exception e) {
            return 1.0f;
        }
    }

    private void add(float x, float y, float size, String icon, int scan) {
        add(x, y, size, size, ROUND, icon, scan);
    }

    private void add(float x, float y, float w, float h, int shape, String icon, int scan) {
        Control c = new Control(x, y, w, h, shape, icon, scan);
        c.id = "web." + (racing ? "race." + (style.equals("classic") ? "" : style + ".") : "menu.") + scan;
        float factor = layouts.getFloat(c.id + ".size", 1.0f);
        float multiplier = getButtonScaleMultiplier();
        float totalFactor = factor * multiplier;
        float cx = layouts.getFloat(c.id + ".x", c.area.centerX() / (getWidth() / scale)) * (getWidth() / scale);
        float cy = layouts.getFloat(c.id + ".y", c.area.centerY() / (getHeight() / scale)) * (getHeight() / scale);
        c.area.set(cx - w * totalFactor / 2f, cy - h * totalFactor / 2f, cx + w * totalFactor / 2f, cy + h * totalFactor / 2f);
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

    /** The touch controls are out of the way because a physical gamepad is in use. */
    boolean hiddenByGamepad() {
        return hiddenByGamepad;
    }

    void gamepadUsed() {
        // Fingers on the controls keep them: whatever is touched is in use.
        if (!gamepadConnected || hiddenByGamepad || editing || hasActiveFingers()) return;
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

    void updateActionKeys(int[] newKeys) {
        if (newKeys == null || newKeys.length < 11) return;
        boolean changed = (keyGas != newKeys[0] ||
                           keyBrake != newKeys[1] ||
                           keyLeft != newKeys[2] ||
                           keyRight != newKeys[3] ||
                           keyHandbrake != newKeys[4] ||
                           keyNitro != newKeys[5] ||
                           keyShiftUp != newKeys[6] ||
                           keyShiftDown != newKeys[7] ||
                           keyLookBack != newKeys[8] ||
                           keyCamera != newKeys[9] ||
                           keyReset != newKeys[10]);
        if (!changed) return;

        keyGas = newKeys[0];
        keyBrake = newKeys[1];
        keyLeft = newKeys[2];
        keyRight = newKeys[3];
        keyHandbrake = newKeys[4];
        keyNitro = newKeys[5];
        keyShiftUp = newKeys[6];
        keyShiftDown = newKeys[7];
        keyLookBack = newKeys[8];
        keyCamera = newKeys[9];
        keyReset = newKeys[10];

        if (racing) {
            for (Control c : controls) {
                if (c == null) continue;
                switch (c.icon) {
                    case "gas": c.scan = keyGas; break;
                    case "brake": c.scan = keyBrake; break;
                    case "left": c.scan = keyLeft; break;
                    case "right": c.scan = keyRight; break;
                    case "hb": c.scan = keyHandbrake; break;
                    case "nitro": c.scan = keyNitro; break;
                    case "shiftUp": c.scan = keyShiftUp; break;
                    case "shiftDown": c.scan = keyShiftDown; break;
                    case "look": c.scan = keyLookBack; break;
                    case "camera": c.scan = keyCamera; break;
                    case "reset": c.scan = keyReset; break;
                }
            }
        }
    }

    private void layoutControls() {
        releaseAll();
        controls.clear();
        selected = null;
        dragPointer = -1;
        controlOpacity = layouts.getFloat("control_opacity", 1.0f);
        if (controlOpacity < 0.10f || controlOpacity > 1.0f) {
            controlOpacity = Math.max(0.10f, Math.min(1.0f, controlOpacity));
        }
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
            add(68, 16, 44, "camera", keyCamera);
            add(120, 16, 44, "look", keyLookBack);
            add(172, 16, 44, "reset", keyReset);
            add(224, 16, 44, "map", 0x32);      // M: World Map
            add(276, 16, 44, "msgs", 0x0f);     // Tab: Messages
            add(328, 16, 44, "drift", 0x2d);    // X: Drift mode
            add(380, 16, 44, "status", 0x14);   // T: Career Status / Player Stats
            add(432, 16, 44, "lift", 0x23);     // H: Hydraulics pressurize
            add(484, 16, 44, "bounce", 0x24);   // J: Hydraulics bounce

            // Start event button (Pill, Enter: 0x1c)
            add(20, bottom - 148, 96, 40, PILL, "start", 0x1c);

            // Manual gear shift buttons
            add(20, bottom - 200, 76, 44, ROUND, "shiftDown", keyShiftDown);
            add(104, bottom - 200, 76, 44, ROUND, "shiftUp", keyShiftUp);

            if (style.equals("stick")) {
                // Thumbstick steering
                add(24, bottom - 144, 140, 140, WHEEL, "stick", STICK);
            } else {
                // Classic buttons steering (◀ ▶)
                add(18, bottom - 88, 84, 84, DPAD, "left", keyLeft);
                add(110, bottom - 88, 84, 84, DPAD, "right", keyRight);
            }

            // Right side: Pedals and Boost (exact nfsu2-web layout)
            // Gas pedal
            add(w - 96, bottom - 160, 84, 156, PEDAL, "gas", keyGas);
            // Brake pedal
            add(w - 188, bottom - 112, 84, 108, PEDAL, "brake", keyBrake);
            // Nitrous button - above gas
            add(w - 86, bottom - 232, 64, 64, ROUND, "nitro", keyNitro);
            // Handbrake button - beside brake
            add(w - 180, bottom - 182, 58, 58, ROUND, "hb", keyHandbrake);
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
            if (controlsHidden && !editing && c.scan != EDIT) continue;
            drawControl(canvas, c);
        }

        if (editing) {
            float w = getWidth() / scale, h = getHeight() / scale;
            if (Float.isNaN(toolbarX) || Float.isNaN(toolbarY)) {
                toolbarX = w / 2f;
                toolbarY = 16f;
            }
            clampToolbar(w, h);
            float cx = toolbarX, ty = toolbarY;

            // Draggable container card behind toolbar
            RectF card = new RectF(cx - 305, ty, cx + 258, ty + 96);
            fill.setColor(isDraggingToolbar ? 0xEE141C24 : 0xDD101620);
            line.setColor(isDraggingToolbar ? COLOR_LIME : 0x55FFFFFF);
            line.setStrokeWidth(1.5f);
            canvas.drawRoundRect(card, 16f, 16f, fill);
            canvas.drawRoundRect(card, 16f, 16f, line);

            // Drag handle (Hamburger icon from Google Icons)
            RectF dragHandle = new RectF(cx - 297, ty + 8, cx - 253, ty + 88);
            fill.setColor(isDraggingToolbar ? 0x55C8F088 : 0x33FFFFFF);
            line.setColor(isDraggingToolbar ? COLOR_LIME : 0x66FFFFFF);
            canvas.drawRoundRect(dragHandle, 12f, 12f, fill);
            canvas.drawRoundRect(dragHandle, 12f, 12f, line);
            drawHamburgerIcon(canvas, dragHandle, isDraggingToolbar ? COLOR_LIME : Color.WHITE);

            // Row 1 buttons (y: ty + 8 .. ty + 46, height 38)
            drawToolbarButton(canvas, new RectF(cx - 245, ty + 8, cx - 155, ty + 46), "RESET", 0xE0182028, 0x44FFFFFF, COLOR_TEXT);
            drawToolbarButton(canvas, new RectF(cx - 145, ty + 8, cx - 105, ty + 46), "−", 0xE01E2836, 0x66FFFFFF, Color.WHITE);
            drawToolbarButton(canvas, new RectF(cx - 95, ty + 8, cx - 55, ty + 46), "+", 0xE01E2836, 0x66FFFFFF, Color.WHITE);
            drawToolbarButton(canvas, new RectF(cx - 45, ty + 8, cx + 50, ty + 46), "SAVE", 0xE018301C, 0xAA88E060, COLOR_LIME);
            drawToolbarButton(canvas, new RectF(cx + 60, ty + 8, cx + 150, ty + 46), "CANCEL", 0xE0182028, 0x44FFFFFF, COLOR_TEXT);
            drawToolbarButton(canvas, new RectF(cx + 160, ty + 8, cx + 250, ty + 46), "EXIT", 0xE036181C, 0xAAFF6B6B, 0xFFFF7B7B);

            // Row 2 buttons (y: ty + 50 .. ty + 88, height 38)
            if (controlsHidden) {
                drawToolbarButton(canvas, new RectF(cx - 205, ty + 50, cx - 75, ty + 88), "SHOW BUTTON", 0xE018301C, 0xAA88E060, COLOR_LIME);
            } else {
                drawToolbarButton(canvas, new RectF(cx - 205, ty + 50, cx - 75, ty + 88), "HIDE BUTTON", 0xE0282018, 0xAAFFB74D, 0xFFFFB74D);
            }
            drawToolbarButton(canvas, new RectF(cx - 65, ty + 50, cx + 65, ty + 88), "OPTIONS", 0xE0162534, 0xAA5AC8FA, 0xFF5AC8FA);
            drawToolbarButton(canvas, new RectF(cx + 75, ty + 50, cx + 205, ty + 88), "OPACITY (" + Math.round(controlOpacity * 100) + "%)", 0xE0162534, 0xAA5AC8FA, 0xFF5AC8FA);
        }
        canvas.restore();
    }

    private static int applyAlpha(int color, float opacity) {
        if (opacity >= 0.999f) return color;
        int a = (color >>> 24);
        int modulated = Math.max(0, Math.min(255, Math.round(a * opacity)));
        return (color & 0x00FFFFFF) | (modulated << 24);
    }

    private void clampToolbar(float w, float h) {
        float minX = 310f;
        float maxX = Math.max(minX, w - 265f);
        float minY = 6f;
        float maxY = Math.max(minY, h - 102f);
        if (toolbarX < minX) toolbarX = minX;
        else if (toolbarX > maxX) toolbarX = maxX;
        if (toolbarY < minY) toolbarY = minY;
        else if (toolbarY > maxY) toolbarY = maxY;
    }

    private void drawHamburgerIcon(Canvas canvas, RectF area, int color) {
        fill.setColor(color);
        float cx = area.centerX(), cy = area.centerY();
        float barW = area.width() * 0.52f;
        float barH = Math.max(3f, area.height() * 0.055f);
        float spacing = area.height() * 0.22f;
        float r = barH / 2f;
        canvas.drawRoundRect(cx - barW / 2f, cy - spacing - barH / 2f, cx + barW / 2f, cy - spacing + barH / 2f, r, r, fill);
        canvas.drawRoundRect(cx - barW / 2f, cy - barH / 2f, cx + barW / 2f, cy + barH / 2f, r, r, fill);
        canvas.drawRoundRect(cx - barW / 2f, cy + spacing - barH / 2f, cx + barW / 2f, cy + spacing + barH / 2f, r, r, fill);
    }

    private void drawControl(Canvas canvas, Control c) {
        boolean on = c.held > 0 || (c.scan == 0x2d && driftActive), picked = editing && c == selected;
        float cx = c.area.centerX(), cy = c.area.centerY();
        float r = Math.min(c.area.width(), c.area.height()) / 2f;

        float op = (c.scan == EDIT) ? Math.max(0.40f, controlOpacity) : controlOpacity;
        float effectiveOpacity = on ? Math.min(1.0f, op * 1.4f + 0.15f) : op;
        if (editing) effectiveOpacity = 1.0f;

        // Background
        fill.setColor(applyAlpha(on ? COLOR_BG_PRESSED : COLOR_BG, effectiveOpacity));
        line.setColor(applyAlpha(picked ? 0xFFFFD18A : (on ? COLOR_STROKE_PRESSED : COLOR_STROKE), effectiveOpacity));
        line.setStrokeWidth(Math.max(1.5f, 2.0f));

        if (c.shape == WHEEL) {
            // Thumbstick base & knob
            canvas.drawCircle(cx, cy, r, fill);
            canvas.drawCircle(cx, cy, r - 1f, line);
            float knobR = r * 0.42f;
            float kx = cx + c.steering * (r - knobR);
            fill.setColor(applyAlpha(on ? 0xD0182414 : 0x88000000, effectiveOpacity));
            canvas.drawCircle(kx, cy, knobR, fill);
            line.setColor(applyAlpha(on ? COLOR_LIME : 0x88FFFFFF, effectiveOpacity));
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
        int ink = applyAlpha(on ? COLOR_LIME : COLOR_TEXT, effectiveOpacity);
        line.setColor(ink);
        fill.setColor(ink);
        text.setColor(ink);
        bitmapPaint.setAlpha(Math.max(0, Math.min(255, Math.round(255f * effectiveOpacity))));

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
                if (nitroBitmap != null) {
                    bitmapPaint.setColorFilter(on ? filterActive : filterIdle);
                    float bw = r * 1.55f;
                    float bh = bw * ((float) nitroBitmap.getHeight() / (float) nitroBitmap.getWidth());
                    nitroDestRect.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f);
                    canvas.drawBitmap(nitroBitmap, null, nitroDestRect, bitmapPaint);
                } else {
                    glyph(canvas, "NOS", cx, cy, 17f, on);
                }
                break;
            case "drift":
                if (driftBitmap != null) {
                    bitmapPaint.setColorFilter((on || driftActive) ? filterActive : filterIdle);
                    float bw = r * 1.4f;
                    float bh = bw * ((float) driftBitmap.getHeight() / (float) driftBitmap.getWidth());
                    driftDestRect.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f);
                    canvas.drawBitmap(driftBitmap, null, driftDestRect, bitmapPaint);
                } else {
                    glyph(canvas, "DRIFT", cx, cy, 11f, on || driftActive);
                }
                break;
            case "status":
                if (statusBitmap != null) {
                    bitmapPaint.setColorFilter(on ? filterActive : filterIdle);
                    float bw = r * 1.3f;
                    float bh = bw * ((float) statusBitmap.getHeight() / (float) statusBitmap.getWidth());
                    statusDestRect.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f);
                    canvas.drawBitmap(statusBitmap, null, statusDestRect, bitmapPaint);
                } else {
                    float barW = s * 0.26f;
                    canvas.drawRect(cx - s * 0.48f, cy + s * 0.15f, cx - s * 0.48f + barW, cy + s * 0.65f, fill);
                    canvas.drawRect(cx - barW / 2f, cy - s * 0.45f, cx + barW / 2f, cy + s * 0.65f, fill);
                    canvas.drawRect(cx + s * 0.48f - barW, cy - s * 0.15f, cx + s * 0.48f, cy + s * 0.65f, fill);
                }
                break;
            case "lift":
                if (liftBitmap != null) {
                    bitmapPaint.setColorFilter(on ? filterActive : filterIdle);
                    float bw = r * 1.3f;
                    float bh = bw * ((float) liftBitmap.getHeight() / (float) liftBitmap.getWidth());
                    liftDestRect.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f);
                    canvas.drawBitmap(liftBitmap, null, liftDestRect, bitmapPaint);
                } else {
                    glyph(canvas, "LIFT", cx, cy, 12f, on);
                    chevron(canvas, cx, cy - s * 0.65f, s * 0.48f, true);
                }
                break;
            case "bounce":
                if (bounceBitmap != null) {
                    bitmapPaint.setColorFilter(on ? filterActive : filterIdle);
                    float bw = r * 1.3f;
                    float bh = bw * ((float) bounceBitmap.getHeight() / (float) bounceBitmap.getWidth());
                    bounceDestRect.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f);
                    canvas.drawBitmap(bounceBitmap, null, bounceDestRect, bitmapPaint);
                } else {
                    glyph(canvas, "BOUNCE", cx, cy, 9f, on);
                    chevron(canvas, cx, cy - s * 0.65f, s * 0.42f, true);
                    chevron(canvas, cx, cy - s * 0.38f, s * 0.42f, true);
                }
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
                drawArrow(canvas, cx, cy, s * 1.15f, 2);
                break;
            case "right":
                drawArrow(canvas, cx, cy, s * 1.15f, 3);
                break;
            case "up":
                drawArrow(canvas, cx, cy, s * 1.15f, 0);
                break;
            case "down":
                drawArrow(canvas, cx, cy, s * 1.15f, 1);
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
                if (resetBitmap != null) {
                    bitmapPaint.setColorFilter(on ? filterActive : filterIdle);
                    float bw = r * 1.3f;
                    float bh = bw * ((float) resetBitmap.getHeight() / (float) resetBitmap.getWidth());
                    resetDestRect.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f);
                    canvas.drawBitmap(resetBitmap, null, resetDestRect, bitmapPaint);
                } else {
                    canvas.drawArc(cx - s * 0.75f, cy - s * 0.75f, cx + s * 0.75f, cy + s * 0.75f, -60, 290, false, line);
                    path.reset();
                    path.moveTo(cx + s * 0.2f, cy - s * 0.95f);
                    path.lineTo(cx + s * 0.55f, cy - s * 0.62f);
                    path.lineTo(cx + s * 0.08f, cy - s * 0.42f);
                    canvas.drawPath(path, line);
                }
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
            case "mode":
                if (style.equals("stick")) {
                    canvas.drawCircle(cx, cy, s * 0.8f, line);
                    canvas.drawCircle(cx, cy, s * 0.28f, fill);
                } else {
                    drawArrow(canvas, cx - s * 0.35f, cy, s * 0.55f, 2);
                    drawArrow(canvas, cx + s * 0.35f, cy, s * 0.55f, 3);
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

    private void drawArrow(Canvas canvas, float x, float y, float s, int dir) {
        // dir: 0 = UP, 1 = DOWN, 2 = LEFT, 3 = RIGHT
        float span = s * 1.15f;
        float len = s * 0.8f;
        path.reset();
        switch (dir) {
            case 0: // UP
                path.moveTo(x, y - len * 0.55f);
                path.lineTo(x + span * 0.5f, y + len * 0.45f);
                path.lineTo(x - span * 0.5f, y + len * 0.45f);
                path.close();
                break;
            case 1: // DOWN
                path.moveTo(x, y + len * 0.55f);
                path.lineTo(x + span * 0.5f, y - len * 0.45f);
                path.lineTo(x - span * 0.5f, y - len * 0.45f);
                path.close();
                break;
            case 2: // LEFT
                path.moveTo(x - len * 0.55f, y);
                path.lineTo(x + len * 0.45f, y - span * 0.5f);
                path.lineTo(x + len * 0.45f, y + span * 0.5f);
                path.close();
                break;
            case 3: // RIGHT
                path.moveTo(x + len * 0.55f, y);
                path.lineTo(x - len * 0.45f, y - span * 0.5f);
                path.lineTo(x - len * 0.45f, y + span * 0.5f);
                path.close();
                break;
        }
        canvas.drawPath(path, fill);
    }

    private void glyph(Canvas canvas, String value, float x, float y, float size, boolean on) {
        text.setTextSize(size);
        text.setColor(on ? COLOR_LIME : COLOR_TEXT);
        canvas.drawText(value, x, y - (text.ascent() + text.descent()) / 2f, text);
    }

    private void drawToolbarButton(Canvas canvas, RectF area, String label, int bgCol, int strokeCol, int textCol) {
        fill.setColor(bgCol);
        line.setColor(strokeCol);
        line.setStrokeWidth(1.5f);
        float radius = Math.min(14f, area.height() / 2f);
        canvas.drawRoundRect(area, radius, radius, fill);
        canvas.drawRoundRect(area, radius, radius, line);
        text.setColor(textCol);
        text.setTextSize(label.length() > 3 ? 12 : 20);
        canvas.drawText(label, area.centerX(), area.centerY() - (text.ascent() + text.descent()) / 2f, text);
    }

    // ---- Editor ------------------------------------------------------------------------------------------

    private void resizeSelected(float change) {
        if (selected == null) return;
        float mult = getButtonScaleMultiplier();
        float currentFactor = selected.area.width() / (selected.original.width() * mult);
        float factor = Math.max(0.55f, Math.min(3.0f, currentFactor + change));
        float cx = selected.area.centerX(), cy = selected.area.centerY();
        float w = selected.original.width() * mult * factor, h = selected.original.height() * mult * factor;
        selected.area.set(cx - w / 2f, cy - h / 2f, cx + w / 2f, cy + h / 2f);
        clamp(selected);
    }

    private void saveLayout() {
        SharedPreferences.Editor save = layouts.edit();
        float mult = getButtonScaleMultiplier();
        for (Control c : controls) {
            save.putFloat(c.id + ".x", c.area.centerX() / (getWidth() / scale))
                .putFloat(c.id + ".y", c.area.centerY() / (getHeight() / scale))
                .putFloat(c.id + ".size", c.area.width() / (c.original.width() * mult));
        }
        save.putFloat("control_opacity", controlOpacity);
        save.apply();
    }

    private void toggleControlsVisibility() {
        controlsHidden = !controlsHidden;
        layouts.edit().putBoolean("controls_hidden", controlsHidden).apply();
        setEditing(false);
        layoutControls();
        invalidate();
        Toast.makeText(getContext(), controlsHidden ? "Touch buttons hidden. Tap Settings at top right to restore." : "Touch buttons visible.", Toast.LENGTH_SHORT).show();
    }

    private boolean editTouch(MotionEvent e) {
        int action = e.getActionMasked(), index = e.getActionIndex();
        float x = e.getX(index) / scale, y = e.getY(index) / scale, w = getWidth() / scale, h = getHeight() / scale;
        if (Float.isNaN(toolbarX) || Float.isNaN(toolbarY)) {
            toolbarX = w / 2f;
            toolbarY = 16f;
        }
        clampToolbar(w, h);
        float cx = toolbarX, ty = toolbarY;

        if (action == MotionEvent.ACTION_DOWN) {
            // Check if touch is inside the floating toolbar card: [cx - 305 .. cx + 258, ty .. ty + 96]
            if (x >= cx - 305 && x <= cx + 258 && y >= ty && y <= ty + 96) {
                // Drag handle area: [cx - 305 .. cx - 250, ty .. ty + 96]
                if (x <= cx - 250) {
                    toolbarDragPointer = e.getPointerId(index);
                    toolbarDragStartX = x;
                    toolbarDragStartY = y;
                    toolbarDragInitX = toolbarX;
                    toolbarDragInitY = toolbarY;
                    isDraggingToolbar = true;
                    invalidate();
                    return true;
                }

                // Row 2 buttons: [ty + 48 .. ty + 90]
                if (y >= ty + 48 && y <= ty + 90) {
                    if (x >= cx - 205 && x <= cx - 75) {
                        toggleControlsVisibility();
                        return true;
                    }
                    if (x >= cx - 65 && x <= cx + 65) {
                        showOptions();
                        return true;
                    }
                    if (x >= cx + 75 && x <= cx + 205) {
                        chooseButtonOpacity();
                        return true;
                    }
                }

                // Row 1 buttons: [ty + 6 .. ty + 48]
                if (y >= ty + 6 && y <= ty + 48) {
                    boolean handled = true;
                    if (x >= cx - 245 && x <= cx - 155) {
                        for (Control c : controls) c.area.set(c.original);
                        selected = null;
                        hasMovedControl = true;
                    } else if (x >= cx - 145 && x <= cx - 105) {
                        resizeSelected(-0.1f);
                        hasMovedControl = true;
                    } else if (x >= cx - 95 && x <= cx - 55) {
                        resizeSelected(0.1f);
                        hasMovedControl = true;
                    } else if (x >= cx - 45 && x <= cx + 50) {
                        saveLayout();
                        setEditing(false);
                        layoutControls();
                    } else if (x >= cx + 60 && x <= cx + 150) {
                        setEditing(false);
                        layoutControls();
                    } else if (x >= cx + 160 && x <= cx + 250) {
                        confirmExitToLauncher();
                    } else {
                        handled = false;
                    }
                    if (handled) {
                        invalidate();
                        return true;
                    }
                }

                // Touched anywhere on toolbar card outside action buttons -> drag toolbar!
                toolbarDragPointer = e.getPointerId(index);
                toolbarDragStartX = x;
                toolbarDragStartY = y;
                toolbarDragInitX = toolbarX;
                toolbarDragInitY = toolbarY;
                isDraggingToolbar = true;
                invalidate();
                return true;
            }

            // Outside toolbar card: check controls
            Control hitControl = hit(e.getX(index), e.getY(index));
            if (!hasMovedControl && hitControl != null && hitControl.scan == EDIT) {
                setEditing(false);
                layoutControls();
                invalidate();
                return true;
            }
            selected = hitControl;
            dragPointer = e.getPointerId(index);
            dragX = x;
            dragY = y;
            dragStartX = x;
            dragStartY = y;
        } else if (action == MotionEvent.ACTION_MOVE) {
            if (isDraggingToolbar && toolbarDragPointer >= 0) {
                int i = e.findPointerIndex(toolbarDragPointer);
                if (i >= 0) {
                    float curX = e.getX(i) / scale, curY = e.getY(i) / scale;
                    toolbarX = toolbarDragInitX + (curX - toolbarDragStartX);
                    toolbarY = toolbarDragInitY + (curY - toolbarDragStartY);
                    clampToolbar(w, h);
                    invalidate();
                    return true;
                }
            }
            if (selected != null && dragPointer >= 0) {
                int i = e.findPointerIndex(dragPointer);
                if (i >= 0) {
                    x = e.getX(i) / scale;
                    y = e.getY(i) / scale;
                    if (!hasMovedControl && Math.hypot(x - dragStartX, y - dragStartY) > 4.0f) {
                        hasMovedControl = true;
                    }
                    selected.area.offset(x - dragX, y - dragY);
                    clamp(selected);
                    dragX = x;
                    dragY = y;
                }
            }
        } else if (action == MotionEvent.ACTION_CANCEL || action == MotionEvent.ACTION_UP) {
            if (isDraggingToolbar) {
                isDraggingToolbar = false;
                toolbarDragPointer = -1;
            }
            dragPointer = -1;
        } else if (action == MotionEvent.ACTION_POINTER_UP) {
            int pid = e.getPointerId(index);
            if (pid == toolbarDragPointer) {
                isDraggingToolbar = false;
                toolbarDragPointer = -1;
            }
            if (pid == dragPointer) {
                dragPointer = -1;
            }
        }
        invalidate();
        return true;
    }

    private void chooseButtonSize() {
        String[] ids = {"auto", "80", "100", "125", "150", "175", "200"};
        String[] labels = {"Auto (Responsive)", "80% (Compact)", "100% (Normal)", "125% (Large)", "150% (Extra Large)", "175% (Huge)", "200% (Max)"};
        String current = layouts.getString("control_size", "auto");
        int selectedIndex = 0;
        for (int i = 0; i < ids.length; i++) {
            if (ids[i].equals(current)) { selectedIndex = i; break; }
        }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Touch Button Size")
                .setSingleChoiceItems(labels, selectedIndex, (dialog, which) -> {
                    String chosen = ids[which];
                    layouts.edit().putString("control_size", chosen).apply();
                    GameOptions.set(getContext(), GameOptions.TOUCH_SIZE, chosen);
                    hasMovedControl = true;
                    layoutControls();
                    dialog.dismiss();
                }).setNegativeButton("Cancel", null).show();
    }

    private void chooseButtonOpacity() {
        Context ctx = getContext();
        float originalOpacity = controlOpacity;
        int currentPct = Math.max(10, Math.min(100, Math.round(originalOpacity * 100)));

        LinearLayout container = new LinearLayout(ctx);
        container.setOrientation(LinearLayout.VERTICAL);
        int padH = Math.round(24 * ctx.getResources().getDisplayMetrics().density);
        int padV = Math.round(16 * ctx.getResources().getDisplayMetrics().density);
        container.setPadding(padH, padV, padH, padV);

        TextView label = new TextView(ctx);
        label.setText("Opacity: " + currentPct + "%");
        label.setTextSize(16);
        label.setTextColor(Color.WHITE);
        label.setPadding(0, 0, 0, Math.round(12 * ctx.getResources().getDisplayMetrics().density));
        container.addView(label);

        SeekBar seekBar = new SeekBar(ctx);
        seekBar.setMax(90); // 10% to 100%
        seekBar.setProgress(currentPct - 10);
        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                int pct = progress + 10;
                label.setText("Opacity: " + pct + "%");
                controlOpacity = pct / 100.0f;
                invalidate();
            }

            @Override
            public void onStartTrackingTouch(SeekBar sb) {}

            @Override
            public void onStopTrackingTouch(SeekBar sb) {}
        });
        container.addView(seekBar);

        new AlertDialog.Builder(ctx, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Touch Button Opacity")
                .setView(container)
                .setPositiveButton("Apply", (d, w) -> {
                    layouts.edit().putFloat("control_opacity", controlOpacity).apply();
                    hasMovedControl = true;
                    invalidate();
                })
                .setNeutralButton("Reset (100%)", (d, w) -> {
                    controlOpacity = 1.0f;
                    layouts.edit().putFloat("control_opacity", 1.0f).apply();
                    hasMovedControl = true;
                    invalidate();
                })
                .setNegativeButton("Cancel", (d, w) -> {
                    controlOpacity = originalOpacity;
                    invalidate();
                })
                .setOnCancelListener(d -> {
                    controlOpacity = originalOpacity;
                    invalidate();
                })
                .show();
    }

    private void showOptions() {
        String currentSize = layouts.getString("control_size", "auto");
        String sizeLabel = currentSize.equals("auto") ? "Auto" : currentSize + "%";
        Context ctx = getContext();
        new AlertDialog.Builder(ctx, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Control Options")
                .setItems(new String[]{
                        "Touch Button Size (" + sizeLabel + ")",
                        "Touch Button Opacity (" + Math.round(controlOpacity * 100) + "%)",
                        "Steering: Buttons / Thumb stick",
                        "Tilt steering",
                        "FPS limit",
                        controlsHidden ? "Show Touch Buttons" : "Hide Touch Buttons",
                        "Save layout preset",
                        "Load layout preset",
                        "Exit to Launcher"
                }, (dialog, which) -> {
                    if (which == 0) chooseButtonSize();
                    else if (which == 1) chooseButtonOpacity();
                    else if (which == 2) chooseStyle();
                    else if (which == 3) configureTilt.run();
                    else if (which == 4) configureFrameLimit.run();
                    else if (which == 5) toggleControlsVisibility();
                    else if (which == 6) saveNamedLayout();
                    else if (which == 7) loadNamedLayout();
                    else if (which == 8) confirmExitToLauncher();
                })
                .setPositiveButton("Close", null)
                .setNegativeButton("Exit to Launcher", (dialog, which) -> confirmExitToLauncher())
                .show();
    }

    private void confirmExitToLauncher() {
        Context ctx = getContext();
        new AlertDialog.Builder(ctx, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Exit to Launcher")
                .setMessage("Are you sure you want to exit to the launcher?")
                .setPositiveButton("Exit", (dialog, which) -> {
                    dialog.dismiss();
                    if (ctx instanceof Activity) {
                        ((Activity) ctx).finish();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void chooseStyle() {
        String[] ids = {"classic", "stick"};
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Steering Mode")
                .setSingleChoiceItems(new String[]{"Buttons (◀ ▶ arrows)", "Thumb stick (Analog stick)"},
                        style.equals("stick") ? 1 : 0, (dialog, which) -> {
                    style = ids[which];
                    layouts.edit().putString("style", style).apply();
                    hasMovedControl = true;
                    layoutControls();
                    dialog.dismiss();
                }).setNegativeButton("Cancel", null).show();
    }

    private void saveNamedLayout() {
        EditText name = new EditText(getContext());
        name.setSingleLine();
        name.setHint("Layout name");
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(24)});
        AlertDialog dialog = new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Save " + (racing ? "driving" : "menu") + " layout")
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
                data.put("opacity", (double) controlOpacity);
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
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Load layout").setItems(names.toArray(new String[0]), (dialog, which) -> {
            try {
                JSONObject data = new JSONObject(layouts.getString(prefix + names.get(which), "{}"));
                style = data.optString("style", "classic");
                if (data.has("opacity")) {
                    controlOpacity = (float) data.optDouble("opacity", 1.0);
                    controlOpacity = Math.max(0.10f, Math.min(1.0f, controlOpacity));
                }
                SharedPreferences.Editor save = layouts.edit().putString("style", style).putFloat("control_opacity", controlOpacity);
                java.util.Iterator<String> ids = data.keys();
                while (ids.hasNext()) {
                    String id = ids.next();
                    if (id.equals("style") || id.equals("opacity")) continue;
                    JSONObject item = data.getJSONObject(id);
                    save.putFloat(id + ".x", (float) item.getDouble("x"))
                        .putFloat(id + ".y", (float) item.getDouble("y"))
                        .putFloat(id + ".size", (float) item.getDouble("size"));
                }
                save.apply();
                hasMovedControl = true;
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
        float bestDistanceSq = Float.MAX_VALUE;
        for (Control c : controls) {
            float cx = c.area.centerX(), cy = c.area.centerY();
            float dx = x - cx, dy = y - cy;
            float dSq;
            if (c.shape == PEDAL || c.shape == DPAD || c.shape == PILL) {
                float halfW = c.area.width() * 0.58f, halfH = c.area.height() * 0.58f;
                if (Math.abs(dx) > halfW || Math.abs(dy) > halfH) continue;
                float nx = dx / c.area.width(), ny = dy / c.area.height();
                dSq = nx * nx + ny * ny;
            } else {
                float r = Math.min(c.area.width(), c.area.height()) / 2f;
                float limit = (c.shape == WHEEL ? 1.25f : 1.15f) * r;
                dSq = dx * dx + dy * dy;
                if (dSq > limit * limit) continue;
            }
            if (dSq < bestDistanceSq) {
                bestDistanceSq = dSq;
                best = c;
            }
        }
        return best;
    }

    private void hold(Control c) {
        if (c == null || ++c.held != 1) return;
        if (c.scan >= 0) {
            keys.set(c.scan, true);
            // Drift mode (0x2d / X) toggle in free roam
            if (c.scan == 0x2d) {
                driftActive = !driftActive;
            }
            // Horn (0x31 / N) triggers Free Roam horn packet & audio
            if (c.scan == 0x31) {
                AdhocRoamClient.setHorn(true);
            }
            // Nitro holds Gas as well, matching web touch controls!
            if (c.scan == keyNitro) {
                keys.set(keyGas, true);
            }
        }
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private void release(Control c) {
        if (c != null && --c.held == 0) {
            if (c.scan >= 0) {
                keys.set(c.scan, false);
                if (c.scan == 0x31) {
                    AdhocRoamClient.setHorn(false);
                }
                if (c.scan == keyNitro) {
                    // Release Gas unless another finger is holding it directly
                    boolean gasHeld = false;
                    for (int i = 0; i < 32; i++) {
                        Control other = fingerMap[i];
                        if (other != null && other.scan == keyGas && other.held > 0) {
                            gasHeld = true;
                            break;
                        }
                    }
                    if (!gasHeld) keys.set(keyGas, false);
                }
            } else if (c.scan == STICK) {
                c.steering = 0;
                keys.set(keyLeft, false);
                keys.set(keyRight, false);
            }
        }
    }

    private void steer(Control c, float x) {
        c.steering = PerformanceCores.calculateSteering(x, c.area.centerX(), c.area.width() / 2f, scale);
        keys.set(keyLeft, c.steering < -0.18f);
        keys.set(keyRight, c.steering > 0.18f);
    }

    void setPointer(Pointer pointer) {
        this.pointer = pointer;
    }

    void setPointerMiddle(PointerMiddle pointerMiddle) {
        this.pointerMiddle = pointerMiddle;
    }

    private void pointerTouch(float x, float y, int phase) {
        if (pointer == null || getWidth() == 0 || getHeight() == 0) return;
        if (phase == 1 && Math.abs(x - pointerX) < 2f && Math.abs(y - pointerY) < 2f) return;
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
        for (int i = 0; i < 32; i++) {
            Control c = fingerMap[i];
            if (c != null && e.findPointerIndex(i) < 0) {
                release(c);
                fingerMap[i] = null;
            }
        }
        if (pointerId >= 0 && e.findPointerIndex(pointerId) < 0) {
            pointerRelease();
        }
        if (cameraTouchId >= 0 && e.findPointerIndex(cameraTouchId) < 0) {
            cameraTouchId = -1;
            AdhocRoamClient.handleRightStick(0f, 0f);
        }
        if (twoFingerActive && (e.getPointerCount() < 2 || (twoFingerId1 >= 0 && e.findPointerIndex(twoFingerId1) < 0) || (twoFingerId2 >= 0 && e.findPointerIndex(twoFingerId2) < 0))) {
            twoFingerActive = false;
            twoFingerId1 = -1;
            twoFingerId2 = -1;
            if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                pointerMiddle.touch(twoFingerLastMidX / getWidth(), twoFingerLastMidY / getHeight(), 2, 0f, 0f);
            }
            AdhocRoamClient.handleRightStick(0f, 0f);
        }
    }

    void releaseAll() {
        for (int i = 0; i < 32; i++) {
            Control c = fingerMap[i];
            if (c != null) {
                release(c);
                fingerMap[i] = null;
            }
        }
        if (cameraTouchId >= 0) {
            cameraTouchId = -1;
            AdhocRoamClient.handleRightStick(0f, 0f);
        }
        if (twoFingerActive) {
            twoFingerActive = false;
            twoFingerId1 = -1;
            twoFingerId2 = -1;
            if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                pointerMiddle.touch(twoFingerLastMidX / getWidth(), twoFingerLastMidY / getHeight(), 2, 0f, 0f);
            }
            AdhocRoamClient.handleRightStick(0f, 0f);
        }
        AdhocRoamClient.setHorn(false);
        pointerRelease();
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (editing) return editTouch(e);
        int action = e.getActionMasked(), index = e.getActionIndex(), id = e.getPointerId(index);

        if (controlsHidden) {
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                Control c = hit(e.getX(index), e.getY(index));
                if (c != null && c.scan == EDIT) {
                    releaseAll();
                    setEditing(true);
                    selected = null;
                    invalidate();
                    return true;
                }
            }
            if (!racing && e.getPointerCount() >= 2) {
                float p0x = e.getX(0), p0y = e.getY(0);
                float p1x = e.getX(1), p1y = e.getY(1);
                float midX = (p0x + p1x) * 0.5f;
                float midY = (p0y + p1y) * 0.5f;

                if (!twoFingerActive) {
                    pointerRelease();
                    twoFingerActive = true;
                    twoFingerId1 = e.getPointerId(0);
                    twoFingerId2 = e.getPointerId(1);
                    twoFingerLastMidX = midX;
                    twoFingerLastMidY = midY;
                    if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                        pointerMiddle.touch(midX / getWidth(), midY / getHeight(), 0, 0f, 0f);
                    }
                    return true;
                } else if (action == MotionEvent.ACTION_MOVE) {
                    float dx = (midX - twoFingerLastMidX) / (float)Math.max(1, getWidth());
                    float dy = (midY - twoFingerLastMidY) / (float)Math.max(1, getHeight());
                    twoFingerLastMidX = midX;
                    twoFingerLastMidY = midY;
                    if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                        pointerMiddle.touch(midX / getWidth(), midY / getHeight(), 1, dx, dy);
                    }
                    float stickDx = Math.max(-1.0f, Math.min(1.0f, dx * 25.0f));
                    float stickDy = Math.max(-1.0f, Math.min(1.0f, dy * 25.0f));
                    AdhocRoamClient.handleRightStick(stickDx, stickDy);
                    return true;
                }
            }
            if (twoFingerActive && (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL || e.getPointerCount() < 2)) {
                twoFingerActive = false;
                twoFingerId1 = -1;
                twoFingerId2 = -1;
                if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                    pointerMiddle.touch(twoFingerLastMidX / getWidth(), twoFingerLastMidY / getHeight(), 2, 0f, 0f);
                }
                AdhocRoamClient.handleRightStick(0f, 0f);
                return true;
            }
            if (!racing) {
                if (action == MotionEvent.ACTION_DOWN && pointerId < 0) {
                    pointerId = id;
                    pointerTouch(e.getX(index), e.getY(index), 0);
                    return true;
                } else if (action == MotionEvent.ACTION_MOVE && id == pointerId) {
                    pointerTouch(e.getX(index), e.getY(index), 1);
                    return true;
                } else if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && id == pointerId) {
                    pointerId = -1;
                    pointerTouch(e.getX(index), e.getY(index), 2);
                    return true;
                }
            }
            return true;
        }

        // A touch brings the controls back and is handled at once, not only shown.
        if (hiddenByGamepad) {
            if (action != MotionEvent.ACTION_DOWN) return true;
            hiddenByGamepad = false;
            invalidate();
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            releaseAll();
            return true;
        }

        // Two-finger car rotation in garage / frontend (matching nfsu2-web)
        if (!racing && e.getPointerCount() >= 2) {
            float p0x = e.getX(0), p0y = e.getY(0);
            float p1x = e.getX(1), p1y = e.getY(1);
            float midX = (p0x + p1x) * 0.5f;
            float midY = (p0y + p1y) * 0.5f;

            if (!twoFingerActive) {
                pointerRelease();
                twoFingerActive = true;
                twoFingerId1 = e.getPointerId(0);
                twoFingerId2 = e.getPointerId(1);
                twoFingerLastMidX = midX;
                twoFingerLastMidY = midY;
                if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                    try {
                        pointerMiddle.touch(midX / getWidth(), midY / getHeight(), 0, 0f, 0f);
                    } catch (Throwable ignored) {}
                }
                return true;
            } else if (action == MotionEvent.ACTION_MOVE) {
                float dx = (midX - twoFingerLastMidX) / (float)Math.max(1, getWidth());
                float dy = (midY - twoFingerLastMidY) / (float)Math.max(1, getHeight());
                twoFingerLastMidX = midX;
                twoFingerLastMidY = midY;
                if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                    try {
                        pointerMiddle.touch(midX / getWidth(), midY / getHeight(), 1, dx, dy);
                    } catch (Throwable ignored) {}
                }
                float stickDx = Math.max(-1.0f, Math.min(1.0f, dx * 25.0f));
                float stickDy = Math.max(-1.0f, Math.min(1.0f, dy * 25.0f));
                AdhocRoamClient.handleRightStick(stickDx, stickDy);
                return true;
            }
        }

        if (twoFingerActive && (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL || e.getPointerCount() < 2)) {
            twoFingerActive = false;
            twoFingerId1 = -1;
            twoFingerId2 = -1;
            if (pointerMiddle != null && getWidth() > 0 && getHeight() > 0) {
                try {
                    pointerMiddle.touch(twoFingerLastMidX / getWidth(), twoFingerLastMidY / getHeight(), 2, 0f, 0f);
                } catch (Throwable ignored) {}
            }
            AdhocRoamClient.handleRightStick(0f, 0f);
            if (action == MotionEvent.ACTION_UP) releaseAll();
            return true;
        }

        if (action == MotionEvent.ACTION_DOWN) {
            releaseAll();
        } else if (action == MotionEvent.ACTION_POINTER_UP) {
            releaseLiftedFingers(e);
        }

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            Control c = hit(e.getX(index), e.getY(index));
            if (c != null && c.scan == STICK && c.held > 0) c = null;
            if (c == null && hit(e.getX(index), e.getY(index)) == null) {
                if (!racing) {
                    // Right half of screen drags car camera in garage, left half does pointer tap
                    if (e.getX(index) > getWidth() * 0.50f && cameraTouchId < 0) {
                        cameraTouchId = id;
                        cameraStartX = e.getX(index);
                        cameraStartY = e.getY(index);
                    } else if (pointerId < 0) {
                        pointerId = id;
                        pointerTouch(e.getX(index), e.getY(index), 0);
                    }
                } else {
                    if (cameraTouchId < 0) {
                        cameraTouchId = id;
                        cameraStartX = e.getX(index);
                        cameraStartY = e.getY(index);
                    }
                }
            }
            release(putFinger(id, c));
            hold(c);
            if (c != null && c.scan == STICK) steer(c, e.getX(index));
        } else if (action == MotionEvent.ACTION_MOVE) {
            boolean changed = false;
            for (int i = 0; i < e.getPointerCount(); ++i) {
                int pointer = e.getPointerId(i);
                if (pointer == pointerId) {
                    if (!racing) pointerTouch(e.getX(i), e.getY(i), 1);
                    continue;
                }
                if (pointer == cameraTouchId) {
                    float dx = (e.getX(i) - cameraStartX) / (getWidth() * 0.20f);
                    float dy = (e.getY(i) - cameraStartY) / (getHeight() * 0.20f);
                    dx = Math.max(-1.0f, Math.min(1.0f, dx));
                    dy = Math.max(-1.0f, Math.min(1.0f, dy));
                    AdhocRoamClient.handleRightStick(dx, dy);
                    continue;
                }
                Control old = getFinger(pointer);
                Control next = hit(e.getX(i), e.getY(i));
                if (old != null && old.scan == STICK) {
                    float oldSteer = old.steering;
                    steer(old, e.getX(i));
                    if (Math.abs(old.steering - oldSteer) > 0.02f) changed = true;
                    continue;
                }
                // Menu-style buttons do not slide; pedals and arrows follow thumb
                if (old != null && old.scan < 0) continue;
                if (next != null && (next.scan == STICK && next.held > 0 || next.scan < 0 && next.scan != STICK)) {
                    next = null;
                }
                if (old != next) {
                    release(old);
                    putFinger(pointer, next);
                    hold(next);
                    if (next != null && next.scan == STICK) steer(next, e.getX(i));
                    changed = true;
                }
            }
            if (changed) invalidate();
            return true;
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            if (id == pointerId) {
                pointerId = -1;
                if (!racing) pointerTouch(e.getX(index), e.getY(index), 2);
            }
            if (id == cameraTouchId) {
                cameraTouchId = -1;
                AdhocRoamClient.handleRightStick(0f, 0f);
            }
            Control c = removeFinger(id);
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
                    setEditing(true);
                    selected = null;
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
