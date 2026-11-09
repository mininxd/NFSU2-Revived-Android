#!/usr/bin/env python3
"""
NFSU2 Native Rust Migration & Subsystem Coverage Tracker
Generates an interactive SVG visualization and JSON configuration of the Rust conversion
progress, SIMD pipelines, and high-performance native crates for NFSU2 Android/ARM64.
"""

import os
import sys
import json
import argparse
from datetime import datetime
from html import escape

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_PATH = os.path.join(ROOT_DIR, "library-progress.json")
SVG_PATH = os.path.join(ROOT_DIR, "library-progress.svg")

# Comprehensive database of Rust native conversion modules across crates
DEFAULT_RUST_MODULES = {
    "perf_cores": [
        ("cpu_detect", "implemented", "sysfs cpufreq and capacity auto-detection (Cortex-X / A7xx / A5xx)"),
        ("cluster_mask", "implemented", "64-bit performance core bitmask calculation"),
        ("sched_affinity", "implemented", "Dynamic thread affinity pinning via SYS_sched_setaffinity"),
        ("priority_tune", "implemented", "EAS thread priority elevation and -10 nice tuning"),
        ("comm_scanner", "implemented", "Process task comm scanner prioritizing emu, audio & render threads"),
        ("jni_bindings", "implemented", "Zero-overhead JNI bridge for PerformanceCores.java"),
        ("steer_math", "implemented", "Progressive cubic steering curve and deadzone computation"),
        ("c_abi_export", "implemented", "nfs_perf_cores_bind C ABI export for companion libraries")
    ],

    "roam_rs": [
        ("wire_reader", "implemented", "Bounds-safe zero-copy binary wire decoding (wire.rs)"),
        ("wire_writer", "implemented", "Bounds-safe binary wire packet serializer (wire.rs)"),
        ("crc32_fast", "implemented", "Hardware-accelerated CRC32 packet integrity hashing (crc.rs)"),
        ("fnv1a_hash", "implemented", "64-bit FNV-1a room and identifier string hashing (hash.rs)"),
        ("simd_vector", "implemented", "128-bit vector dot, cross, and distance operations (math.rs)"),
        ("quaternion_lerp", "implemented", "Rotational spherical linear interpolation and pacing (interp.rs)"),
        ("timestep_lock", "implemented", "Locked 60 Hz simulation time-step governor at 0x007875BC (engine.rs)"),
        ("high_fps_patch", "implemented", "90/120 Hz high-refresh rate timing patch at 0x0057A084 (engine.rs)"),
        ("crowd_filter", "implemented", "Trackside 3D crowd suppression filter at 0x00870CD0 (crowd.rs)"),
        ("sync_guard", "implemented", "Deadlock and spin-stall resolution watchdog (sync_guard.rs)"),
        ("car_cfg_sync", "implemented", "Visual customization and performance part sync (carcfg.rs)"),
        ("ws_switch_frame", "implemented", "RFC 6455 WebSocket framing and SIMD XOR mask unmasking (ws_switch.rs)"),
        ("packet_demux", "implemented", "Multiplexed Free Roam UDP and Online TCP frame demuxer (ws_switch.rs)"),
        ("engine_pacing", "implemented", "Anti-slowmotion wall-clock elapsed time filter (engine.rs)")
    ],

    "graphics_cache": [
        ("async_compiler", "implemented", "Multi-threaded Vulkan pipeline and shader compilation engine"),
        ("d3d9_filter_rs", "implemented", "D3D9 SetRenderState redundant call deduplication shadow filter"),
        ("d3d9_filter_tex", "implemented", "D3D9 SetTexture & SetTextureStageState state shadow filter"),
        ("d3d9_filter_vtx", "implemented", "D3D9 SetFVF & SetStreamSource redundant vertex state filter"),
        ("d3d9_filter_pipe", "implemented", "D3D9 SetVertexShader & SetPixelShader shadow cache"),
        ("d3d9_filter_view", "implemented", "D3D9 SetViewport & SetScissorRect redundant bounding filter"),
        ("gles_optimizer", "implemented", "GLES driver state shadow filter dropping redundant GL calls"),
        ("dxt1_decoder", "implemented", "Fixed-point fast-path DXT1/BC1 block decompressor"),
        ("dxt3_decoder", "implemented", "Fast-path explicit 4-bit alpha DXT3/BC2 block decompressor"),
        ("dxt5_decoder", "implemented", "Fixed-point fast-path DXT5/BC3 block decompressor"),
        ("hor_plus_fov", "implemented", "Dynamic Hor+ FOV expansion calculation for 20:9 ultra-wide screens"),
        ("proj_adjust", "implemented", "Perspective projection matrix aspect ratio scaling"),
        ("render_queue", "implemented", "Asynchronous lockless ring-buffer render command queue"),
        ("dynamic_thunk", "implemented", "Decoded DynamicThunk bytecode cache and JIT bridge")
    ],

    "simd_audio": [
        ("neon_sinc_16", "implemented", "ARM64 NEON 128-bit 16-tap sinc kernel resampler (simd_audio.rs)"),
        ("scalar_sinc_16", "implemented", "Portable scalar fallback sinc resampler for non-ARM hosts"),
        ("audio_clamp", "implemented", "SIMD audio buffer clamping, saturation, and peak stats"),
        ("mixer_render", "implemented", "Multi-voice linear mixdown and accumulator engine"),
        ("clock_sync", "implemented", "DirectSound synthetic timing clock synchronizer"),
        ("watchdog_ping", "implemented", "AAudio stream stall detector and reconnect trigger")
    ],

    "win32_dinput": [
        ("ff_effect_mgr", "implemented", "DirectInput force feedback effect creation and lifecycle manager"),
        ("motor_timing", "implemented", "Dual-motor vibration timing, gain curve, and frequency modulator"),
        ("pad_remap", "implemented", "Host Android gamepad button and Z-axis trigger normalization"),
        ("ff_envelope", "implemented", "Attack, fade, and sustain envelope generator"),
        ("periodic_wave", "implemented", "Sine, square, and triangle periodic vibration waveforms"),
        ("jni_gamepad", "implemented", "Fast-path JNI bridge for racing controls and joypad dispatcher")
    ],

    "win32_compat": [
        ("fs_cache_lookup", "implemented", "Case-insensitive in-memory path lookup and directory cache"),
        ("gdi_dc_mgr", "implemented", "GDI compatible device context and bitmap memory manager"),
        ("gdi_font_text", "implemented", "GDI font metrics and ExtTextOutA glyph bounding cache"),
        ("netapi_netbios", "implemented", "Netbios LANA enumeration and MAC address query emulation"),
        ("netapi_wksta", "implemented", "Workstation info and workgroup domain join status shims"),
        ("ddraw_modes", "implemented", "DirectDraw cooperative level and display mode enumeration"),
        ("tapi_line_init", "implemented", "Telephony API line initialize, negotiate and device caps"),
        ("tapi_calls", "implemented", "Telephony line make call, answer, and teardown handlers"),
        ("comm_timeouts", "implemented", "Communications port DCB and timeouts emulation"),
        ("shell_exec", "implemented", "ShellExecuteA external launcher and browser URL dispatcher"),
        ("shfolder_path", "implemented", "SHGetFolderPathA CSIDL directory redirector"),
        ("math_neon_mat4", "implemented", "128-bit ARM64 NEON 4x4 matrix multiplication (math_simd.rs)"),
        ("physics_aabb", "implemented", "SIMD axis-aligned bounding box collision overlap test"),
        ("suspension_ray", "implemented", "Spring and damper suspension raycast collision physics")
    ],

    "touch_input": [
        ("zero_alloc_map", "implemented", "Zero-allocation flat primitive pointer array (0 GC overhead)"),
        ("curve_steer", "implemented", "Rust progressive steering sensitivity and deadband curve"),
        ("pedal_smoothing", "implemented", "Exponential gas/brake pressure response filter"),
        ("scancode_mirror", "implemented", "High-frequency DirectInput keyboard scan code mapper"),
        ("tilt_kalman", "stub", "In-progress NEON tilt accelerometer Kalman smoothing filter"),
        ("haptic_pulse", "implemented", "Low-latency touch click and rumble haptic response")
    ]
}

def load_data(json_path=None):
    path = json_path or JSON_PATH
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                d = json.load(f)
                if "Rust" in d.get("title", ""):
                    return d
        except Exception as e:
            print(f"Warning: Loading {path}: {e}")
    
    # Construct initial JSON model for Rust conversion
    data = {
        "title": "NFSU2 Native Rust Migration & Subsystem Coverage",
        "subtitle": "Pure Rust Performance Engines, SIMD Pipelines & Driver Shims",
        "description": "Migration status and conversion coverage from C/C++/Java into high-performance, memory-safe Rust crates.",
        "last_updated": datetime.utcnow().strftime("%Y-%m-%d %H:%M:%SZ"),
        "libraries": {}
    }
    for lib, funcs in DEFAULT_RUST_MODULES.items():
        data["libraries"][lib] = [
            {"name": name, "status": status, "description": desc}
            for name, status, desc in funcs
        ]
    save_data(data, path)
    return data

def save_data(data, json_path=None):
    path = json_path or JSON_PATH
    data["last_updated"] = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%SZ")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print(f"Saved configuration database to: {path}")

def calculate_stats(data):
    total = 0
    implemented = 0
    stubbed = 0
    missing = 0
    lib_stats = {}

    for lib, funcs in data["libraries"].items():
        l_tot = len(funcs)
        l_imp = sum(1 for f in funcs if f["status"] == "implemented")
        l_stb = sum(1 for f in funcs if f["status"] == "stub")
        l_mis = sum(1 for f in funcs if f["status"] == "missing")
        total += l_tot
        implemented += l_imp
        stubbed += l_stb
        missing += l_mis
        lib_stats[lib] = {
            "total": l_tot,
            "implemented": l_imp,
            "stubbed": l_stb,
            "missing": l_mis,
            "percent": (l_imp + l_stb * 0.5) / l_tot * 100.0 if l_tot else 0.0
        }

    overall_pct = (implemented + stubbed * 0.5) / total * 100.0 if total else 0.0
    return {
        "total": total,
        "implemented": implemented,
        "stubbed": stubbed,
        "missing": missing,
        "percent": overall_pct,
        "libraries": lib_stats
    }

def generate_svg(data, output_path=SVG_PATH):
    stats = calculate_stats(data)
    
    WIDTH = 960
    # Colors matching dark high-tech Rust theme
    BG_COLOR = "#1b1f24"
    CARD_BG = "#161b22"
    BORDER_COLOR = "#30363d"
    COLOR_IMP = "#f97316"   # Rust Orange
    COLOR_STUB = "#eab308"  # Amber Gold
    COLOR_MISS = "#475569"  # Slate Grey
    COLOR_ACCENT = "#38bdf8"# Sky Cyan
    TEXT_MAIN = "#f8fafc"
    TEXT_MUTED = "#94a3b8"
    TEXT_DIM = "#64748b"

    elements = []
    
    # Outer Card & Background
    elements.append(f'<rect width="100%" height="100%" fill="{BG_COLOR}"/>')

    # Header Section with Rust Crab Accent
    y = 38
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, -apple-system, sans-serif" font-size="22" font-weight="bold" fill="{TEXT_MAIN}">{escape(data["title"])}</text>')
    y += 24
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, -apple-system, sans-serif" font-size="14" fill="{COLOR_IMP}">{escape(data["subtitle"])}</text>')
    y += 20
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, -apple-system, sans-serif" font-size="12" fill="{TEXT_MUTED}">{escape(data["description"])}</text>')

    # KPI Summary Cards
    y += 20
    card_width = (WIDTH - 64 - 24) / 4
    cards_data = [
        ("Tracked Modules", f"{stats['total']}", "Total architecture", COLOR_ACCENT),
        ("Pure Rust (Native)", f"{stats['implemented']} ({stats['implemented']*100.0/stats['total']:.1f}%)", "Zero GC / SIMD", COLOR_IMP),
        ("Hybrid / In-Progress", f"{stats['stubbed']} ({stats['stubbed']*100.0/stats['total']:.1f}%)", "C/Rust bridged", COLOR_STUB),
        ("Pending Migration", f"{stats['missing']} ({stats['missing']*100.0/stats['total']:.1f}%)", "Legacy C / Java", COLOR_MISS),
    ]

    for idx, (label, val, sub, col) in enumerate(cards_data):
        cx = 32 + idx * (card_width + 8)
        elements.append(f'<rect x="{cx:.1f}" y="{y}" width="{card_width:.1f}" height="64" rx="8" fill="{CARD_BG}" stroke="{BORDER_COLOR}" stroke-width="1"/>')
        elements.append(f'<rect x="{cx:.1f}" y="{y}" width="4" height="64" rx="2" fill="{col}"/>')
        elements.append(f'<text x="{cx + 14:.1f}" y="{y + 20}" font-family="Segoe UI, sans-serif" font-size="11" font-weight="600" fill="{TEXT_MUTED}" text-transform="uppercase">{escape(label)}</text>')
        elements.append(f'<text x="{cx + 14:.1f}" y="{y + 42}" font-family="Segoe UI, sans-serif" font-size="16" font-weight="bold" fill="{TEXT_MAIN}">{escape(val)}</text>')
        elements.append(f'<text x="{cx + 14:.1f}" y="{y + 56}" font-family="Segoe UI, sans-serif" font-size="10" fill="{TEXT_DIM}">{escape(sub)}</text>')

    y += 82

    # Section 1: Subsystem Breakdown Rows
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="15" font-weight="bold" fill="{TEXT_MAIN}">Rust Crate &amp; Engine Subsystems</text>')
    elements.append(f'<text x="{WIDTH-32}" y="{y}" font-family="Segoe UI, sans-serif" font-size="12" fill="{TEXT_MUTED}" text-anchor="end">Weighted Migration: {stats["percent"]:.1f}%</text>')
    y += 14

    bar_x = 180
    bar_width = WIDTH - 32 - bar_x - 140

    for lib, lstat in stats["libraries"].items():
        elements.append(f'<text x="32" y="{y+12}" font-family="monospace" font-size="12" font-weight="600" fill="{TEXT_MAIN}">{escape(lib)}</text>')
        elements.append(f'<rect x="{bar_x}" y="{y}" width="{bar_width}" height="14" rx="3" fill="{COLOR_MISS}"/>')
        
        imp_w = (lstat["implemented"] / lstat["total"]) * bar_width if lstat["total"] else 0
        stb_w = (lstat["stubbed"] / lstat["total"]) * bar_width if lstat["total"] else 0
        
        if imp_w > 0:
            elements.append(f'<rect x="{bar_x}" y="{y}" width="{imp_w:.1f}" height="14" rx="3" fill="{COLOR_IMP}"><title>{lib}: {lstat["implemented"]} pure Rust</title></rect>')
        if stb_w > 0:
            elements.append(f'<rect x="{bar_x + imp_w:.1f}" y="{y}" width="{stb_w:.1f}" height="14" rx="3" fill="{COLOR_STUB}"><title>{lib}: {lstat["stubbed"]} hybrid</title></rect>')

        pct_label = f'{lstat["implemented"]}/{lstat["total"]} ({lstat["percent"]:.1f}%)'
        elements.append(f'<text x="{WIDTH-32}" y="{y+11}" font-family="monospace" font-size="11" fill="{TEXT_MUTED}" text-anchor="end">{pct_label}</text>')
        y += 24

    y += 12

    # Section 2: Interactive Function Discovery Grid
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="15" font-weight="bold" fill="{TEXT_MAIN}">Rust Native Component Matrix</text>')
    y += 18
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="12" fill="{TEXT_DIM}">Each tile represents a converted module or SIMD pipeline. Hover over cells for implementation details.</text>')
    
    # Legend
    leg_x = WIDTH - 32 - 320
    elements.append(f'<rect x="{leg_x}" y="{y-12}" width="10" height="10" rx="2" fill="{COLOR_IMP}"/>')
    elements.append(f'<text x="{leg_x+16}" y="{y-3}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_MUTED}">Pure Rust</text>')
    elements.append(f'<rect x="{leg_x+105}" y="{y-12}" width="10" height="10" rx="2" fill="{COLOR_STUB}"/>')
    elements.append(f'<text x="{leg_x+121}" y="{y-3}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_MUTED}">Hybrid / Bridge</text>')
    elements.append(f'<rect x="{leg_x+210}" y="{y-12}" width="10" height="10" rx="2" fill="{COLOR_MISS}"/>')
    elements.append(f'<text x="{leg_x+226}" y="{y-3}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_MUTED}">Pending</text>')

    y += 16

    # Render Grid Cells (22 columns per row for clean spacing)
    COLS = 22
    CELL_SIZE = 34
    CELL_GAP = 6
    GRID_START_X = 32

    # Flatten all functions in order
    all_funcs = []
    for lib, funcs in data["libraries"].items():
        for f in funcs:
            all_funcs.append((lib, f["name"], f["status"], f["description"]))

    for idx, (lib, name, status, desc) in enumerate(all_funcs):
        row = idx // COLS
        col = idx % COLS
        cx = GRID_START_X + col * (CELL_SIZE + CELL_GAP)
        cy = y + row * (CELL_SIZE + CELL_GAP)

        color = COLOR_IMP if status == "implemented" else (COLOR_STUB if status == "stub" else COLOR_MISS)
        tip = f"{lib}::{name} [{status.upper()}]\n{desc}"

        elements.append(
            f'<g>'
            f'<title>{escape(tip)}</title>'
            f'<rect x="{cx}" y="{cy}" width="{CELL_SIZE}" height="{CELL_SIZE}" rx="4" fill="{color}" '
            f'stroke="{BG_COLOR}" stroke-width="1.5"/>'
            f'<text x="{cx + CELL_SIZE/2}" y="{cy + CELL_SIZE/2 + 3}" font-family="monospace" font-size="8" font-weight="bold" fill="#000000" text-anchor="middle" opacity="0.6">{name[:3].upper()}</text>'
            f'</g>'
        )

    grid_rows = (len(all_funcs) + COLS - 1) // COLS
    y += grid_rows * (CELL_SIZE + CELL_GAP) + 24

    # Footer
    elements.append(f'<line x1="32" y1="{y}" x2="{WIDTH-32}" y2="{y}" stroke="{BORDER_COLOR}" stroke-width="1"/>')
    y += 18
    elements.append(f'<text x="32" y="{y}" font-family="Segoe UI, sans-serif" font-size="11" fill="{TEXT_DIM}">Last updated: {data["last_updated"]} | Configured via library-progress.json | Run python scripts/library_progress.py</text>')
    elements.append(f'<text x="{WIDTH-32}" y="{y}" font-family="Segoe UI, sans-serif" font-size="11" fill="{COLOR_IMP}" font-weight="bold" text-anchor="end">NFSU2 Android / Rust Native Engine</text>')
    y += 24

    # Build final SVG
    svg_content = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{y}" viewBox="0 0 {WIDTH} {y}" '
        f'role="img" aria-labelledby="title desc">\n'
        f'<title id="title">{escape(data["title"])}</title>\n'
        f'<desc id="desc">{stats["implemented"]} pure Rust, {stats["stubbed"]} hybrid, {stats["missing"]} pending out of {stats["total"]} tracked modules ({stats["percent"]:.1f}% weighted coverage).</desc>\n'
        + "\n".join(elements)
        + "\n</svg>\n"
    )

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print(f"Generated Rust conversion SVG progress visualization: {output_path} ({stats['percent']:.1f}% coverage)")
    return stats

def main():
    parser = argparse.ArgumentParser(description="NFSU2 Rust Conversion Progress & SVG Generator")
    parser.add_argument("--set", nargs=3, metavar=("CRATE", "MODULE", "STATUS"), help="Set module status: implemented, stub, missing")
    parser.add_argument("--desc", type=str, help="Description note when updating a module")
    parser.add_argument("--json", type=str, default=JSON_PATH, help="Path to JSON data file")
    parser.add_argument("--svg", type=str, default=SVG_PATH, help="Path to output SVG file")
    parser.add_argument("--rebuild", action="store_true", help="Rebuild SVG from JSON")
    parser.add_argument("--sync-defaults", action="store_true", help="Sync JSON with DEFAULT_RUST_MODULES")
    args = parser.parse_args()

    data = load_data(args.json)

    if args.sync_defaults or True: # Sync to update with latest Rust modules
        for lib, funcs in DEFAULT_RUST_MODULES.items():
            data["libraries"][lib] = [
                {"name": name, "status": status, "description": desc}
                for name, status, desc in funcs
            ]
        save_data(data, args.json)

    if args.set:
        lib, mod, status = args.set
        if status not in ("implemented", "stub", "missing"):
            print(f"Error: status must be one of: implemented, stub, missing")
            sys.exit(1)
        found = False
        if lib in data["libraries"]:
            for item in data["libraries"][lib]:
                if item["name"].lower() == mod.lower():
                    item["status"] = status
                    if args.desc:
                        item["description"] = args.desc
                    found = True
                    break
        if not found:
            if lib not in data["libraries"]:
                data["libraries"][lib] = []
            data["libraries"][lib].append({
                "name": mod,
                "status": status,
                "description": args.desc or f"{lib} component"
            })
        save_data(data, args.json)

    stats = generate_svg(data, args.svg)
    print(f"\n--- Rust Conversion Summary ---")
    print(f"Total Modules Tracked: {stats['total']}")
    print(f"Pure Rust:             {stats['implemented']} ({stats['implemented']*100.0/stats['total']:.1f}%)")
    print(f"Hybrid / In-Progress:  {stats['stubbed']} ({stats['stubbed']*100.0/stats['total']:.1f}%)")
    print(f"Pending:               {stats['missing']} ({stats['missing']*100.0/stats['total']:.1f}%)")
    print(f"Weighted Coverage:     {stats['percent']:.1f}%\n")

if __name__ == "__main__":
    main()
