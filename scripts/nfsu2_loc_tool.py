#!/usr/bin/env python3
"""NFSU2 & EA Localization (.loc) File Tool.

Allows inspecting, dumping to JSON, and compiling binary .loc files
(LOCH / LOCI / LOCL chunks) used by Need for Speed Underground 2 memcard dialogs.
"""

import argparse
import json
import os
import struct
import sys

DEFAULT_ID_TEXTS = [
    "2.03.09",
    "2.08.04",
    "Ya",
    "Tidak",
    "Lanjutkan",
    "Hard\xa0disk PC",
    "Apakah Anda ingin menimpa permainan yang disimpan sebelumnya?",
    "Penyimpanan selesai.",
    "Tidak dapat menyimpan %1.",
    "Pemuatan selesai.",
    "Tidak dapat memuat %1.",
    "Apakah Anda yakin ingin menghapus %1?",
    "Permainan tersimpan telah dihapus.",
    "Tidak dapat menghapus %1.",
]


def parse_loc(data: bytes):
    """Parses a .loc file into a list of strings."""
    if len(data) < 20 or data[:4] != b'LOCH':
        raise ValueError("Invalid .loc file: missing LOCH header")

    locl_pos = data.find(b'LOCL')
    if locl_pos < 0:
        raise ValueError("Invalid .loc file: missing LOCL chunk")

    magic, size, pad, num = struct.unpack('<4sIII', data[locl_pos:locl_pos + 16])
    offsets = struct.unpack(f'<{num}I', data[locl_pos + 16:locl_pos + 16 + num * 4])

    strings = []
    for off in offsets:
        str_data = data[locl_pos + off:]
        end = 0
        while end + 1 < len(str_data) and not (str_data[end] == 0 and str_data[end + 1] == 0):
            end += 2
        s = str_data[:end].decode('utf-16-le', errors='replace')
        strings.append(s)

    return strings


def build_loc(template_data: bytes, texts: list):
    """Builds a .loc file given template LOCH/LOCI headers and a list of UTF-16 strings."""
    locl_pos = template_data.find(b'LOCL')
    if locl_pos < 0:
        raise ValueError("Template missing LOCL chunk")

    header = template_data[:locl_pos]
    n = len(texts)
    body = bytearray()
    offs = []

    for t in texts:
        offs.append(16 + 4 * n + len(body))
        body += t.encode('utf-16-le') + bytes(4)

    body += bytes(-len(body) % 4)
    locl_chunk = b'LOCL' + struct.pack(f'<III{n}I', 16 + 4 * n + len(body), 0, n, *offs) + body
    return header + locl_chunk


def main():
    parser = argparse.ArgumentParser(description="NFSU2 & EA Localization (.loc) Tool")
    subparsers = parser.add_subparsers(dest="command")

    # dump
    dump_parser = subparsers.add_parser("dump", help="Dump strings from a .loc file")
    dump_parser.add_argument("file", help="Path to .loc file")
    dump_parser.add_argument("-o", "--out", help="Output JSON path (optional)")

    # build
    build_parser = subparsers.add_parser("build", help="Build a .loc file from JSON")
    build_parser.add_argument("json_file", help="Path to input JSON file")
    build_parser.add_argument("-t", "--template", required=True, help="Template .loc file for headers")
    build_parser.add_argument("-o", "--out", required=True, help="Output .loc path")

    # create-indonesian
    id_parser = subparsers.add_parser("create-indonesian", help="Create bahasa_indonesia.loc from template")
    id_parser.add_argument("-t", "--template", required=True, help="Template .loc file (e.g. LOCALE_ENGLISH.loc)")
    id_parser.add_argument("-o", "--out", default="bahasa_indonesia.loc", help="Output .loc path")

    args = parser.parse_args()

    if args.command == "dump":
        with open(args.file, "rb") as f:
            data = f.read()
        strings = parse_loc(data)
        if args.out:
            with open(args.out, "w", encoding="utf-8") as f:
                json.dump(strings, f, indent=2, ensure_ascii=False)
            print(f"Dumped {len(strings)} strings to {args.out}")
        else:
            for i, s in enumerate(strings):
                print(f"[{i:02d}] {s}")

    elif args.command == "build":
        with open(args.json_file, "r", encoding="utf-8") as f:
            texts = json.load(f)
        with open(args.template, "rb") as f:
            template = f.read()
        out_data = build_loc(template, texts)
        with open(args.out, "wb") as f:
            f.write(out_data)
        print(f"Built {args.out} ({len(out_data)} bytes) with {len(texts)} strings")

    elif args.command == "create-indonesian":
        with open(args.template, "rb") as f:
            template = f.read()
        out_data = build_loc(template, DEFAULT_ID_TEXTS)
        with open(args.out, "wb") as f:
            f.write(out_data)
        print(f"Created Bahasa Indonesia locale: {args.out} ({len(out_data)} bytes)")

    else:
        parser.print_help()


if __name__ == "__main__":
    main()
