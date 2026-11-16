package io.mininxd.nfsu2;

import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.util.Log;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/**
 * Universal GPU Driver Manager supporting any mobile GPU architecture:
 * ARM Mali (PanVK kbase/Bifrost/Valhall), Qualcomm Adreno (Turnip/Freedreno),
 * Mesa Vulkan, PowerVR, and Immortalis.
 * Supports auto-discovery from /sdcard/Download, /sdcard/cache, and SAF import.
 */
final class GpuDrivers {
    private static final String TAG = "NFSU2-GpuDrivers";

    static final class Driver {
        final String id, name, library;
        final File directory;
        Driver(String id, String name, File directory, String library) {
            this.id = id; this.name = name; this.directory = directory; this.library = library;
        }
    }

    private static File root(Context context) {
        File root = new File(context.getFilesDir(), "gpu_drivers");
        root.mkdirs();
        return root;
    }

    static List<Driver> list(Context context) {
        // Auto-discover any new driver ZIPs dropped in storage before listing
        autoDiscoverStorageDrivers(context);

        List<Driver> result = new ArrayList<>();
        File[] dirs = root(context).listFiles(File::isDirectory);
        if (dirs != null) for (File dir : dirs) {
            try {
                if (dir.getName().startsWith(".")) continue;
                File recordFile = new File(dir, "driver.json");
                if (!recordFile.isFile()) continue;
                JSONObject meta = new JSONObject(new String(Files.readAllBytes(recordFile.toPath()), StandardCharsets.UTF_8));
                String library = meta.getString("libraryName");
                if (!library.matches("[A-Za-z0-9_.+-]+\\.so")) continue;
                File loadDir = new File(dir, meta.getString("folder"));
                if (!loadDir.getCanonicalPath().startsWith(dir.getCanonicalPath() + "/")) continue;
                if (new File(loadDir, library).isFile()) {
                    result.add(new Driver(dir.getName(), meta.getString("name"), loadDir, library));
                }
            } catch (Exception ignored) { }
        }
        result.sort(Comparator.comparing(d -> d.name.toLowerCase(Locale.ROOT)));
        return result;
    }

    static String selectedId(Context context) {
        return GameOptions.prefs(context).getString("gpu_driver", "");
    }

    static Driver selected(Context context) {
        String id = selectedId(context);
        List<Driver> all = list(context);
        if (!id.isEmpty()) {
            for (Driver driver : all) if (driver.id.equals(id)) return driver;
        }

        // Auto-select PanVK for Mali or Turnip for Adreno if not explicitly chosen
        if (id.isEmpty() && !all.isEmpty()) {
            for (Driver driver : all) {
                if (driver.name.toLowerCase(Locale.ROOT).contains("panvk") ||
                    driver.library.toLowerCase(Locale.ROOT).contains("panfrost")) {
                    select(context, driver.id);
                    Log.i(TAG, "Auto-selected PanVK Vulkan driver for Mali GPU: " + driver.name);
                    return driver;
                }
            }
        }
        return null;
    }

    static void select(Context context, String id) {
        GameOptions.prefs(context).edit().putString("gpu_driver", id).apply();
    }

    /**
     * Auto-discovers driver packages in standard user directories:
     * /sdcard/Download, /sdcard/cache, /sdcard/nfsu2/drivers.
     */
    public static void autoDiscoverStorageDrivers(Context context) {
        try {
            File ext = Environment.getExternalStorageDirectory();
            if (ext == null || !ext.exists()) return;

            List<File> scanDirs = Arrays.asList(
                new File(ext, "Download"),
                new File(ext, "cache"),
                new File(ext, "nfsu2/drivers"),
                new File(ext, "nfsu2")
            );

            Set<String> installedNames = new HashSet<>();
            File[] existing = root(context).listFiles(File::isDirectory);
            if (existing != null) {
                for (File d : existing) {
                    File tagFile = new File(d, "source_archive.txt");
                    if (tagFile.isFile()) {
                        try {
                            installedNames.add(new String(Files.readAllBytes(tagFile.toPath()), StandardCharsets.UTF_8).trim());
                        } catch (Exception ignored) {}
                    }
                }
            }

            for (File scanDir : scanDirs) {
                if (!scanDir.isDirectory()) continue;
                File[] zips = scanDir.listFiles((dir, name) -> name.toLowerCase(Locale.ROOT).endsWith(".zip"));
                if (zips == null) continue;

                for (File zipFile : zips) {
                    String baseName = zipFile.getName();
                    if (installedNames.contains(baseName)) continue;

                    // Inspect if it is likely a Vulkan driver zip
                    String lower = baseName.toLowerCase(Locale.ROOT);
                    if (lower.contains("panvk") || lower.contains("turnip") || lower.contains("mali") ||
                        lower.contains("vulkan") || lower.contains("mesa") || lower.contains("adreno")) {
                        try {
                            Driver d = importFile(context, zipFile);
                            if (d != null) {
                                installedNames.add(baseName);
                                File rec = new File(root(context), d.id + "/source_archive.txt");
                                Files.write(rec.toPath(), baseName.getBytes(StandardCharsets.UTF_8));
                                Log.i(TAG, "Auto-imported custom GPU driver from " + zipFile.getAbsolutePath() + ": " + d.name);
                            }
                        } catch (Exception e) {
                            Log.w(TAG, "Failed auto-importing driver from " + zipFile.getName() + ": " + e.getMessage());
                        }
                    }
                }
            }
        } catch (Throwable t) {
            Log.d(TAG, "Auto-discovery error: " + t.getMessage());
        }
    }

    public static Driver importFile(Context context, File zipFile) throws Exception {
        try (InputStream in = new FileInputStream(zipFile)) {
            return importStream(context, in);
        }
    }

    static Driver importZip(Context context, Uri uri) throws Exception {
        InputStream input = context.getContentResolver().openInputStream(uri);
        if (input == null) throw new IOException("Could not open driver ZIP URI.");
        try (InputStream in = input) {
            return importStream(context, in);
        }
    }

    private static Driver importStream(Context context, InputStream input) throws Exception {
        File stage = new File(root(context), ".import-" + UUID.randomUUID());
        if (!stage.mkdirs()) throw new IOException("Could not create driver directory.");
        try {
            DriverArchive.extract(input, stage);
            File packageRoot = findMeta(stage);

            String library = null;
            String name = null;
            String driverVersion = "Vulkan 1.3";

            if (packageRoot != null && new File(packageRoot, "meta.json").isFile()) {
                JSONObject meta = new JSONObject(new String(Files.readAllBytes(new File(packageRoot, "meta.json").toPath()), StandardCharsets.UTF_8));
                library = meta.getString("libraryName");
                if (meta.optInt("minApi", 26) > Build.VERSION.SDK_INT) {
                    throw new IOException("This driver requires a newer version of Android.");
                }
                name = meta.optString("name", library);
                driverVersion = meta.optString("driverVersion", "Vulkan 1.3");
            } else {
                // Universal fallback: locate any .so library inside the archive
                packageRoot = stage;
                File soFile = findVulkanSo(stage);
                if (soFile == null) throw new IOException("No Vulkan driver library (.so) found in ZIP.");
                packageRoot = soFile.getParentFile();
                library = soFile.getName();
                name = generateDriverName(library);
            }

            if (!library.matches("[A-Za-z0-9_.+-]+\\.so")) throw new IOException("libraryName is not valid: " + library);
            DriverArchive.validateLibrary(new File(packageRoot, library));

            if (name == null || name.trim().isEmpty()) name = library;
            if (name.length() > 120) name = name.substring(0, 120);

            String relative = "payload";
            File installed = new File(root(context), UUID.randomUUID().toString());
            if (!installed.mkdirs()) throw new IOException("Could not install driver.");
            try {
                if (!packageRoot.renameTo(new File(installed, relative))) {
                    throw new IOException("Could not move package to destination.");
                }
                JSONObject record = new JSONObject().put("name", name).put("libraryName", library).put("folder", relative);
                if (!driverVersion.isEmpty()) record.put("driverVersion", driverVersion);
                Files.write(new File(installed, "driver.json").toPath(), record.toString().getBytes(StandardCharsets.UTF_8));
                readOnly(new File(installed, relative));
            } catch (Exception error) {
                DriverArchive.delete(installed);
                throw error;
            }
            return new Driver(installed.getName(), name, new File(installed, relative), library);
        } finally {
            if (stage.exists()) DriverArchive.delete(stage);
        }
    }

    private static File findVulkanSo(File dir) {
        File[] files = dir.listFiles();
        if (files == null) return null;
        for (File f : files) {
            if (f.isFile() && f.getName().endsWith(".so")) {
                String n = f.getName().toLowerCase(Locale.ROOT);
                if (n.contains("vulkan") || n.contains("panfrost") || n.contains("freedreno") || n.contains("turnip")) {
                    return f;
                }
            }
        }
        for (File f : files) {
            if (f.isDirectory()) {
                File sub = findVulkanSo(f);
                if (sub != null) return sub;
            }
        }
        return null;
    }

    private static String generateDriverName(String lib) {
        String lower = lib.toLowerCase(Locale.ROOT);
        if (lower.contains("panfrost")) return "PanVK Mali Bifrost/Valhall";
        if (lower.contains("freedreno")) return "Turnip Adreno Vulkan Driver";
        if (lower.contains("vulkan")) return "Universal Vulkan Driver (" + lib + ")";
        return lib;
    }

    private static File findMeta(File root) throws IOException {
        if (new File(root, "meta.json").isFile()) return root;
        File found = null;
        File[] children = root.listFiles(File::isDirectory);
        if (children != null) for (File child : children) {
            File candidate = findMeta(child);
            if (candidate != null) {
                if (found != null) throw new IOException("Multiple meta.json found; package is ambiguous.");
                found = candidate;
            }
        }
        return found;
    }

    private static void readOnly(File file) throws IOException {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) readOnly(child);
        else if (!file.setReadOnly()) throw new IOException("Could not protect driver library.");
    }

    static void remove(Context context, Driver driver) throws IOException {
        File target = new File(root(context), driver.id);
        if (!target.getCanonicalFile().getParentFile().equals(root(context).getCanonicalFile()))
            throw new IOException("Invalid driver path.");
        DriverArchive.delete(target);
        if (selectedId(context).equals(driver.id)) select(context, "");
    }

    /** {hooks dir, temp dir, driver dir, library}; empty driver fields select the system driver. */
    static String[] loaderArguments(Context context) {
        Driver driver = android.os.Process.is64Bit() ? selected(context) : null;
        return new String[] {context.getApplicationInfo().nativeLibraryDir, context.getCacheDir().getAbsolutePath(),
                driver == null ? "" : driver.directory.getAbsolutePath() + "/", driver == null ? "" : driver.library};
    }
}
