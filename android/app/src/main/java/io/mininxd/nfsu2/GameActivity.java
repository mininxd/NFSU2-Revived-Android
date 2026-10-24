package io.mininxd.nfsu2;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Environment;
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

/** Storage permission gateway for the fixed Internal storage/nfsu2 directory. */
public final class GameActivity extends NativeActivity {
    static { System.loadLibrary("nfsu2_android"); }
    private boolean requested;
    private boolean foreground;
    private TextView status;
    private PopupWindow statusWindow;
    private PopupWindow controlsWindow;
    private RacingControlsView touchControls;
    private boolean statusHidden;
    private Runnable statusHideTask;
    private static void nativeKey(int scan, boolean down) { com.nfsu2.androidevolved.GameActivity.nativeKey(scan, down); }
    private static boolean nativeDrivingControls() { return com.nfsu2.androidevolved.GameActivity.nativeDrivingControls(); }
    private static boolean nativeGamepadUsed() { return com.nfsu2.androidevolved.GameActivity.nativeGamepadUsed(); }
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
    private void sendCombinedKey(int scan) {
        boolean held=touchKeys[scan]||(scan==0xcb&&tiltLeft)||(scan==0xcd&&tiltRight);
        if(sentKeys[scan]!=held) {sentKeys[scan]=held;nativeKey(scan,held);}
    }
    private void setTouchKey(int scan,boolean down) {touchKeys[scan]=down;sendCombinedKey(scan);}
    private void setTiltKeys(boolean left,boolean right) {tiltLeft=left;tiltRight=right;sendCombinedKey(0xcb);sendCombinedKey(0xcd);}
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
            boolean pad=device.isExternal()&&(buttons||stick||keys);
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
            touchControls.postDelayed(this,250);
        }
    };
    private static void nativeTypeText(String value, boolean replace) { com.nfsu2.androidevolved.GameActivity.nativeTypeText(value, replace); }
    private static void nativePointer(float x, float y, int phase) { com.nfsu2.androidevolved.GameActivity.nativePointer(x, y, phase); }
    private static void nativeLanguage(String language) { com.nfsu2.androidevolved.GameActivity.nativeLanguage(language); }
    private static void nativeResolution(int width, int height) { com.nfsu2.androidevolved.GameActivity.nativeResolution(width, height); }
    private static void nativeFrameLimit(int framesPerSecond) { com.nfsu2.androidevolved.GameActivity.nativeFrameLimit(framesPerSecond); }
    private static void nativeWidescreen(boolean enabled) { com.nfsu2.androidevolved.GameActivity.nativeWidescreen(enabled); }
    private static void nativeOnlineServer(String host) { com.nfsu2.androidevolved.GameActivity.nativeOnlineServer(host); }
    private static void nativeGamepadConnected(boolean connected) { com.nfsu2.androidevolved.GameActivity.nativeGamepadConnected(connected); }
    private static void nativeGamepadDevices(int[] ids) { com.nfsu2.androidevolved.GameActivity.nativeGamepadDevices(ids); }
    private static void nativeMinimapTop(boolean top) { com.nfsu2.androidevolved.GameActivity.nativeMinimapTop(top); }
    private static void nativeBackBuffers(int count) { com.nfsu2.androidevolved.GameActivity.nativeBackBuffers(count); }
    private static void nativeCpuTextures(boolean enabled) { com.nfsu2.androidevolved.GameActivity.nativeCpuTextures(enabled); }
    private static void nativeGpuDriver(String hooks, String temp, String directory, String library) { com.nfsu2.androidevolved.GameActivity.nativeGpuDriver(hooks, temp, directory, library); }
    private void showFrameLimitOptions() {
        GameOptions.Option option=GameOptions.find(GameOptions.FPS);
        String saved=GameOptions.get(this,GameOptions.FPS);int selected=0;
        for(int i=0;i<option.values.length;i++)if(option.values[i].equals(saved))selected=i;
        new AlertDialog.Builder(this).setTitle(option.title)
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
        inputManager=(android.hardware.input.InputManager)getSystemService(INPUT_SERVICE);
        tiltSensors=(android.hardware.SensorManager)getSystemService(SENSOR_SERVICE);
        tiltSensor=tiltSensors==null?null:tiltSensors.getDefaultSensor(android.hardware.Sensor.TYPE_ACCELEROMETER);
        android.content.SharedPreferences options=getSharedPreferences("touch_layouts_v1",MODE_PRIVATE);
        tiltEnabled=options.getBoolean("tilt.enabled",false);tiltInvert=options.getBoolean("tilt.invert",false);
        tiltSensitivity=options.getFloat("tilt.sensitivity",1);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        android.view.Display display=getWindowManager().getDefaultDisplay();
        android.view.Display.Mode current=display.getMode();float refresh=60;
        for(android.view.Display.Mode mode:display.getSupportedModes())
            if(mode.getPhysicalWidth()==current.getPhysicalWidth()&&mode.getPhysicalHeight()==current.getPhysicalHeight())
                refresh=Math.max(refresh,mode.getRefreshRate());
        android.view.WindowManager.LayoutParams windowParams=getWindow().getAttributes();
        windowParams.preferredRefreshRate=Math.min(120,refresh);
        Intent launch = getIntent();
        String cutout = launch.getStringExtra("cutout");
        if (cutout == null) cutout = GameOptions.get(this, GameOptions.CUTOUT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            if ("safe".equals(cutout)) {
                windowParams.layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
            } else {
                windowParams.layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }
        }
        getWindow().setAttributes(windowParams);
        // Launcher options travel as extras; a direct start (adb, recents) falls back to the saved ones.
        DisplayMetrics metrics = new DisplayMetrics(); getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int[] size = GameOptions.renderSize(this, Math.max(metrics.widthPixels, metrics.heightPixels),
                Math.min(metrics.widthPixels, metrics.heightPixels));
        nativeResolution(launch.getIntExtra("renderWidth", size[0]), launch.getIntExtra("renderHeight", size[1]));
        String language = launch.getStringExtra("language");
        if (language == null || "English".equalsIgnoreCase(language)) {
            language = GameOptions.language(this);
        }
        if ("English".equalsIgnoreCase(language)) {
            language = "English UK";
        }
        nativeLanguage(language);
        nativeFrameLimit(launch.getIntExtra("frameCap", Integer.parseInt(GameOptions.get(this, GameOptions.FPS))));
        nativeWidescreen(launch.getBooleanExtra("widescreen", GameOptions.get(this, GameOptions.WIDESCREEN).equals("on")));
        String server = launch.getStringExtra("onlineServer");
        nativeOnlineServer(GameOptions.validServer(server) ? server : GameOptions.onlineServer(this));
        nativeMinimapTop(launch.getBooleanExtra("minimapTop", GameOptions.get(this, GameOptions.MINIMAP).equals("top")));
        nativeBackBuffers(launch.getIntExtra("backBuffers", Integer.parseInt(GameOptions.get(this, GameOptions.LATENCY))));
        nativeCpuTextures(launch.getBooleanExtra("cpuTextures", GameOptions.get(this, GameOptions.TEXTURES).equals("cpu")));
        String[] loader = launch.getStringArrayExtra("driverLoader");
        if (loader == null || loader.length != 4) loader = GpuDrivers.loaderArguments(this);
        nativeGpuDriver(loader[0], loader[1], loader[2], loader[3]);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(0xb0000000);
        status.setTextSize(18);
        status.setPadding(24, 16, 24, 16);
        status.setText("NFSU2\nChecking internal storage/nfsu2…");
        // NativeActivity gives its main window surface to Vulkan; use a separate
        // non-interactive popup surface for the diagnostic text.
        statusWindow = new PopupWindow(status, ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, false);
        statusWindow.setTouchable(false);
        touchControls = new RacingControlsView(this, this::setTouchKey, this::showTextInput,this::showTiltOptions,this::showFrameLimitOptions);
        touchControls.setPointer(GameActivity::nativePointer);
        controlsWindow = new PopupWindow(touchControls, ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT, false);
        controlsWindow.setClippingEnabled(false);
    }
    public void setStatus(String message) {
        if (message != null) {
            message = message
                    .replace("Datos accesibles: memoria interna/nfsu2", "Accessible data: internal storage/nfsu2")
                    .replace("Vulkan 1.1: presentación activa", "Vulkan 1.1: presentation active")
                    .replace("Ejecutando entrada CRT del juego…", "Executing game CRT entry…")
                    .replace("Ejecutando entrada CRT del juego", "Executing game CRT entry…")
                    .replace("Arranque guest detenido:", "Guest boot halted:")
                    .replace("Entrada guest terminada:", "Guest entry finished:");
        }
        final String text = message;
        runOnUiThread(() -> {
            if (statusHideTask != null) status.removeCallbacks(statusHideTask);
            if (text != null && (text.contains("Executing game CRT entry") || text.contains("Ejecutando entrada CRT"))) {
                status.setText("NFSU2\n" + text);
                statusHideTask = () -> {statusHidden=true;if(statusWindow.isShowing())statusWindow.dismiss();};
                status.postDelayed(statusHideTask, 3000);
            } else {
                statusHidden=false;
                status.setText("NFSU2\n" + text);
                if (!statusWindow.isShowing() && hasWindowFocus())
                    statusWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
            }
        });
    }
    private boolean wasBackgrounded = false;

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if(!focused) {
            if(touchControls!=null)touchControls.releaseAll();
            setTiltKeys(false,false);
            if(controlsWindow!=null)controlsWindow.dismiss();
        }
        if(focused){
            if(Build.VERSION.SDK_INT>=30){
                android.view.WindowInsetsController insets=getWindow().getInsetsController();
                if(insets!=null){insets.hide(android.view.WindowInsets.Type.systemBars());
                    insets.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);}
            }else getWindow().getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_FULLSCREEN |
                    android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                    android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            if (wasBackgrounded) {
                showRecoveryDialog();
            }
        }
        if (focused && foreground && statusWindow != null && !statusHidden && !statusWindow.isShowing()) {
            statusWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
        }
        if (focused && foreground && controlsWindow != null && !controlsWindow.isShowing()) {
            controlsWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
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
    private float simulatedPointerX = 0.5f;
    private boolean rightStickDragging = false;

    @Override public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if ((event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            // Right analog stick rotates preview car when inside a menu/garage/lobby
            if (!nativeDrivingControls()) {
                float rx = event.getAxisValue(android.view.MotionEvent.AXIS_Z);
                if (Math.abs(rx) < 0.05f) {
                    rx = event.getAxisValue(android.view.MotionEvent.AXIS_RX);
                }
                if (Math.abs(rx) > 0.15f) {
                    if (!rightStickDragging) {
                        simulatedPointerX = 0.5f;
                        nativePointer(simulatedPointerX, 0.5f, 0); // Pointer DOWN
                        rightStickDragging = true;
                    }
                    simulatedPointerX += rx * 0.035f;
                    if (simulatedPointerX < 0.05f) simulatedPointerX = 0.95f;
                    else if (simulatedPointerX > 0.95f) simulatedPointerX = 0.05f;
                    nativePointer(simulatedPointerX, 0.5f, 1); // Pointer MOVE
                } else if (rightStickDragging) {
                    nativePointer(simulatedPointerX, 0.5f, 2); // Pointer UP
                    rightStickDragging = false;
                    simulatedPointerX = 0.5f;
                }
            }
        }
        return super.dispatchGenericMotionEvent(event);
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

    @Override protected void onDestroy() {
        super.onDestroy();
        // Each launch uses a fresh guest machine in the dedicated :game process.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
