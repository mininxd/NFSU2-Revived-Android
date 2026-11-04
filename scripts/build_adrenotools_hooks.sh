#!/usr/bin/env bash
set -euo pipefail

# Build adrenotools hook libraries for ARM64:
# - libhook_impl.so
# - libmain_hook.so
# - libfile_redirect_hook.so
# - libgsl_alloc_hook.so
# - libnfsu2_diagnostics.so (with custom driver probe support)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$ROOT_DIR/android/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$OUT_DIR"

if [ -z "${CLANG_BIN:-}" ]; then
  for ndk_dir in "${ANDROID_HOME:-/usr/local/lib/android/sdk}/ndk" "${ANDROID_NDK_ROOT:-}" "/opt/android-ndk"; do
    if [ -n "$ndk_dir" ] && [ -d "$ndk_dir" ]; then
      CLANG_BIN=$(find "$ndk_dir" -name "aarch64-linux-android*-clang" 2>/dev/null | grep -E "android(24|25|26|27|28|29|30|31|32|33|34|35)-clang" | sort -V | head -n 1 || true)
      if [ -n "$CLANG_BIN" ]; then break; fi
    fi
  done
  if [ -z "$CLANG_BIN" ]; then
    CLANG_BIN=$(which aarch64-linux-android-clang || which aarch64-linux-gnu-gcc || true)
  fi
fi

if [ -z "${CLANG_BIN:-}" ] || [ ! -x "$CLANG_BIN" ]; then
  echo "Error: aarch64 NDK clang not found." >&2
  exit 1
fi

CLANGXX_BIN="${CLANG_BIN}++"
if [ ! -x "$CLANGXX_BIN" ]; then
  CLANGXX_BIN="$(dirname "$CLANG_BIN")/$(basename "$CLANG_BIN" | sed 's/clang$/clang++/')"
fi
if [ ! -x "$CLANGXX_BIN" ]; then
  CLANGXX_BIN="$CLANG_BIN"
fi

echo "Using C compiler: $CLANG_BIN"
echo "Using C++ compiler: $CLANGXX_BIN"

ADRENOTOOLS_DIR="$ROOT_DIR/third_party/libadrenotools"
HOOKS_DIR="$ADRENOTOOLS_DIR/src/hook"
LINKERNS_DIR="$ADRENOTOOLS_DIR/lib/linkernsbypass"

if [ ! -f "$HOOKS_DIR/hook_impl.cpp" ] || [ ! -f "$LINKERNS_DIR/android_linker_ns.cpp" ]; then
  echo "Adrenotools submodule files missing. Updating git submodules..."
  git -C "$ROOT_DIR" submodule update --init --recursive third_party/libadrenotools || true
fi

if [ ! -f "$HOOKS_DIR/hook_impl.cpp" ] || [ ! -f "$LINKERNS_DIR/android_linker_ns.cpp" ]; then
  echo "Warning: Adrenotools source files still not found at $HOOKS_DIR. Skipping hook build."
  exit 0
fi

echo "1. Building libhook_impl.so..."
$CLANGXX_BIN -std=c++17 -O2 -shared -fPIC -Wall -Wextra -fvisibility=hidden \
  -Wl,-soname,libhook_impl.so \
  -Wl,-z,max-page-size=16384 \
  -static-libstdc++ \
  -I"$ADRENOTOOLS_DIR/include" \
  -I"$LINKERNS_DIR" \
  -I"$HOOKS_DIR" \
  "$HOOKS_DIR/hook_impl.cpp" \
  "$LINKERNS_DIR/android_linker_ns.cpp" \
  "$LINKERNS_DIR/elf_soname_patcher.cpp" \
  -llog -landroid -ldl \
  -o "$OUT_DIR/libhook_impl.so"

echo "2. Building libmain_hook.so..."
$CLANG_BIN -O2 -shared -fPIC -Wall -Wextra -fvisibility=hidden \
  -Wl,-soname,libmain_hook.so \
  -Wl,-z,global \
  -Wl,-z,max-page-size=16384 \
  -I"$ADRENOTOOLS_DIR/include" \
  -I"$LINKERNS_DIR" \
  -I"$HOOKS_DIR" \
  "$HOOKS_DIR/main_hook.c" \
  -L"$OUT_DIR" -lhook_impl \
  -o "$OUT_DIR/libmain_hook.so"

echo "3. Building libfile_redirect_hook.so..."
$CLANG_BIN -O2 -shared -fPIC -Wall -Wextra -fvisibility=hidden \
  -Wl,-soname,libfile_redirect_hook.so \
  -Wl,-z,global \
  -Wl,-z,max-page-size=16384 \
  -I"$ADRENOTOOLS_DIR/include" \
  -I"$LINKERNS_DIR" \
  -I"$HOOKS_DIR" \
  "$HOOKS_DIR/file_redirect_hook.c" \
  -L"$OUT_DIR" -lhook_impl \
  -o "$OUT_DIR/libfile_redirect_hook.so"

echo "4. Building libgsl_alloc_hook.so..."
$CLANG_BIN -O2 -shared -fPIC -Wall -Wextra -fvisibility=hidden \
  -Wl,-soname,libgsl_alloc_hook.so \
  -Wl,-z,global \
  -Wl,-z,max-page-size=16384 \
  -I"$ADRENOTOOLS_DIR/include" \
  -I"$LINKERNS_DIR" \
  -I"$HOOKS_DIR" \
  "$HOOKS_DIR/gsl_alloc_hook.c" \
  -L"$OUT_DIR" -lhook_impl \
  -o "$OUT_DIR/libgsl_alloc_hook.so"

echo "5. Building libnfsu2_diagnostics.so with custom driver probe support..."
if $CLANGXX_BIN -std=c++17 -O2 -shared -fPIC -Wall -Wextra -Wno-missing-field-initializers -fvisibility=hidden \
  -DNFS_CUSTOM_DRIVERS \
  -Wl,-soname,libnfsu2_diagnostics.so \
  -Wl,-z,max-page-size=16384 \
  -I"$ROOT_DIR/android/app/src/main/cpp" \
  -I"$ADRENOTOOLS_DIR/include" \
  -I"$LINKERNS_DIR" \
  -I"$ADRENOTOOLS_DIR" \
  -I"$ADRENOTOOLS_DIR/src" \
  "$ROOT_DIR/android/app/src/main/cpp/gpu_probe.cpp" \
  "$ROOT_DIR/android/app/src/main/cpp/vulkan_driver.cpp" \
  "$ADRENOTOOLS_DIR/src/bcenabler.cpp" \
  "$ADRENOTOOLS_DIR/src/driver.cpp" \
  "$LINKERNS_DIR/android_linker_ns.cpp" \
  "$LINKERNS_DIR/elf_soname_patcher.cpp" \
  -static-libstdc++ \
  -llog -ldl -landroid \
  -o "$OUT_DIR/libnfsu2_diagnostics.so"; then
  echo "libnfsu2_diagnostics.so built with custom driver support."
else
  echo "Adrenotools build failed for diagnostics; building fallback libnfsu2_diagnostics.so without custom drivers..."
  $CLANGXX_BIN -std=c++17 -O2 -shared -fPIC -Wall -Wextra -Wno-missing-field-initializers \
    -Wl,-z,max-page-size=16384 \
    -I"$ROOT_DIR/android/app/src/main/cpp" \
    "$ROOT_DIR/android/app/src/main/cpp/gpu_probe.cpp" \
    "$ROOT_DIR/android/app/src/main/cpp/vulkan_driver.cpp" \
    -static-libstdc++ \
    -llog -ldl \
    -o "$OUT_DIR/libnfsu2_diagnostics.so"
fi

echo "Adrenotools libraries built successfully:"
ls -lh "$OUT_DIR"/lib*hook*.so "$OUT_DIR"/libnfsu2_diagnostics.so
