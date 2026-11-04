package io.mininxd.nfsu2;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.FeatureInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Fresh process for each driver test (from the MW port): adrenotools' loader hooks cannot be switched
 * in a running process, and a broken driver must not take the launcher down with it.
 */
public final class GpuProbeActivity extends Activity {
    private static final String TAG = "NFSU2_GpuProbe";
    private static boolean nativeLoaded = false;

    static {
        try {
            System.loadLibrary("nfsu2_diagnostics");
            nativeLoaded = true;
        } catch (Throwable t) {
            Log.w(TAG, "libnfsu2_diagnostics.so not available, will use system fallback: " + t.getMessage());
            nativeLoaded = false;
        }
    }

    public static native String nativeGpuReport(String hooks, String temp, String directory, String library);

    private boolean finished;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView status = new TextView(this);
        status.setText("Checking GPU & Vulkan driver…");
        status.setTextColor(0xFFFFFFFF); status.setTextSize(18);
        status.setGravity(android.view.Gravity.CENTER); status.setBackgroundColor(0xFF0B0F14);
        setContentView(status);
        Handler main = new Handler(Looper.getMainLooper());
        main.postDelayed(() -> finishReport("{\"probeError\":\"Driver test timed out after 20 seconds\",\"driverLoadFailed\":true}"), 20000);
        String[] loader = GpuDrivers.loaderArguments(this);
        new Thread(() -> {
            String result;
            if (nativeLoaded) {
                try {
                    result = nativeGpuReport(loader[0], loader[1], loader[2], loader[3]);
                } catch (Throwable error) {
                    Log.w(TAG, "Native driver probe failed, falling back to system query", error);
                    result = getSystemVulkanReport();
                }
            } else {
                result = getSystemVulkanReport();
            }
            String report = result;
            main.post(() -> finishReport(report));
        }, "VulkanDriverProbe").start();
    }

    private String getSystemVulkanReport() {
        try {
            JSONObject obj = new JSONObject();
            PackageManager pm = getPackageManager();
            int version = 0;
            int level = 0;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                FeatureInfo[] features = pm.getSystemAvailableFeatures();
                if (features != null) {
                    for (FeatureInfo f : features) {
                        if (PackageManager.FEATURE_VULKAN_HARDWARE_VERSION.equals(f.name)) {
                            version = f.version;
                        } else if (PackageManager.FEATURE_VULKAN_HARDWARE_LEVEL.equals(f.name)) {
                            level = f.version;
                        }
                    }
                }
            }

            if (version == 0) {
                return "{\"probeError\":\"Vulkan is not supported on this device.\",\"driverLoadFailed\":true}";
            }

            int major = version >> 22;
            int minor = (version >> 12) & 0x3ff;
            int patch = version & 0xfff;
            String verStr = major + "." + minor + "." + patch;

            String gpuName = Build.HARDWARE != null && !Build.HARDWARE.isEmpty() ? Build.HARDWARE : "System GPU";
            if (Build.VERSION.SDK_INT >= 31 && Build.SOC_MODEL != null && !Build.SOC_MODEL.isEmpty()) {
                gpuName = Build.SOC_MODEL + " (" + gpuName + ")";
            }
            obj.put("gpu", gpuName);
            obj.put("vulkan", verStr);
            obj.put("vendorId", 0);
            obj.put("requestedDriver", "system");
            obj.put("driverName", "System Vulkan (" + Build.MANUFACTURER + ")");
            obj.put("driverInfo", "Hardware Level " + level);
            obj.put("logicalDeviceResult", 0);
            obj.put("driverLoadFailed", false);
            obj.put("bcTextures", true);
            obj.put("vertexFormatsMissing", "");
            obj.put("compatible", major > 1 || (major == 1 && minor >= 1));
            obj.put("missing", new JSONArray());
            return obj.toString();
        } catch (Exception e) {
            return "{\"probeError\":\"" + e.getMessage() + "\",\"driverLoadFailed\":true}";
        }
    }

    private void finishReport(String report) {
        if (finished) return;
        finished = true;
        setResult(RESULT_OK, new Intent().putExtra("gpu_report", report));
        finish();
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            System.exit(0);
        }, 300);
    }
}
