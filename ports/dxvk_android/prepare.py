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

# Apply hardware tone curve / gamma ramp on Android even in Windowed swapchain mode
replace(
    "src/d3d9/d3d9_swapchain.cpp",
    "if (!isIdentity && !m_presentParams.Windowed)",
    "if (!isIdentity)"
)

# Zero-overhead Enhanced Contrast GPU post-processing in presenter swapchain
replace(
    "src/dxvk/dxvk_swapchain_blitter.cpp",
    '#include "dxvk_swapchain_blitter.h"\n',
    '#include "dxvk_swapchain_blitter.h"\n#ifdef __ANDROID__\n#include <dlfcn.h>\n#ifndef RTLD_DEFAULT\n#define RTLD_DEFAULT ((void*)0)\n#endif\n#endif\n'
)
replace(
    "src/dxvk/dxvk_swapchain_blitter.h",
    "      uint32_t   samplerCursor;\n    };",
    "      uint32_t   samplerCursor;\n      uint32_t   enhancedContrast;\n    };"
)
replace(
    "src/dxvk/dxvk_swapchain_blitter.cpp",
    "    args.samplerCursor = cursorSampler;\n\n    ctx->cmdBindPipeline",
    """    args.samplerCursor = cursorSampler;

    static int s_env_contrast = -1;
    if (s_env_contrast < 0) {
      const char* e = getenv("NFSU2_ENHANCED_CONTRAST");
      if (e && (strcmp(e, "0") == 0 || strcasecmp(e, "off") == 0 || strcasecmp(e, "false") == 0)) {
        s_env_contrast = 0;
      } else if (e && (strcmp(e, "2") == 0 || strcasecmp(e, "punchy") == 0)) {
        s_env_contrast = 2;
      } else if (e && (strcmp(e, "1") == 0 || strcasecmp(e, "on") == 0 || strcasecmp(e, "true") == 0)) {
        s_env_contrast = 1;
      } else {
        s_env_contrast = -2;
      }
    }
    uint32_t contrast_mode = 1u;
    if (s_env_contrast >= 0) {
      contrast_mode = static_cast<uint32_t>(s_env_contrast);
    } else {
      static auto s_mode_fn = reinterpret_cast<uint32_t(*)()>(dlsym(RTLD_DEFAULT, "nfs_contrast_get_mode"));
      if (s_mode_fn) contrast_mode = s_mode_fn();
    }
    args.enhancedContrast = contrast_mode;

    ctx->cmdBindPipeline"""
)
replace(
    "src/dxvk/shaders/dxvk_present_common.glsl",
    """uniform present_info_t {
  ivec2 src_offset;
  ivec2 src_extent;
  ivec2 dst_offset;
  ivec2 cursor_offset;
  ivec2 cursor_extent;
  uint sampler_gamma;
  uint sampler_cursor;
};""",
    """uniform present_info_t {
  ivec2 src_offset;
  ivec2 src_extent;
  ivec2 dst_offset;
  ivec2 cursor_offset;
  ivec2 cursor_extent;
  uint sampler_gamma;
  uint sampler_cursor;
  uint enhanced_contrast;
};


vec4 apply_enhanced_contrast(vec4 color, uint mode) {
  if (mode == 0u) return color;
  vec3 c = clamp(color.rgb, 0.0, 1.0);
  // High-performance NFSU2 Enhanced Contrast S-curve:
  // f(x) = x^2 * (3 - 2x)
  vec3 s_curve = c * c * (3.0 - 2.0 * c);
  // Blend curve based on mode (1 = balanced cinematic, 2 = punchy)
  float blend = (mode == 2u) ? 0.80 : 0.60;
  vec3 contrast = mix(c, s_curve, blend);
  // Boost neon color saturation while keeping asphalt natural
  float luma = dot(contrast, vec3(0.2126, 0.7152, 0.0722));
  float sat_boost = (mode == 2u) ? 1.25 : 1.15;
  vec3 vivid = mix(vec3(luma), contrast, sat_boost);
  return vec4(clamp(vivid, 0.0, 1.0), color.a);
}"""
)
replace(
    "src/dxvk/shaders/dxvk_present_common.glsl",
    """      return color;

    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: {
      color.rgb = sc_rgb_to_nits(color.rgb) / SDR_NITS;

      if (!c_dst_is_srgb)
        color.rgb = linear_to_srgb(color.rgb);

      return color;
    }

    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
      return color;""",
    """      color = apply_enhanced_contrast(color, enhanced_contrast);
      return color;

    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: {
      color.rgb = sc_rgb_to_nits(color.rgb) / SDR_NITS;

      if (!c_dst_is_srgb)
        color.rgb = linear_to_srgb(color.rgb);

      color = apply_enhanced_contrast(color, enhanced_contrast);
      return color;
    }

    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
      color = apply_enhanced_contrast(color, enhanced_contrast);
      return color;"""
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

# 4. Support custom Vulkan driver entry point on Android (adrenotools / Turnip)
replace(
    "src/vulkan/vulkan_loader.cpp",
    "  LibraryLoader::LibraryLoader() {\n    std::tie(m_library, m_getInstanceProcAddr) = loadVulkanLibrary();\n  }",
    """#ifdef __ANDROID__
extern "C" PFN_vkGetInstanceProcAddr dxvkAndroidGetInstanceProcAddr();
#endif

  LibraryLoader::LibraryLoader() {
#ifdef __ANDROID__
    PFN_vkGetInstanceProcAddr customProc = dxvkAndroidGetInstanceProcAddr();
    if (customProc) {
      Logger::info("Vulkan: Using custom Vulkan driver entry point (adrenotools / Turnip)");
      m_getInstanceProcAddr = customProc;
      m_library = nullptr;
      return;
    }
#endif
    std::tie(m_library, m_getInstanceProcAddr) = loadVulkanLibrary();
  }"""
)

# 5. Generate or copy SPIR-V shader headers
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
