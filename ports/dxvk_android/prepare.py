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

    uint32_t contrast_mode = 0u;
    static auto s_mode_fn = []() -> uint32_t(*)() {
#ifdef __ANDROID__
      void* sym = nullptr;
      void* h_gc = dlopen("libgraphics_cache.so", RTLD_NOLOAD);
      if (!h_gc) h_gc = dlopen("libgraphics_cache.so", RTLD_NOW | RTLD_GLOBAL);
      if (h_gc) sym = dlsym(h_gc, "nfs_contrast_get_mode");
      if (!sym) {
        void* h_core = dlopen("libgame_core.so", RTLD_NOLOAD);
        if (!h_core) h_core = dlopen("libgame_core.so", RTLD_NOW | RTLD_GLOBAL);
        if (h_core) sym = dlsym(h_core, "nfs_contrast_get_mode");
      }
      if (!sym) sym = dlsym(RTLD_DEFAULT, "nfs_contrast_get_mode");
      return reinterpret_cast<uint32_t(*)()>(sym);
#else
      return reinterpret_cast<uint32_t(*)()>(dlsym(RTLD_DEFAULT, "nfs_contrast_get_mode"));
#endif
    }();
    if (s_mode_fn) contrast_mode = s_mode_fn();
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


vec4 apply_enhanced_contrast(vec4 color, uint enabled) {
  if (enabled == 0u) return color;
  vec3 c = clamp(color.rgb, 0.0, 1.0);

  // 1. Exposure (EV +0.01): 2^0.01 ~= 1.00695555
  c = clamp(c * 1.00695555, 0.0, 1.0);

  // 2. Contrast (+15): Sigmoidal S-curve blend
  vec3 s_curve = c * c * (3.0 - 2.0 * c);
  c = mix(c, s_curve, 0.15);

  // 3. Highlight (-35) & White (+16): Glare recovery and specular boost
  vec3 th = clamp((c - 0.5) / 0.5, 0.0, 1.0);
  vec3 wh_weight = th * th * (3.0 - 2.0 * th);
  c += -0.35 * 0.20 * wh_weight * (c - 0.5);

  vec3 tw = clamp((c - 0.75) / 0.25, 0.0, 1.0);
  vec3 ww_weight = tw * tw * (3.0 - 2.0 * tw);
  c += 0.16 * 0.15 * ww_weight * (1.0 - c);

  // 4. Shadow (-12) & Black (+22): Rich deep shadows with lifted toe
  vec3 ts = clamp((0.5 - c) / 0.5, 0.0, 1.0);
  vec3 ws_weight = ts * ts * (3.0 - 2.0 * ts);
  c += -0.12 * 0.18 * ws_weight * (0.5 - c);

  vec3 tb = clamp((0.25 - c) / 0.25, 0.0, 1.0);
  vec3 wb_weight = tb * tb * (3.0 - 2.0 * tb);
  c += 0.22 * 0.10 * wb_weight * c;

  // 5. Dehaze (+20): Atmospheric clarity & chroma saturation boost (+20%)
  c = clamp(c, 0.0, 1.0);
  c = c - 0.024 * (1.0 - c) * c;
  c = clamp(c, 0.0, 1.0);

  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
  vec3 vivid = mix(vec3(luma), c, 1.20);
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
