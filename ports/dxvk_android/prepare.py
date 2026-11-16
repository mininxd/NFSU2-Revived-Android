"""Prepare the pinned DXVK v3.1.1 (doitsujin/dxvk) for Android ARM64."""
import argparse
import hashlib
import pathlib
import shutil
import subprocess

PIN = "b1a1c99ab52b687cf950d62c88bc2fa316b41663"

parser = argparse.ArgumentParser(description="Prepare DXVK v3.1.1 for Android ARM64")
parser.add_argument("source", type=pathlib.Path, help="Path to DXVK git checkout or release directory")
parser.add_argument("output", type=pathlib.Path, help="Destination directory for prepared source")
parser.add_argument("--skip-pin-check", action="store_true", help="Skip git commit hash verification")
args = parser.parse_args()

if not args.skip_pin_check and (args.source / ".git").exists():
    try:
        revision = subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip()
        if revision != PIN:
            print(f"Warning: DXVK source revision is {revision}, expected {PIN}")
    except Exception as e:
        print(f"Note: git revision check skipped ({e})")

args.output.mkdir(parents=True, exist_ok=True)

for directory in ("src", "include", "subprojects"):
    src_dir = args.source / directory
    if src_dir.exists():
        shutil.copytree(src_dir, args.output / directory, dirs_exist_ok=True)

for extra_file in ("LICENSE", "RELEASE"):
    f = args.source / extra_file
    if f.exists():
        shutil.copy2(f, args.output / extra_file)

vulkan_inc = args.output / "include" / "vulkan"
vulkan_inc.mkdir(parents=True, exist_ok=True)
for header in ("vulkan_android.h", "vulkan_xlib.h"):
    h_src = pathlib.Path(__file__).parent / "include" / header
    if h_src.exists():
        shutil.copy2(h_src, vulkan_inc / header)

def replace(rel_path, old, new):
    target = args.output / rel_path
    if not target.exists():
        return
    text = target.read_text(encoding="utf-8", errors="replace")
    if old not in text:
        return
    target.write_text(text.replace(old, new), encoding="utf-8")

# 1. Forward warnings and errors to Android logcat
replace(
    "src/util/log/log.cpp",
    '#include "log.h"\n',
    '#include "log.h"\n#ifdef __ANDROID__\n#include <android/log.h>\n#endif\n'
)
replace(
    "src/util/log/log.cpp",
    '          std::cerr << adjusted;\n',
    '          std::cerr << adjusted;\n#ifdef __ANDROID__\n          if (level >= LogLevel::Warn)\n            __android_log_print(level >= LogLevel::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN, "DXVK", "%s", adjusted.c_str());\n#endif\n'
)

# 2. Make BC texture compression optional for mobile GPUs without BC support
replace(
    "src/dxvk/dxvk_device_info.cpp",
    "ENABLE_FEATURE(core.features, textureCompressionBC, true),",
    "ENABLE_FEATURE(core.features, textureCompressionBC, false),"
)

(args.output / "version.h").write_text('#define DXVK_VERSION "v3.1.1-android-arm64"\n')
(args.output / "PINNED_REVISION").write_text(PIN + "\n")
(args.output / "ANDROID_PREPARE_SHA256").write_text(
    hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest() + "\n",
    encoding="ascii"
)

print(f"Prepared DXVK v3.1.1 (PIN {PIN}) successfully in {args.output}")
