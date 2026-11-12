package io.mininxd.nfsu2;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.content.Intent;
import android.net.Uri;
import android.Manifest;
import android.content.pm.PackageManager;
import android.widget.Toast;
import android.widget.TextView;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.PopupWindow;
import android.graphics.Color;
import android.widget.LinearLayout;
import android.widget.EditText;
import android.widget.CheckBox;
import android.app.AlertDialog;
import android.text.InputFilter;
import android.text.InputType;
import android.view.inputmethod.EditorInfo;
import android.util.DisplayMetrics;
import android.util.Log;

/** Storage permission gateway for the fixed Internal storage/nfsu2 directory. */
public final class GameActivity extends NativeActivity {
    private static volatile GameActivity s_instance = null;
    private static volatile String s_cachedNews = null;
    private static final String DEFAULT_NEWS_CONTENT =
        " %{START CMD=news TITLE=\"NFSU2 Revived Core\" BTN1=\"OK\" BTN1-GOTO=\"$exit=0\" %}\r\n" +
        "Welcome to Need for Speed Underground 2 Revived.\r\n\r\n" +
        "Engine Architecture & Core:\r\n" +
        "- Lifted Core: Original SPEED2.EXE x86 binary statically lifted to native 64-bit ARM64 C and Rust.\r\n" +
        "- Graphics Pipeline: Mobile DXVK Vulkan & GLES translation with dynamic GPU command deduplication.\r\n" +
        "- Audio Subsystem: Low-latency AAudio driver with DirectSound buffer cursor tracking running on pure Rust.\r\n" +
        "- Preservation Project: Developed as an open-source fan preservation effort. Source code available on GitHub:\r\n" +
        "  https://github.com/mininxd/NFSU2-Revived-Android\r\n\r\n" +
        "See you on the streets of Bayview.\r\n" +
        "%{%}";

    static {
        try {
            System.loadLibrary("game_core");
        } catch (Throwable ignored) {}
        try {
            System.loadLibrary("graphics_cache");
        } catch (Throwable ignored) {}
        System.loadLibrary("nfsu2_android");
        try {
            System.loadLibrary("nfsu2_roam");
        } catch (Throwable ignored) {}
    }
    private boolean requested;
    private boolean foreground;
    private TextView status;
    private PopupWindow statusWindow;

    private android.media.AudioManager audioManager;
    private final android.media.AudioManager.OnAudioFocusChangeListener audioFocusListener = focusChange -> {
        switch (focusChange) {
            case android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
            case android.media.AudioManager.AUDIOFOCUS_LOSS:
                GameLog.w("Audio", "Audio focus lost (" + focusChange + "), incoming call or media switch active");
                try { nativeSetAudioMuted(true); } catch (Throwable ignored) {}
                break;
            case android.media.AudioManager.AUDIOFOCUS_GAIN:
            case android.media.AudioManager.AUDIOFOCUS_GAIN_TRANSIENT:
            case android.media.AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK:
                GameLog.i("Audio", "Audio focus restored (" + focusChange + ")");
                try {
                    nativeSetAudioMuted(false);
                    nativeRequestAudioRestart();
                } catch (Throwable ignored) {}
                break;
        }
    };

    private android.media.AudioDeviceCallback audioDeviceCallback;
    private boolean audioDeviceCallbackRegistered;

    private void registerAudioRouteCallback() {
        if (audioDeviceCallbackRegistered) return;
        try {
            if (audioManager == null) {
                audioManager = (android.media.AudioManager) getSystemService(AUDIO_SERVICE);
            }
            if (audioManager == null || android.os.Build.VERSION.SDK_INT < 23) return;
            if (audioDeviceCallback == null) {
                audioDeviceCallback = new android.media.AudioDeviceCallback() {
                    @Override public void onAudioDevicesAdded(android.media.AudioDeviceInfo[] added) {
                        GameLog.i("Audio", "Audio device added, requesting AAudio restart");
                        try { nativeRequestAudioRestart(); } catch (Throwable ignored) {}
                    }
                    @Override public void onAudioDevicesRemoved(android.media.AudioDeviceInfo[] removed) {
                        GameLog.i("Audio", "Audio device removed, requesting AAudio restart");
                        try { nativeRequestAudioRestart(); } catch (Throwable ignored) {}
                    }
                };
            }
            audioManager.registerAudioDeviceCallback(audioDeviceCallback, new android.os.Handler(getMainLooper()));
            audioDeviceCallbackRegistered = true;
        } catch (Throwable t) {
            GameLog.w("Audio", "Could not register audio device callback: " + t.getMessage());
        }
    }

    private void requestGameAudioFocus() {
        try {
            if (audioManager == null) {
                audioManager = (android.media.AudioManager) getSystemService(AUDIO_SERVICE);
            }
            if (audioManager != null) {
                if (Build.VERSION.SDK_INT >= 26) {
                    android.media.AudioFocusRequest request = new android.media.AudioFocusRequest.Builder(android.media.AudioManager.AUDIOFOCUS_GAIN)
                            .setAudioAttributes(new android.media.AudioAttributes.Builder()
                                    .setUsage(android.media.AudioAttributes.USAGE_GAME)
                                    .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                                    .build())
                            .setOnAudioFocusChangeListener(audioFocusListener)
                            .build();
                    audioManager.requestAudioFocus(request);
                } else {
                    audioManager.requestAudioFocus(audioFocusListener, android.media.AudioManager.STREAM_MUSIC, android.media.AudioManager.AUDIOFOCUS_GAIN);
                }
                registerAudioRouteCallback();
            }
        } catch (Throwable t) {
            GameLog.w("Audio", "Could not request audio focus: " + t.getMessage());
        }
    }
    private PopupWindow controlsWindow;
    private DiagnosticOverlay diagnosticOverlay;
    private RacingControlsView touchControls;
    private boolean statusHidden;
    private Runnable statusHideTask;
    private final boolean[] touchKeys=new boolean[256],sentKeys=new boolean[256];
    private boolean tiltLeft,tiltRight,tiltEnabled,tiltCalibrate=true;
    private float tiltFiltered,tiltNeutral,tiltSensitivity=1;
    private boolean tiltInvert;
    private android.hardware.SensorManager tiltSensors;
    private android.hardware.Sensor tiltSensor;
    private final android.hardware.SensorEventListener tiltListener=new android.hardware.SensorEventListener() {
        @Override public void onAccuracyChanged(android.hardware.Sensor sensor,int accuracy) {}
        @Override public void onSensorChanged(android.hardware.SensorEvent event) {
            int rotation=getWindowManager().getDefaultDisplay().getRotation();
            float horizontal=rotation==android.view.Surface.ROTATION_90?-event.values[1]:
                    rotation==android.view.Surface.ROTATION_270?event.values[1]:event.values[0];
            if(tiltCalibrate) {tiltNeutral=horizontal;tiltFiltered=horizontal;tiltCalibrate=false;}
            tiltFiltered+=.2f*(horizontal-tiltFiltered);
            float turn=(tiltFiltered-tiltNeutral)*tiltSensitivity*(tiltInvert?-1:1);
            if(!foreground||!hasWindowFocus()||!tiltEnabled||touchControls==null||!touchControls.isDriving()) {setTiltKeys(false,false);return;}
            setTiltKeys(turn<-(tiltLeft?.5f:.8f),turn>(tiltRight?.5f:.8f));
        }
    };
    private int keyLeft = 0xcb;
    private int keyRight = 0xcd;

    private void sendCombinedKey(int scan) {
        boolean held=touchKeys[scan]||(scan==keyLeft&&tiltLeft)||(scan==keyRight&&tiltRight);
        if(sentKeys[scan]!=held) {sentKeys[scan]=held;nativeKey(scan,held);}
    }
    private void setTouchKey(int scan,boolean down) {touchKeys[scan]=down;sendCombinedKey(scan);}
    private void setTiltKeys(boolean left,boolean right) {tiltLeft=left;tiltRight=right;sendCombinedKey(keyLeft);sendCombinedKey(keyRight);}
    private void updateTiltSensor() {
        if(tiltSensors==null)return;tiltSensors.unregisterListener(tiltListener);setTiltKeys(false,false);
        if(foreground&&tiltEnabled&&tiltSensor!=null) {
            tiltCalibrate=true;tiltSensors.registerListener(tiltListener,tiltSensor,android.hardware.SensorManager.SENSOR_DELAY_GAME);
        }
    }
    private void showTiltOptions() {
        if(tiltSensor==null) {Toast.makeText(this,"This device does not have an accelerometer",Toast.LENGTH_LONG).show();return;}
        android.content.SharedPreferences options=getSharedPreferences("touch_layouts_v1",MODE_PRIVATE);
        LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(dp(24),dp(8),dp(24),dp(8));
        CheckBox enabled=new CheckBox(this);enabled.setText("Steer by tilting device");enabled.setChecked(tiltEnabled);form.addView(enabled);
        CheckBox invert=new CheckBox(this);invert.setText("Invert steering direction");invert.setChecked(tiltInvert);form.addView(invert);
        TextView label=new TextView(this);label.setText("Sensitivity: "+tiltSensitivity+"×");form.addView(label);
        android.widget.SeekBar sensitivity=new android.widget.SeekBar(this);sensitivity.setMax(30);sensitivity.setProgress(Math.round(tiltSensitivity*10)-10);form.addView(sensitivity);
        sensitivity.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            public void onStartTrackingTouch(android.widget.SeekBar bar) {}public void onStopTrackingTouch(android.widget.SeekBar bar) {}
            public void onProgressChanged(android.widget.SeekBar bar,int progress,boolean user) {label.setText("Sensitivity: "+((progress+10)/10f)+"×");}
        });
        TextView help=new TextView(this);help.setText("Hold the device in your playing position when applying. Tilting steers left/right; pedals remain on screen.");form.addView(help);
        new AlertDialog.Builder(this).setTitle("Tilt Steering").setView(form)
                .setPositiveButton("Apply and calibrate",(dialog,which)->{
                    tiltEnabled=enabled.isChecked();tiltInvert=invert.isChecked();tiltSensitivity=(sensitivity.getProgress()+10)/10f;
                    options.edit().putBoolean("tilt.enabled",tiltEnabled).putBoolean("tilt.invert",tiltInvert).putFloat("tilt.sensitivity",tiltSensitivity).apply();
                    updateTiltSensor();
                }).setNegativeButton("Cancel",null).show();
    }
    private android.hardware.input.InputManager inputManager;
    private final android.hardware.input.InputManager.InputDeviceListener gamepadListener=new android.hardware.input.InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int id) {updateGamepads();}
        @Override public void onInputDeviceRemoved(int id) {updateGamepads();}
        @Override public void onInputDeviceChanged(int id) {updateGamepads();}
    };
    /**
     * Physical gamepads (Bluetooth or USB) drive the game; the touch layer hides while one is connected.
     * Only external devices count: built-in sensors and side keys of several MediaTek phones report
     * themselves as joysticks with X/Y axes, and their resting value used to steer the car by itself.
     * Pads in "keyboard" mode report only KEYBOARD but still have gamepad buttons.
     */
    private void updateGamepads() {
        boolean connected=false;
        java.util.ArrayList<Integer> accepted=new java.util.ArrayList<>();
        StringBuilder seen=new StringBuilder();
        for(int id:android.view.InputDevice.getDeviceIds()) {
            android.view.InputDevice device=android.view.InputDevice.getDevice(id);
            if(device==null||device.isVirtual())continue;
            int sources=device.getSources();
            boolean buttons=(sources&android.view.InputDevice.SOURCE_GAMEPAD)==android.view.InputDevice.SOURCE_GAMEPAD;
            // Some fingerprint sensors advertise JOYSTICK with only a Z axis.
            boolean stick=(sources&android.view.InputDevice.SOURCE_JOYSTICK)==android.view.InputDevice.SOURCE_JOYSTICK&&
                    device.getMotionRange(android.view.MotionEvent.AXIS_X)!=null&&
                    device.getMotionRange(android.view.MotionEvent.AXIS_Y)!=null;
            boolean keys=(sources&android.view.InputDevice.SOURCE_KEYBOARD)==android.view.InputDevice.SOURCE_KEYBOARD&&
                    device.getKeyboardType()!=android.view.InputDevice.KEYBOARD_TYPE_ALPHABETIC&&
                    device.hasKeys(android.view.KeyEvent.KEYCODE_BUTTON_A)[0];
            // Accept external controllers as well as built-in handhelds (Retroid, Odin, etc.)
            // and USB controllers that report isExternal() == false on Xiaomi / HyperOS.
            boolean pad=(device.isExternal()||buttons||stick)&&(buttons||stick||keys);
            seen.append(seen.length()==0?"":"; ").append(device.getName()).append(" [").append(Integer.toHexString(sources))
                    .append(device.isExternal()?" external":" internal").append(pad?" pad":"").append(']');
            if(pad){connected=true;accepted.add(id);}
        }
        android.util.Log.i("NFS","Input devices: "+seen);
        // Only these devices may move the guest's stick or press its keys.
        int[] ids=new int[accepted.size()];for(int i=0;i<ids.length;++i)ids[i]=accepted.get(i);
        nativeGamepadDevices(ids);
        if(touchControls!=null)touchControls.setGamepadConnected(connected);
        nativeGamepadConnected(connected);
    }
    private final Runnable updateControlMode = new Runnable() {
        @Override public void run() {
            if(!foreground||touchControls==null)return;
            touchControls.setGameMode(nativeDrivingControls());
            // Playing on with the pad after a touch revealed the controls hides them again.
            if(nativeGamepadUsed())touchControls.gamepadUsed();
            updateHudForGamepad(touchControls.hiddenByGamepad());
            if (io.mininxd.nfsu2.net.RoamNativeBridge.isLoaded()) {
                try {
                    int[] actionKeys = io.mininxd.nfsu2.net.RoamNativeBridge.nativeGetActionKeys();
                    if (actionKeys != null && actionKeys.length >= 11) {
                        keyLeft = actionKeys[2];
                        keyRight = actionKeys[3];
                        touchControls.updateActionKeys(actionKeys);
                    }
                } catch (Throwable ignored) {}
            }
            touchControls.postDelayed(this,250);
        }
    };
    private static void nativeKey(int scan, boolean down) {
        com.nfsu2.androidevolved.GameActivity.nativeKey(scan, down);
        try { nativeKeyNav(scan, down); } catch (Throwable ignored) {}
    }
    private static native void nativeKeyNav(int scan, boolean down);
    private static boolean nativeDrivingControls() { return com.nfsu2.androidevolved.GameActivity.nativeDrivingControls(); }
    private static boolean nativeGamepadUsed() { return com.nfsu2.androidevolved.GameActivity.nativeGamepadUsed(); }
    private static void nativeTypeText(String value, boolean replace) { com.nfsu2.androidevolved.GameActivity.nativeTypeText(value, replace); }
    private static void nativePointer(float x, float y, int phase) { com.nfsu2.androidevolved.GameActivity.nativePointer(x, y, phase); }
    static void nativePointerMiddle(float x, float y, int phase, float dx, float dy) {
        try {
            nativeDirectPointerMiddle(x, y, phase, dx, dy);
        } catch (Throwable ignored) {}
        try {
            nativeGarageTwoFingerDrag(dx, dy, phase != 2);
        } catch (Throwable ignored) {}
        try {
            com.nfsu2.androidevolved.GameActivity.nativePointerMiddle(x, y, phase, dx, dy);
        } catch (Throwable ignored) {}
    }
    private static native void nativeDirectPointerMiddle(float x, float y, int phase, float dx, float dy);
    private static native void nativeGarageTwoFingerDrag(float dx, float dy, boolean active);
    private static void nativeLanguage(String language) { com.nfsu2.androidevolved.GameActivity.nativeLanguage(language); }
    private static void nativeResolution(int width, int height) { com.nfsu2.androidevolved.GameActivity.nativeResolution(width, height); }
    private static void nativeFrameLimit(int framesPerSecond) {
        try {
            com.nfsu2.androidevolved.GameActivity.nativeFrameLimit(framesPerSecond);
        } catch (Throwable ignored) {}
        try {
            android.system.Os.setenv("NFSU2_FPS", String.valueOf(framesPerSecond), true);
        } catch (Throwable ignored) {}
        try {
            if (io.mininxd.nfsu2.net.RoamNativeBridge.isLoaded()) {
                io.mininxd.nfsu2.net.RoamNativeBridge.nativeSetFrameLimit(framesPerSecond);
            }
        } catch (Throwable ignored) {}
    }
    private static void nativeWidescreen(boolean enabled) { com.nfsu2.androidevolved.GameActivity.nativeWidescreen(enabled); }
    private static void nativeScreenMode(int mode) {
        try { com.nfsu2.androidevolved.GameActivity.nativeScreenMode(mode); }
        catch (Throwable t) { android.util.Log.w("GameActivity", "nativeScreenMode: " + t.getMessage()); }
    }
    private static void nativeDrawDistance(float distance) {
        try {
            com.nfsu2.androidevolved.GameActivity.nativeDrawDistance(distance);
        } catch (Throwable ignored) {}
    }
    private static void nativeForwardCulling(boolean enabled) {
        try {
            com.nfsu2.androidevolved.GameActivity.nativeForwardCulling(enabled);
        } catch (Throwable ignored) {}
    }
    private static void nativeOnlineServer(String host) {
        try {
            com.nfsu2.androidevolved.GameActivity.nativeOnlineServer(host);
        } catch (Throwable t) {
            android.util.Log.w("GameActivity", "nativeOnlineServer: " + t.getMessage());
        }
    }
    private static void nativeGamepadConnected(boolean connected) { com.nfsu2.androidevolved.GameActivity.nativeGamepadConnected(connected); }
    private static void nativeGamepadDevices(int[] ids) { com.nfsu2.androidevolved.GameActivity.nativeGamepadDevices(ids); }
    private static void nativeMinimapTop(boolean top) { com.nfsu2.androidevolved.GameActivity.nativeMinimapTop(top); }
    private static void nativeGamepadLayout(boolean playstationNames) {
        try {
            com.nfsu2.androidevolved.GameActivity.nativeGamepadLayout(playstationNames);
            android.util.Log.i("NFSU2", playstationNames
                    ? "Gamepad labels: PS2 Cross Circle Square Triangle L1 L2 L3"
                    : "Gamepad labels: Xbox A B X Y LB RB");
        } catch (UnsatisfiedLinkError missing) {
            android.util.Log.w("NFSU2", "Gamepad layout option needs a newer libnfsu2_android.so");
        }
    }
    private static void nativeSpeedometerPosition(int position) {
        try { com.nfsu2.androidevolved.GameActivity.nativeSpeedometerPosition(position); }
        catch (UnsatisfiedLinkError missing) { android.util.Log.w("NFSU2", "Speedometer position needs a newer libnfsu2_android.so"); }
    }
    // The minimap and speedometer moves make room for the touch controls. Both
    // follow them live: original places while a gamepad hides them, the
    // launcher's when a touch shows them (the game rebuilds its HUD).
    private boolean minimapTop;
    private int speedometerPosition;
    private Boolean hudForGamepad;
    private void updateHudForGamepad(boolean gamepad) {
        if (hudForGamepad != null && hudForGamepad == gamepad) return;
        hudForGamepad = gamepad;
        nativeSpeedometerPosition(gamepad ? 0 : speedometerPosition);
        nativeMinimapTop(minimapTop);
    }
    private static void nativeBackBuffers(int count) { com.nfsu2.androidevolved.GameActivity.nativeBackBuffers(count); }
    private static void nativeCpuTextures(boolean enabled) { com.nfsu2.androidevolved.GameActivity.nativeCpuTextures(enabled); }
    // Older libnfsu2_android.so builds have no region switch: keep the game's region.
    private static void nativeRegion(int region) {
        try { com.nfsu2.androidevolved.GameActivity.nativeRegion(region); }
        catch (UnsatisfiedLinkError missing) { android.util.Log.w("NFSU2", "Region option needs a newer libnfsu2_android.so"); }
    }
    private static void nativeGpuDriver(String hooks, String temp, String directory, String library) {
        try {
            com.nfsu2.androidevolved.GameActivity.nativeGpuDriver(hooks, temp, directory, library);
        } catch (Throwable t) {
            android.util.Log.w("GameActivity", "nativeGpuDriver: " + t.getMessage());
        }
    }
    private static native void nativeSetAudioMuted(boolean muted);
    private static native void nativeRequestAudioRestart();
    private void showFrameLimitOptions() {
        GameOptions.Option option=GameOptions.find(GameOptions.FPS);
        String saved=GameOptions.get(this,GameOptions.FPS);int selected=0;
        for(int i=0;i<option.values.length;i++)if(option.values[i].equals(saved))selected=i;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(option.title)
                .setSingleChoiceItems(option.labels,selected,(dialog,which)->{
                    GameOptions.set(this,GameOptions.FPS,option.values[which]);
                    nativeFrameLimit(Integer.parseInt(option.values[which]));dialog.dismiss();
                }).setNegativeButton("Cancel",null).show();
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void showTextInput() {
        EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setHint("Name, email or password");
        name.setFilters(new InputFilter[]{new InputFilter.LengthFilter(40)});
        name.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD |
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        name.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        CheckBox replace = new CheckBox(this);
        replace.setText("Replace current name");replace.setChecked(true);
        LinearLayout form = new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(20), 0, dp(20), 0);form.addView(name);form.addView(replace);
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Type in game")
                .setView(form)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Type", null)
                .create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
            String value = name.getText().toString();
            if (value.isEmpty() || !value.matches("[A-Za-z0-9 .,=/;_+?:!@#$%&*()-]{1,40}")) {
                name.setError("Up to 40 characters: letters, numbers and . , - _ @ + ! # $ % & * ( ) = ? / ; :");
                return;
            }
            nativeTypeText(value, replace.isChecked());
            dialog.dismiss();
        }));
        dialog.show();
        name.requestFocus();
        dialog.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
    }
    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        s_instance = this;
        refreshNewsAsync();
        GameOptions.syncWidescreenIni(this);
        AllCpuCoresManager.init(this);
        GamePerformanceBooster.apply(this, getWindow());
        PerformanceCores.bindToPerformanceCores();
        GameLog.init(this);
        io.mininxd.nfsu2.net.NetLog.init(this);
        inputManager=(android.hardware.input.InputManager)getSystemService(INPUT_SERVICE);
        tiltSensors=(android.hardware.SensorManager)getSystemService(SENSOR_SERVICE);
        tiltSensor=tiltSensors==null?null:tiltSensors.getDefaultSensor(android.hardware.Sensor.TYPE_ACCELEROMETER);
        android.content.SharedPreferences options=getSharedPreferences("touch_layouts_v1",MODE_PRIVATE);
        tiltEnabled=options.getBoolean("tilt.enabled",false);tiltInvert=options.getBoolean("tilt.invert",false);
        tiltSensitivity=options.getFloat("tilt.sensitivity",1);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        try {
            getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS |
                    android.view.WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION);
            getWindow().setStatusBarColor(android.graphics.Color.TRANSPARENT);
            getWindow().setNavigationBarColor(android.graphics.Color.TRANSPARENT);
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().setDecorFitsSystemWindows(false);
            }
        } catch (Throwable ignored) {}
        applyDisplayRefreshRate();
        Intent launch = getIntent();
        float distVal = 450.0f;
        try {
            String menuScale = GameOptions.get(this, GameOptions.MENU_BUTTON_SIZE);
            if ("auto".equals(menuScale) || "100".equals(menuScale)) android.system.Os.setenv("NFSU2_MENU_SCALE", "1.0", true);
            else if ("125".equals(menuScale)) android.system.Os.setenv("NFSU2_MENU_SCALE", "1.25", true);
            else if ("150".equals(menuScale)) android.system.Os.setenv("NFSU2_MENU_SCALE", "1.5", true);
            else if ("175".equals(menuScale)) android.system.Os.setenv("NFSU2_MENU_SCALE", "1.75", true);
            else if ("200".equals(menuScale)) android.system.Os.setenv("NFSU2_MENU_SCALE", "2.0", true);
            else android.system.Os.setenv("NFSU2_MENU_SCALE", menuScale, true);

            try {
                android.system.Os.setenv("NFSU2_DATA_DIR", LauncherActivity.gameRoot().getAbsolutePath(), true);
            } catch (Throwable ignored) {}

            String horn = GameOptions.get(this, GameOptions.HORN);
            android.system.Os.setenv("NFSU2_HORN", horn, true);

            boolean hiddenRaces = GameOptions.isCheatEnabled(this, "hidden_races");
            android.system.Os.setenv("NFSU2_SHOW_HIDDEN_RACES", hiddenRaces ? "1" : "0", true);

            boolean dumbAI = GameOptions.isCheatEnabled(this, "dumb_ai");
            android.system.Os.setenv("NFSU2_DUMB_AI", dumbAI ? "1" : "0", true);

            boolean extraOptions = GameOptions.isExtraOptionsEnabled(this);
            android.system.Os.setenv("NFSU2_EXTRA_OPTIONS", extraOptions ? "1" : "0", true);

            android.system.Os.setenv("NFSU2_AUTOSAVE", "1", true);

            android.system.Os.setenv("NFSU2_INTRO", "original", true);
            android.system.Os.setenv("NFSU2_CUTSCENE_BORDERS", "full", true);

            String roamMaxCars = "4"; // default (hidden option)
            android.system.Os.setenv("NFSU2_ROAM_MAX_CARS", roamMaxCars, true);

            String crew = launch.getStringExtra("roamCrew");
            if (crew == null) crew = GameOptions.roamCrew(this);
            if (crew != null && !crew.trim().isEmpty()) {
                android.system.Os.setenv("NFSU2_ROAM_CREW", crew.trim(), true);
            }

            String backend = launch.getStringExtra("graphicsBackend");
            if (backend == null || "auto".equalsIgnoreCase(backend)) backend = GameOptions.getEffectiveGraphicsBackend(this);
            android.system.Os.setenv("NFSU2_RENDERER", backend, true);

            String drawDist = GameOptions.get(this, GameOptions.DRAW_DISTANCE);
            distVal = 100.0f;
            if ("ultra_performance".equals(drawDist)) distVal = 50.0f;
            else if ("performance".equals(drawDist)) distVal = 100.0f;
            else if ("balanced".equals(drawDist)) distVal = 250.0f;
            else if ("extended".equals(drawDist)) distVal = 450.0f;
            else if ("original".equals(drawDist)) distVal = 0.0f;
            android.system.Os.setenv("NFSU2_DRAW_DISTANCE", String.valueOf((int)distVal), true);

            android.system.Os.setenv("NFSU2_CROWD", "0", true);
            GraphicsCacheManager.setCrowdSuppression(true);
            android.system.Os.setenv("NFSU2_NEWS_URL", "https://api.mininxd.tech/nfsu2_news", true);

            // Sync touch control size
            String touchSize = GameOptions.get(this, GameOptions.TOUCH_SIZE);
            getSharedPreferences("touch_layouts_v2", MODE_PRIVATE)
                    .edit().putString("control_size", touchSize).apply();
        } catch (Throwable ignored) {}
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        // Launcher options travel as extras; a direct start (adb, recents) falls back to the saved ones.
        DisplayMetrics metrics = new DisplayMetrics(); getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int dispW = Math.max(metrics.widthPixels, metrics.heightPixels);
        int dispH = Math.min(metrics.widthPixels, metrics.heightPixels);
        int[] size = GameOptions.renderSize(this, dispW, dispH);
        String resPercent = launch.getStringExtra("resolutionPercent");
        if (resPercent == null) resPercent = GameOptions.get(this, GameOptions.RESOLUTION);
        try {
            android.system.Os.setenv("NFSU2_DISPLAY_WIDTH", String.valueOf(dispW), true);
            android.system.Os.setenv("NFSU2_DISPLAY_HEIGHT", String.valueOf(dispH), true);
            android.system.Os.setenv("NFSU2_RES_PERCENT", resPercent, true);
        } catch (Throwable ignored) {}
        nativeResolution(launch.getIntExtra("renderWidth", size[0]), launch.getIntExtra("renderHeight", size[1]));
        String displayLogs = launch.getStringExtra("displayLogs");
        if (displayLogs == null) displayLogs = GameOptions.get(this, GameOptions.DISPLAY_LOGS);
        GameOptions.applyWidescreenPatchFile(this);
        if (GameOptions.usesWidescreenPatch(this))
            GameOptions.syncWidescreenHudDat(this, !"off".equalsIgnoreCase(displayLogs));
        String language = launch.getStringExtra("language");
        if (language == null || "English".equalsIgnoreCase(language)) {
            language = GameOptions.language(this);
        }
        if ("English".equalsIgnoreCase(language)) {
            language = "English UK";
        }
        nativeLanguage(language);
        nativeFrameLimit(launch.getIntExtra("frameCap", Integer.parseInt(GameOptions.get(this, GameOptions.FPS))));
        int screenMode = launch.getIntExtra("widescreenMode", GameOptions.widescreenMode(this));
        nativeWidescreen(true);             // 3D world is ALWAYS widescreen fullscreen, never squished
        nativeScreenMode(screenMode);       // 0=Widescreen HUD edges, 2=4:3 HUD centre
        nativeDrawDistance(distVal);
        nativeForwardCulling(true);
        String server = launch.getStringExtra("onlineServer");
        String configuredServer = GameOptions.validServer(server) ? server : GameOptions.onlineServer(this);
        boolean useLocalBridge = io.mininxd.nfsu2.net.EahubClassicOnline.shouldUseLocalBridge(configuredServer);
        if (useLocalBridge) {
            // Modern nfsu2-web server: tunnel DirtySock through LocalOnlineBridge WebSocket
            nativeOnlineServer("127.0.0.1");
        } else {
            // Classic server (eahub.eu or custom): gethostbyname in guest_network.inc
            // redirects all *.ea.com DNS lookups to this host directly over plain TCP.
            String eaNationServer = GameOptions.isEahubClassic(configuredServer) ? "eahub.eu" : configuredServer;
            nativeOnlineServer(eaNationServer);
        }
        io.mininxd.nfsu2.net.EahubClassicOnline.logStartup(this, configuredServer, !useLocalBridge);
        minimapTop = launch.getBooleanExtra("minimapTop", "top".equalsIgnoreCase(GameOptions.get(this, GameOptions.MINIMAP)));
        try { android.system.Os.setenv("NFSU2_MINIMAP", minimapTop ? "top" : "bottom", true); } catch (Throwable ignored) {}
        speedometerPosition = launch.getIntExtra("speedometer", GameOptions.speedometerPosition(this));
        nativeMinimapTop(minimapTop);
        updateHudForGamepad(false);
        nativeGamepadLayout(launch.getBooleanExtra("consolePad", GameOptions.isConsolePad(this)));
        nativeBackBuffers(launch.getIntExtra("backBuffers", 2));
        nativeCpuTextures(launch.getBooleanExtra("cpuTextures", false));
        nativeRegion(launch.getIntExtra("region", GameOptions.regionCode(this)));
        String[] loader = launch.getStringArrayExtra("driverLoader");
        if (loader == null || loader.length != 4) loader = GpuDrivers.loaderArguments(this);
        nativeGpuDriver(loader[0], loader[1], loader[2], loader[3]);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(0xb0000000);
        status.setTextSize(18);
        status.setPadding(24, 16, 24, 16);
        status.setText("NFSU2 Revived\nChecking internal storage/nfsu2…");
        // NativeActivity gives its main window surface to Vulkan; use a separate
        // non-interactive popup surface for the diagnostic text.
        statusWindow = new PopupWindow(status, ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, false);
        statusWindow.setTouchable(false);
        touchControls = new RacingControlsView(this, this::setTouchKey, this::showTextInput,this::showTiltOptions,this::showFrameLimitOptions);
        touchControls.setPointer(GameActivity::nativePointer);
        touchControls.setPointerMiddle(GameActivity::nativePointerMiddle);
        touchControls.setOnEditingChangeListener(editing -> {
            if (diagnosticOverlay != null) {
                diagnosticOverlay.setSuspended(editing);
            }
        });
        controlsWindow = new PopupWindow(touchControls, ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT, false);
        controlsWindow.setClippingEnabled(false);
        controlsWindow.setFocusable(false);
        controlsWindow.setTouchable(true);
        controlsWindow.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        if (Build.VERSION.SDK_INT >= 22) {
            controlsWindow.setAttachedInDecor(true);
        }
        java.util.ArrayList<String> cheats = launch.getStringArrayListExtra("cheats");
        if (cheats == null) cheats = GameOptions.getActiveCheatCodes(this);
        if (!cheats.isEmpty()) applyCheatCodes(cheats);

        boolean isWs = useLocalBridge;  // only WS-based servers need Adhoc via LocalOnlineBridge
        boolean roamEnabled = launch.getBooleanExtra("roamEnabled", GameOptions.isRoamEnabled(this));
        if (roamEnabled || isWs) {
            String roamServer = launch.getStringExtra("roamServer");
            if (roamServer == null || roamServer.isEmpty()) roamServer = GameOptions.roamServer(this);
            else if (GameOptions.isEahubClassic(roamServer)) roamServer = GameOptions.DEFAULT_ROAM_SERVER;
            String roamRoom = launch.hasExtra("roamRoom") ? launch.getStringExtra("roamRoom") : GameOptions.roamRoom(this);
            if (roamRoom == null) roamRoom = "";
            String roamName = launch.getStringExtra("roamName");
            if (roamName == null || roamName.isEmpty()) roamName = GameOptions.roamName(this);
            boolean ghostMode = launch.getBooleanExtra("roamGhost", GameOptions.isGhostModeEnabled(this));
            AdhocRoamClient.getInstance().start(this, roamServer, roamRoom, roamName, ghostMode);
        }

        diagnosticOverlay = new DiagnosticOverlay(this, displayLogs);
        if (diagnosticOverlay.isEnabled()) {
            diagnosticOverlay.start();
        }
        startCompanionTicker();
    }

    private Thread mCompanionTickerThread;
    private final java.util.concurrent.atomic.AtomicBoolean mCompanionRunning = new java.util.concurrent.atomic.AtomicBoolean(false);

    private void startCompanionTicker() {
        // Native roam companion is ticked synchronously per-frame in guest_runtime.cpp at Direct3D9 Present.
    }

    private void stopCompanionTicker() {
    }

    private void applyCheatCodes(java.util.List<String> codes) {
        if (codes == null || codes.isEmpty()) return;
        android.os.Handler handler = new android.os.Handler(android.os.Looper.getMainLooper());
        long[] waves = new long[] { 5000, 7500, 10000, 13000 };
        for (long waveDelay : waves) {
            handler.postDelayed(() -> {
                if (!foreground) return;
                long offset = 0;
                for (String code : codes) {
                    handler.postDelayed(() -> {
                        if (foreground) {
                            nativeTypeText(code, false);
                            android.util.Log.i("NFSU2", "Delivered cheat code: " + code);
                        }
                    }, offset);
                    offset += 250;
                }
            }, waveDelay);
        }
    }
    public void setStatus(String message) {
        if (message != null) {
            String backend = GameOptions.getEffectiveGraphicsBackend(this);
            if ("opengl".equals(backend)) {
                message = message.replace("Vulkan 1.1: presentación activa", "OpenGL ES: presentation active")
                                 .replace("Vulkan 1.1: presentation active", "OpenGL ES: presentation active");
            } else {
                message = message.replace("Vulkan 1.1: presentación activa", "Vulkan 1.1: presentation active");
            }
            message = message
                    .replace("Datos accesibles: memoria interna/nfsu2", "Accessible data: internal storage/nfsu2")
                    .replace("Ejecutando entrada CRT del juego…", "Executing game CRT entry…")
                    .replace("Ejecutando entrada CRT del juego", "Executing game CRT entry…")
                    .replace("Arranque guest detenido:", "Guest boot halted:")
                    .replace("Entrada guest terminada:", "Guest entry finished:")
                    .replace("La GPU no pudo crear el dispositivo grafico", "The GPU could not create the graphics device")
                    .replace("Usa \"Probar driver\" en el launcher para ver que le falta.", "Use \"Check driver\" in the launcher to see what is missing.")
                    .replace("Usa \"Probar driver\"", "Use \"Check driver\"")
                    .replace("para ver que le falta", "to see what is missing");
        }
        final String text = message;
        if (text != null) {
            GameLog.i("Status", text);
            if (text.contains("halted") || text.contains("detenido") || text.contains("Error")
                    || text.contains("failed") || text.contains("terminada") || text.contains("finished")
                    || text.contains("stall") || text.contains("freeze")) {
                GameLog.logHalt(this, text);
            }
        }
        runOnUiThread(() -> {
            if (statusHideTask != null) status.removeCallbacks(statusHideTask);
            if (text != null && (text.contains("Executing game CRT entry") || text.contains("Ejecutando entrada CRT"))) {
                status.setText("NFSU2 Revived\n" + text);
                statusHideTask = () -> {statusHidden=true;if(statusWindow.isShowing())statusWindow.dismiss();};
                status.postDelayed(statusHideTask, 3000);
            } else {
                statusHidden=false;
                status.setText("NFSU2 Revived\n" + text);
                if (!statusWindow.isShowing() && hasWindowFocus())
                    statusWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
            }
        });
    }
    private boolean wasBackgrounded = false;

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
                if (mode != null && mode.getPhysicalWidth() == current.getPhysicalWidth() && mode.getPhysicalHeight() == current.getPhysicalHeight()) {
                    refresh = Math.max(refresh, mode.getRefreshRate());
                    if (bestMode == null || mode.getRefreshRate() > bestMode.getRefreshRate()) {
                        bestMode = mode;
                    }
                }
            }
            android.view.WindowManager.LayoutParams windowParams = getWindow().getAttributes();
            windowParams.preferredRefreshRate = Math.min(120f, refresh);
            if (Build.VERSION.SDK_INT >= 23 && bestMode != null) {
                windowParams.preferredDisplayModeId = bestMode.getModeId();
            }
            try {
                java.lang.reflect.Field minField = windowParams.getClass().getField("preferredMinDisplayRefreshRate");
                minField.setFloat(windowParams, Math.min(60.0f, refresh));
                java.lang.reflect.Field maxField = windowParams.getClass().getField("preferredMaxDisplayRefreshRate");
                maxField.setFloat(windowParams, Math.min(120.0f, refresh));
            } catch (Throwable ignored) {}
            getWindow().setAttributes(windowParams);
        } catch (Throwable ignored) {}
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if(!focused) {
            if(touchControls!=null)touchControls.releaseAll();
            setTiltKeys(false,false);
            if(diagnosticOverlay!=null)diagnosticOverlay.hide();
        }
        if(focused){
            applyDisplayRefreshRate();
            if(Build.VERSION.SDK_INT>=30){
                getWindow().setDecorFitsSystemWindows(false);
                android.view.WindowInsetsController insets=getWindow().getInsetsController();
                if(insets!=null){
                    insets.hide(android.view.WindowInsets.Type.systemBars());
                    insets.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            }else getWindow().getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_FULLSCREEN |
                    android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                    android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            if (wasBackgrounded) {
                showRecoveryDialog();
                return;
            }
        }
        if (focused && foreground && statusWindow != null && !statusHidden && !statusWindow.isShowing()) {
            statusWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
        }
        if (focused && foreground && controlsWindow != null && !controlsWindow.isShowing()) {
            controlsWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
        }
        if (focused && foreground && diagnosticOverlay != null) {
            diagnosticOverlay.show();
        }
    }
    // LAN games announce and discover themselves with UDP broadcasts on port 9999;
    // without this lock many devices drop incoming Wi-Fi broadcasts to save power.
    private android.net.wifi.WifiManager.MulticastLock lanBroadcasts;
    private void holdLanBroadcasts(boolean hold) {
        if (lanBroadcasts == null) {
            android.net.wifi.WifiManager wifi = (android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wifi == null) return;
            lanBroadcasts = wifi.createMulticastLock("nfsu2-lan");
            lanBroadcasts.setReferenceCounted(false);
        }
        if (hold) lanBroadcasts.acquire(); else lanBroadcasts.release();
    }
    @Override protected void onPause() {
        foreground=false;
        stopCompanionTicker();
        holdLanBroadcasts(false);
        updateTiltSensor();
        if(inputManager!=null)inputManager.unregisterInputDeviceListener(gamepadListener);
        if (statusWindow != null) statusWindow.dismiss();
        if (touchControls != null) {touchControls.removeCallbacks(updateControlMode);touchControls.releaseAll();}
        if (controlsWindow != null) controlsWindow.dismiss();
        super.onPause();
    }
    @Override protected void onResume() {
        super.onResume();
        foreground=true;
        requestGameAudioFocus();
        startCompanionTicker();
        holdLanBroadcasts(true);
        updateTiltSensor();
        if(inputManager!=null){inputManager.registerInputDeviceListener(gamepadListener,null);updateGamepads();}
        if(touchControls!=null) {touchControls.removeCallbacks(updateControlMode);touchControls.post(updateControlMode);}
        boolean allowed = Build.VERSION.SDK_INT >= 30
                ? Environment.isExternalStorageManager()
                : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        if (!allowed && !requested) {
            requested = true;
            Toast.makeText(this, "Allow file access to read the nfsu2 folder from internal storage", Toast.LENGTH_LONG).show();
            if (Build.VERSION.SDK_INT >= 30) {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } else {
                requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
            }
        }
    }
    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        // NativeActivity may stay resumed when a runtime permission dialog closes.
        if (code == 1 && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) recreate();
    }
    @Override public void onBackPressed() {
        // Prevent accidental back gesture from closing the game while steering
    }
    @Override protected void onStop() {
        super.onStop();
        wasBackgrounded = true;
    }

    private void showRecoveryDialog() {
        if (isFinishing() || (Build.VERSION.SDK_INT >= 17 && isDestroyed())) return;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Session Interrupted")
                .setMessage("Android OS paused the graphics pipeline while the game was in the background.\n\nTap Quick Restart to reload cleanly in ~1 second.")
                .setPositiveButton("Quick Restart", (dialog, which) -> quickRestart())
                .setNeutralButton("Try Resume", (dialog, which) -> {
                    wasBackgrounded = false;
                    dialog.dismiss();
                    if (controlsWindow != null && !controlsWindow.isShowing() && hasWindowFocus()) {
                        try {
                            controlsWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
                        } catch (Exception ignored) {}
                    }
                    if (diagnosticOverlay != null) {
                        try { diagnosticOverlay.show(); } catch (Exception ignored) {}
                    }
                    final long startFrames = io.mininxd.nfsu2.net.RoamNativeBridge.isLoaded()
                            ? io.mininxd.nfsu2.net.RoamNativeBridge.nativeGetFrameCount() : -1;
                    getWindow().getDecorView().postDelayed(() -> {
                        if (isFinishing() || (Build.VERSION.SDK_INT >= 17 && isDestroyed())) return;
                        if (io.mininxd.nfsu2.net.RoamNativeBridge.isLoaded() && startFrames >= 0) {
                            long currentFrames = io.mininxd.nfsu2.net.RoamNativeBridge.nativeGetFrameCount();
                            if (currentFrames <= startFrames) {
                                android.util.Log.w("NFSU2", "Game render loop stalled after resume (frames=" + currentFrames + "); triggering quickRestart to recover cleanly");
                                quickRestart();
                            }
                        }
                    }, 850);
                })
                .setNegativeButton("Exit to Launcher", (dialog, which) -> finish())
                .setCancelable(false)
                .show();
    }

    private void quickRestart() {
        wasBackgrounded = false;
        Intent intent = getIntent();
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    public static String fetchNewsContent() {
        if (s_cachedNews != null && !s_cachedNews.trim().isEmpty()) {
            refreshNewsAsync();
            return s_cachedNews;
        }

        String news = fetchNewsNetwork();
        if (news != null && !news.trim().isEmpty()) {
            s_cachedNews = news;
            return news;
        }

        // Offline fallback: read bundled assets/news.txt inside the APK
        try {
            if (s_instance != null) {
                java.io.InputStream in = s_instance.getAssets().open("news.txt");
                java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
                byte[] buf = new byte[1024];
                int n;
                while ((n = in.read(buf)) != -1) {
                    baos.write(buf, 0, n);
                }
                in.close();
                String res = baos.toString("UTF-8").trim();
                if (!res.isEmpty()) {
                    return res;
                }
            }
        } catch (Throwable ignored) {}

        return DEFAULT_NEWS_CONTENT;
    }

    private static String fetchNewsNetwork() {
        String customUrl = "https://api.mininxd.tech/nfsu2_news";
        try {
            String envUrl = System.getenv("NFSU2_NEWS_URL");
            if (envUrl != null && !envUrl.trim().isEmpty()) {
                customUrl = envUrl.trim();
            }
            java.net.URL url = new java.net.URL(customUrl);
            java.net.HttpURLConnection conn = (java.net.HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(5000);
            conn.setRequestProperty("User-Agent", "NFSU2-Revived-Android/1.0");
            conn.setRequestProperty("Accept", "text/plain, */*");
            int code = conn.getResponseCode();
            if (code == 200) {
                java.io.InputStream in = conn.getInputStream();
                java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
                byte[] buf = new byte[1024];
                int n;
                while ((n = in.read(buf)) != -1) {
                    baos.write(buf, 0, n);
                }
                in.close();
                String res = baos.toString("UTF-8").trim();
                if (!res.isEmpty()) {
                    if (!res.contains("CMD=news")) {
                        res = " %{START CMD=news TITLE=\"NFSU2 News\" BTN1=\"OK\" BTN1-GOTO=\"$exit=0\" %}\r\n" + res + "\r\n%{%}";
                    }
                    Log.i("NFSU2News", "Successfully fetched custom news from " + customUrl + " (" + res.length() + " chars)");
                    return res;
                }
            }
            Log.w("NFSU2News", "Custom news URL returned code " + code + ", falling back to bundled news.txt");
        } catch (Throwable t) {
            Log.w("NFSU2News", "Failed fetching custom news (" + t.getMessage() + "), falling back to bundled news.txt");
        }
        return null;
    }

    private static void refreshNewsAsync() {
        new Thread(() -> {
            try {
                String fresh = fetchNewsNetwork();
                if (fresh != null && !fresh.trim().isEmpty()) {
                    s_cachedNews = fresh;
                }
            } catch (Throwable ignored) {}
        }, "NewsRefresher").start();
    }

    private void processGamepadRightStick(android.view.MotionEvent event) {
        if ((event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) != android.view.InputDevice.SOURCE_JOYSTICK) {
            return;
        }
        float rx_rot = event.getAxisValue(android.view.MotionEvent.AXIS_RX);
        float ry_rot = event.getAxisValue(android.view.MotionEvent.AXIS_RY);

        float rx_z = event.getAxisValue(android.view.MotionEvent.AXIS_Z);
        float ry_z = event.getAxisValue(android.view.MotionEvent.AXIS_RZ);

        // Clamp resting trigger values (-1.0f on resting analog triggers)
        if (rx_z < -0.85f) rx_z = 0.0f;
        if (ry_z < -0.85f) ry_z = 0.0f;
        if (rx_rot < -0.85f) rx_rot = 0.0f;
        if (ry_rot < -0.85f) ry_rot = 0.0f;

        float rx, ry;
        if (Math.abs(rx_rot) > 0.10f || Math.abs(ry_rot) > 0.10f) {
            rx = rx_rot;
            ry = ry_rot;
        } else {
            rx = rx_z;
            ry = ry_z;
        }

        if (Math.abs(rx) > 0.15f || Math.abs(ry) > 0.15f) {
            AdhocRoamClient.handleRightStick(rx, ry);
        } else {
            AdhocRoamClient.handleRightStick(0f, 0f);
        }
    }

    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        processGamepadRightStick(event);
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public boolean onGenericMotionEvent(android.view.MotionEvent event) {
        processGamepadRightStick(event);
        return super.onGenericMotionEvent(event);
    }

    @Override protected void onDestroy() {
        if (s_instance == this) {
            s_instance = null;
        }
        super.onDestroy();
        stopCompanionTicker();
        if (diagnosticOverlay != null) diagnosticOverlay.stop();
        AdhocRoamClient.getInstance().stop();
        // Each launch uses a fresh guest machine in the dedicated :game process.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
