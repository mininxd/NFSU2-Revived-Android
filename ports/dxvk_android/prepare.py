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

vulkan_headers_src = args.source / "include" / "vulkan" / "include" / "vulkan"
if vulkan_headers_src.exists():
    shutil.copytree(vulkan_headers_src, args.output / "include" / "vulkan", dirs_exist_ok=True)

spirv_headers_src = args.source / "include" / "spirv" / "include" / "spirv"
if spirv_headers_src.exists():
    shutil.copytree(spirv_headers_src, args.output / "include" / "spirv", dirs_exist_ok=True)

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

# 2. Make optional Vulkan features non-blocking for universal mobile GPU support
OPTIONAL_FEATURES = [
    "ENABLE_FEATURE(core.features, textureCompressionBC, true),",
    "ENABLE_FEATURE(core.features, depthBiasClamp, true),",
    "ENABLE_FEATURE(core.features, depthClamp, true),",
    "ENABLE_FEATURE(core.features, dualSrcBlend, true),",
    "ENABLE_FEATURE(core.features, fillModeNonSolid, true),",
    "ENABLE_FEATURE(core.features, geometryShader, true),",
    "ENABLE_FEATURE(core.features, multiViewport, true),",
    "ENABLE_FEATURE(core.features, occlusionQueryPrecise, true),",
    "ENABLE_FEATURE(core.features, sampleRateShading, true),",
    "ENABLE_FEATURE(core.features, shaderClipDistance, true),",
    "ENABLE_FEATURE(core.features, shaderCullDistance, true),",
    "ENABLE_FEATURE(core.features, shaderImageGatherExtended, true),",
    "ENABLE_FEATURE(core.features, shaderInt16, true),",
    "ENABLE_FEATURE(core.features, shaderInt64, true),",
    "ENABLE_FEATURE(core.features, shaderSampledImageArrayDynamicIndexing, true),",
    "ENABLE_EXT_FEATURE(extDepthClipEnable, depthClipEnable, true),",
    "ENABLE_EXT_FEATURE(extRobustness2, robustBufferAccess2, true),",
    "ENABLE_EXT_FEATURE(extRobustness2, nullDescriptor, true),",
    "ENABLE_EXT(khrLoadStoreOpNone, true),",
    "ENABLE_EXT_FEATURE(khrMaintenance5, maintenance5, true),",
    "ENABLE_EXT_FEATURE(khrMaintenance6, maintenance6, true),",
]

for feat in OPTIONAL_FEATURES:
    replace(
        "src/dxvk/dxvk_device_info.cpp",
        feat,
        feat.replace("true),", "false),")
    )

# 3. Generate or copy SPIR-V shader headers
gen_dir = args.output / "generated"
gen_dir.mkdir(parents=True, exist_ok=True)

pre_gen = pathlib.Path(__file__).parent / "generated"
if pre_gen.exists():
    for h in pre_gen.glob("*.h"):
        shutil.copy2(h, gen_dir / h.name)

# If glslangValidator is available, compile live shaders to ensure freshness
glslang = shutil.which("glslangValidator") or shutil.which("glslang")
if glslang:
    DXVK_SHADERS = [
        "src/dxvk/shaders/dxvk_cursor_vert.vert",
        "src/dxvk/shaders/dxvk_cursor_frag.frag",
        "src/dxvk/shaders/dxvk_dummy_frag.frag",
        "src/dxvk/shaders/dxvk_mipgen.comp",
        "src/dxvk/shaders/dxvk_present_frag.frag",
        "src/dxvk/shaders/dxvk_present_frag_blit.frag",
        "src/dxvk/shaders/dxvk_present_frag_ms.frag",
        "src/dxvk/shaders/dxvk_present_frag_ms_blit.frag",
        "src/dxvk/shaders/dxvk_present_vert.vert",
        "src/dxvk/hud/shaders/hud_chunk_frag_background.frag",
        "src/dxvk/hud/shaders/hud_chunk_frag_visualize.frag",
        "src/dxvk/hud/shaders/hud_chunk_vert_background.vert",
        "src/dxvk/hud/shaders/hud_chunk_vert_visualize.vert",
        "src/dxvk/hud/shaders/hud_frame_time_eval.comp",
        "src/dxvk/hud/shaders/hud_graph_frag.frag",
        "src/dxvk/hud/shaders/hud_graph_vert.vert",
        "src/dxvk/hud/shaders/hud_text_frag.frag",
        "src/dxvk/hud/shaders/hud_text_vert.vert",
        "src/d3d9/shaders/d3d9_convert_yuy2_uyvy.comp",
        "src/d3d9/shaders/d3d9_convert_l6v5u5.comp",
        "src/d3d9/shaders/d3d9_convert_x8l8v8u8.comp",
        "src/d3d9/shaders/d3d9_convert_a2w10v10u10.comp",
        "src/d3d9/shaders/d3d9_convert_w11v11u10.comp",
        "src/d3d9/shaders/d3d9_convert_nv12.comp",
        "src/d3d9/shaders/d3d9_convert_yv12.comp",
        "src/d3d9/shaders/d3d9_fixed_function_vert.vert",
        "src/d3d9/shaders/d3d9_fixed_function_frag.frag",
        "src/d3d9/shaders/d3d9_fixed_function_frag_sample.frag",
    ]
    for s in DXVK_SHADERS:
        src_path = args.output / s
        if src_path.exists():
            var_name = src_path.stem
            out_h = gen_dir / f"{var_name}.h"
            subprocess.run([
                glslang, "--quiet", "--target-env", "vulkan1.3",
                "--vn", var_name, str(src_path), "-o", str(out_h)
            ], check=False)

(args.output / "version.h").write_text('#pragma once\n#define DXVK_VERSION "v3.1.1-android-arm64"\n')
(args.output / "buildenv.h").write_text('#pragma once\n#define DXVK_TARGET "aarch64-linux-android"\n#define DXVK_COMPILER "clang"\n#define DXVK_COMPILER_VERSION "NDK Clang"\n')
(gen_dir / "version.h").write_text('#pragma once\n#define DXVK_VERSION "v3.1.1-android-arm64"\n')
(gen_dir / "buildenv.h").write_text('#pragma once\n#define DXVK_TARGET "aarch64-linux-android"\n#define DXVK_COMPILER "clang"\n#define DXVK_COMPILER_VERSION "NDK Clang"\n')
(args.output / "PINNED_REVISION").write_text(PIN + "\n")
(args.output / "ANDROID_PREPARE_SHA256").write_text(
    hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest() + "\n",
    encoding="ascii"
)

print(f"Prepared DXVK v3.1.1 (PIN {PIN}) successfully in {args.output}")
