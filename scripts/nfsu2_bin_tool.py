#!/usr/bin/env python3
"""
NFSU2 Language Binary (.bin) Tool & Indonesian Localizer
Supports:
  - dump <file.bin> <file.json>
  - build <file.json> <base.bin> <out.bin>
  - generate-indo <en.bin> <out.bin>
"""

import sys
import os
import struct
import json
import re

# Comprehensive Indonesian translation mapping for NFSU2 UI, HUD, Menus, Options & Dialogue
TRANSLATIONS = {
    # Main Navigation & Prompts
    "Back": "Kembali",
    "Back ": "Kembali ",
    "Continue": "Lanjutkan",
    "Yes": "Ya",
    "No": "Tidak",
    "OK": "OK",
    "Cancel": "Batal",
    "Accept": "Terima",
    "Select": "Pilih",
    "Delete": "Hapus",
    "Load": "Muat",
    "Save": "Simpan",
    "Done": "Selesai",
    "Help": "Bantuan",
    "Next": "Berikutnya",
    "Previous": "Sebelumnya",
    "None": "Tidak Ada",
    "Default": "Bawaan",
    "Reset": "Atur Ulang",
    "Exit": "Keluar",
    "Quit": "Keluar",
    "Retry": "Coba Lagi",
    "Restart": "Ulangi",
    "Paused": "Jeda",
    "Loading...": "Memuat...",
    "Please Wait": "Harap Tunggu",
    "Press Enter": "Tekan Enter",
    "Press START": "Tekan START",
    "Press Any Key": "Tekan Tombol Apapun",
    "Connecting...": "Menghubungkan...",
    "Error": "Kesalahan",
    "Warning": "Peringatan",
    "Success": "Berhasil",

    # Modes & Menus
    "Career": "Karier",
    "Career Mode": "Mode Karier",
    "Resume Career": "Lanjutkan Karier",
    "Start New Career": "Mulai Karier Baru",
    "Quick Race": "Balapan Cepat",
    "Quick Race Mode Select": "Pilih Mode Balapan Cepat",
    "Free Roam": "Jelajah Bebas",
    "World Map": "Peta Dunia",
    "Garage": "Garasi",
    "Car Lot": "Dealer Mobil",
    "Options": "Pengaturan",
    "Main Menu": "Menu Utama",
    "Single Player": "Pemain Tunggal",
    "Multiplayer": "Multipemain",
    "LAN Play": "Main LAN",
    "Online": "Daring",
    "Customize Car": "Kustomisasi Mobil",
    "Car Select": "Pilih Mobil",
    "Track Select": "Pilih Lintasan",
    "Statistics": "Statistik",
    "Credits": "Kredit",
    "Exit Game": "Keluar Permainan",
    "Quit to Main Menu": "Keluar ke Menu Utama",
    "Quit to Windows": "Keluar dari Permainan",
    "Return to Explore Mode?^All unsaved race progress will be lost.": "Kembali ke Mode Jelajah?^Semua progres balapan yang belum disimpan akan hilang.",
    "Restart Race?": "Ulangi Balapan?",
    "Restart Race": "Ulangi Balapan",

    # Race Types
    "Circuit": "Sirkuit",
    "Sprint": "Sprint",
    "Drag": "Drag",
    "Drift": "Drift",
    "Street X": "Street X",
    "Underground Racing League": "Liga Balap Underground",
    "Outrun": "Balapan Liar",
    "Lap Knockout": "Sistem Gugur Putaran",
    "Free Run": "Lari Bebas",
    "Time Trial": "Uji Waktu",

    # In-Game HUD & Telemetry
    "Lap": "Putaran",
    "Laps": "Putaran",
    "Position": "Posisi",
    "Pos": "Pos",
    "Time": "Waktu",
    "Time:": "Waktu:",
    "Best Lap": "Putaran Terbaik",
    "Best Lap:": "Putaran Terbaik:",
    "Distance": "Jarak",
    "Distance:": "Jarak:",
    "Speed": "Kecepatan",
    "KM/H": "KM/J",
    "MPH": "MPH",
    "RPM": "RPM",
    "Tachometer": "Takometer",
    "Nitrous": "Nitro",
    "N2O": "N2O",
    "Score": "Skor",
    "Points": "Poin",
    "Total Points": "Total Poin",
    "Drift Points": "Poin Drift",
    "Drafting": "Menguntit (Drafting)",
    "Clean Section": "Bagian Bersih",
    "Lead": "Memimpin",
    "Behind": "Tertinggal",
    "Ahead": "Di Depan",
    "Final Lap!": "Putaran Terakhir!",
    "Wrong Way!": "Salah Arah!",
    "Blown Engine!": "Mesin Meledak!",
    "Busted!": "Tertangkap!",
    "Total Time": "Waktu Total",
    "Stage": "Tahap",
    "Stage:": "Tahap:",

    # Options Submenus
    "Gameplay": "Gameplay",
    "Audio": "Audio",
    "Video": "Video",
    "Display": "Tampilan",
    "Controls": "Kontrol",
    "Player Options": "Pengaturan Pemain",
    "Game Options": "Pengaturan Permainan",
    "Audio Options": "Pengaturan Audio",
    "Video Options": "Pengaturan Video",
    "Display Options": "Pengaturan Tampilan",
    "Controller Options": "Pengaturan Pengontrol",
    "Car Options": "Pengaturan Mobil",

    # Video Settings
    "Resolution": "Resolusi",
    "Brightness": "Kecerahan",
    "Contrast": "Kontras",
    "Enhanced Contrast": "Kontras Ditingkatkan",
    "Overbright": "Pencerahan Berlebih",
    "Road Reflection": "Refleksi Jalan",
    "Car Environment Map": "Peta Refleksi Mobil",
    "Motion Blur": "Efek Gerak Kabur",
    "Light Trails": "Jejak Cahaya",
    "Fog": "Kabut",
    "Depth of Field": "Kedalaman Bidang",
    "Anti-Aliasing": "Anti-Aliasing",
    "Car Detail": "Detail Mobil",
    "World Detail": "Detail Dunia",
    "Texture Filtering": "Penyaringan Tekstur",
    "Anisotropic Filtering": "Penyaringan Anisotropik",
    "Shadows": "Bayangan",
    "Headlight Effects": "Efek Lampu Depan",
    "Crowd": "Penonton",
    "Particle Effects": "Efek Partikel",
    "Horizon Distance": "Jarak Horison",

    # Audio Settings
    "Master Volume": "Volume Utama",
    "Music Volume": "Volume Musik",
    "Sound FX Volume": "Volume Efek Suara",
    "Speech Volume": "Volume Suara Bicara",
    "Engine Volume": "Volume Suara Mesin",
    "Ambient Volume": "Volume Lingkungan",
    "Stereo": "Stereo",
    "Mono": "Mono",
    "Surround": "Surround",
    "EA Trax": "Musik EA Trax",

    # Gameplay Settings
    "Transmission": "Transmisi",
    "Automatic": "Otomatis",
    "Manual": "Manual",
    "Speedometer": "Speedometer",
    "Metric": "Metrik",
    "Imperial": "Imperial",
    "Camera": "Kamera",
    "Bumper": "Bumper",
    "Hood": "Kap Mesin",
    "Close": "Dekat",
    "Far": "Jauh",
    "Opponent Difficulty": "Kesulitan Lawan",
    "Easy": "Mudah",
    "Medium": "Sedang",
    "Hard": "Sulit",
    "Traffic Density": "Kepadatan Lalu Lintas",
    "Catch Up": "Kejar Lawan (Catch Up)",

    # Control Settings
    "Steering": "Kemudi",
    "Steer Left": "Belok Kiri",
    "Steer Right": "Belok Kanan",
    "Throttle": "Gas",
    "Accelerate": "Akselerasi",
    "Brakes": "Rem",
    "Handbrake": "Rem Tangan",
    "Reverse": "Mundur",
    "Shift Up": "Oper Gigi Naik",
    "Shift Down": "Oper Gigi Turun",
    "Nitrous Boost": "Dorongan Nitro",
    "Look Back": "Lihat Belakang",
    "Change View": "Ganti Sudut Pandang",
    "Reset Car": "Atur Ulang Mobil",
    "Hydraulics / Bounce": "Hidrolik / Memantul",

    # Car Customization Shops
    "Body Shop": "Bengkel Bodi",
    "Performance Shop": "Bengkel Performa",
    "Graphics Shop": "Bengkel Grafis",
    "Car Specialties Shop": "Bengkel Spesialis",
    "Parts": "Suku Cadang",
    "Body Kits": "Kit Bodi",
    "Front Bumper": "Bumper Depan",
    "Rear Bumper": "Bumper Belakang",
    "Side Skirts": "Rok Samping",
    "Spoiler": "Spoiler",
    "Roof Scoop": "Ventilasi Atap",
    "Headlights": "Lampu Depan",
    "Taillights": "Lampu Belakang",
    "Side Mirrors": "Spion Samping",
    "Exhaust Tip": "Ujung Knalpot",
    "Rims": "Velg",
    "Carbon Fiber": "Serat Karbon",
    "Carbon Hood": "Kap Mesin Karbon",
    "Carbon Trunk": "Bagasi Karbon",
    "Carbon Spoiler": "Spoiler Karbon",
    "Custom Gauges": "Pengukur Kustom",
    "Neon": "Lampu Neon",
    "Window Tint": "Kaca Film",
    "Lights": "Lampu",
    "Nitrous Purge": "Purge Nitro",
    "Hydraulics": "Hidrolik",
    "Trunk Audio": "Audio Bagasi",
    "Spinners": "Spinner Velg",
    "Split Hoods": "Kap Terbelah",
    "Doors": "Pintu",
    "Scissor Doors": "Pintu Gunting",
    "Paint": "Cat",
    "Base Color": "Warna Dasar",
    "Vinyls": "Stiker Vinyl",
    "Decals": "Dekal",
    "Sponsor Decals": "Dekal Sponsor",
    "Numbers": "Nomor",

    # Performance Upgrades & Dyno
    "Performance Tuning / Dyno": "Penyetelan Performa / Dyno",
    "Dyno": "Dyno",
    "Engine": "Mesin",
    "ECU": "ECU & Pengapian",
    "Suspension": "Suspensi",
    "Tires": "Ban",
    "Turbo": "Turbo",
    "Weight Reduction": "Pengurangan Bobot",
    "Horsepower": "Tenaga Kuda (HP)",
    "Torque": "Torsi",
    "Top Speed": "Kecepatan Puncak",
    "Acceleration": "Akselerasi",
    "Handling": "Pengendalian",
    "Gear Ratios": "Rasio Gigi",
    "Aerodynamics": "Aerodinamika",
    "Brake Bias": "Keseimbangan Rem",

    # Colors
    "RED": "MERAH",
    "BLUE": "BIRU",
    "GREEN": "HIJAU",
    "YELLOW": "KUNING",
    "ORANGE": "JINGGA",
    "PURPLE": "UNGU",
    "WHITE": "PUTIH",
    "BLACK": "HITAM",
    "GREY": "ABU-ABU",
    "GOLD": "EMAS",
    "SILVER": "PERAK",
    "BRONZE": "PERUNGGU",
    "LIME": "HIJAU MUDA",
    "CYAN": "BIRU MUDA",
    "MAGENTA": "MAGENTA",

    # Confirmations & Save/Load
    "Save Game": "Simpan Permainan",
    "Load Game": "Muat Permainan",
    "Delete Game": "Hapus Permainan",
    "Autosave": "Simpan Otomatis",
    "Save Successful": "Penyimpanan Berhasil",
    "Load Successful": "Pemuatan Berhasil",
    "Are you sure you want to delete this profile?": "Apakah Anda yakin ingin menghapus profil ini?",
    "Are you sure you want to exit?": "Apakah Anda yakin ingin keluar?",
    "Are you sure you want to quit?": "Apakah Anda yakin ingin berhenti?",
    "All unsaved progress will be lost.": "Semua progres yang belum disimpan akan hilang.",
    "Would you like to purchase this graphics setup for your car?": "Apakah Anda ingin membeli pengaturan grafis ini untuk mobil Anda?",
    "Would you like to purchase this performance setup for your car?": "Apakah Anda ingin membeli pengaturan performa ini untuk mobil Anda?",
    "Do you want to enter the car lot?": "Apakah Anda ingin masuk ke dealer mobil?",
    "Incoming Mobile Message": "Pesan Masuk",
    "SMS": "SMS",
    "Messages": "Pesan",
    "Bank": "Saldo",
    "Cash": "Uang Tunai",
    "Cost": "Biaya",
    "Price": "Harga",
    "Free": "Gratis",
    "Install": "Pasang",
    "Uninstall": "Lepas",
    "Trade In": "Tukar Tambah",
    "New Car Unlocked!": "Mobil Baru Terbuka!",
    "New Part Unlocked!": "Suku Cadang Baru Terbuka!",
    "Magazine Cover": "Sampul Majalah",
    "DVD Cover": "Sampul DVD",
    "Photo Shoot": "Sesi Foto",
    "Sponsor": "Sponsor",
    "Contract": "Kontrak",
    "Superb Drift!": "Drift Hebat!",
    "Colossal Drift!": "Drift Luar Biasa!",
    "Outrageous Drift!": "Drift Mengagumkan!",
    "Insane Drift!": "Drift Gila!",
}

# Substring replacements for compound action prompts (preserving $JOY_...$ tags)
ACTION_REPLACEMENTS = [
    ("Change Selection", "Ubah Pilihan"),
    ("Select Highlighted Option", "Pilih Opsi yang Disorot"),
    ("Select Highlighted Track", "Pilih Lintasan yang Disorot"),
    ("Back to Previous Menu", "Kembali ke Menu Sebelumnya"),
    ("Rotate Car", "Putar Mobil"),
    ("Switch Cars", "Ganti Mobil"),
    ("Trade for Car", "Tukar Mobil"),
    ("Move Highlight Up / Down", "Gerakkan Sorotan Atas / Bawah"),
    ("Log-off Online Play", "Keluar dari Main Daring"),
    ("Back", "Kembali"),
    ("Continue", "Lanjutkan"),
    ("Select", "Pilih"),
    ("Accept", "Terima"),
    ("Cancel", "Batal"),
    ("Zoom In / Out", "Perbesar / Perkecil"),
    ("Open / Close", "Buka / Tutup"),
    ("Test Dyno", "Uji Dyno"),
    ("Purchase", "Beli"),
    ("Install Part", "Pasang Suku Cadang"),
    ("Uninstall Part", "Lepas Suku Cadang"),
    ("Toggle Fullscreen", "Beralih Layar Penuh"),
    ("View Car Details", "Lihat Detail Mobil"),
    ("Change Category", "Ubah Kategori"),
]

def parse_bin(bin_bytes):
    cid, csz = struct.unpack('<II', bin_bytes[:8])
    if cid != 0x39000:
        raise ValueError(f"Not a valid NFSU2 language bin chunk: 0x{cid:08X}")
    payload = bin_bytes[8:8+csz]
    h0, num_entries, h2, h3 = struct.unpack('<IIII', payload[:16])
    table_bytes = payload[h2:h2 + num_entries * 8]
    strings_bytes = payload[h3:]
    
    entries = []
    for i in range(num_entries):
        k, off = struct.unpack('<II', table_bytes[i*8 : (i+1)*8])
        raw = strings_bytes[off:].split(b'\x00')[0]
        text = raw.decode('latin1', 'replace')
        entries.append((k, text))
    
    trailing = bin_bytes[8+csz:]
    return h0, h2, entries, trailing

def pack_bin(entries, trailing_bytes, table_start_rel=6164):
    # Sort strictly by uint32 key for binary search lookup in SPEED2.EXE
    entries = sorted(entries, key=lambda x: x[0])
    num_entries = len(entries)
    h0 = 16
    
    strings_data = bytearray()
    table_entries = []
    for k, text in entries:
        raw = text.encode('latin1', 'replace') + b'\x00'
        off = len(strings_data)
        strings_data.extend(raw)
        table_entries.append((k, off))
    
    table_bytes = bytearray()
    for k, off in table_entries:
        table_bytes.extend(struct.pack('<II', k, off))
    
    strings_start_rel = table_start_rel + len(table_bytes)
    while (strings_start_rel % 16) != 0:
        strings_start_rel += 1
    
    payload = bytearray()
    payload.extend(struct.pack('<IIII', h0, num_entries, table_start_rel, strings_start_rel))
    payload.extend(b'\x00' * (table_start_rel - 16))
    payload.extend(table_bytes)
    payload.extend(b'\x00' * (strings_start_rel - (table_start_rel + len(table_bytes))))
    payload.extend(strings_data)
    while (len(payload) % 16) != 0:
        payload.append(0)
    
    chunk_hdr = struct.pack('<II', 0x39000, len(payload))
    return bytes(chunk_hdr + payload + trailing_bytes)

def translate_text(text):
    if not text:
        return text
    # 1. Exact match
    if text in TRANSLATIONS:
        return TRANSLATIONS[text]
    
    # 2. Case insensitive exact match
    for k, v in TRANSLATIONS.items():
        if text.strip().lower() == k.lower():
            return v
    
    # 3. Compound action prompts (contains ^ or $JOY_...)
    res = text
    for act_en, act_id in ACTION_REPLACEMENTS:
        if act_en in res:
            res = res.replace(act_en, act_id)
    
    return res

def generate_indonesian_bin(en_bin_path, out_bin_path):
    with open(en_bin_path, 'rb') as f:
        en_bytes = f.read()
    
    h0, h2, entries, trailing = parse_bin(en_bytes)
    print(f"[bin_tool] Read {len(entries)} entries from {en_bin_path}")
    
    translated_entries = []
    translated_count = 0
    for k, text in entries:
        new_text = translate_text(text)
        if new_text != text:
            translated_count += 1
        translated_entries.append((k, new_text))
    
    print(f"[bin_tool] Translated {translated_count} / {len(entries)} entries to Bahasa Indonesia")
    
    out_bytes = pack_bin(translated_entries, trailing, h2)
    with open(out_bin_path, 'wb') as f:
        f.write(out_bytes)
    
    print(f"[bin_tool] Wrote {len(out_bytes)} bytes to {out_bin_path}")
    
    # Self-validation
    _, _, roundtrip, _ = parse_bin(out_bytes)
    assert len(roundtrip) == len(entries), "Entry count mismatch!"
    prev = -1
    for k, _ in roundtrip:
        assert k > prev, f"Binary search sort violation at key 0x{k:08X}"
        prev = k
    print("[bin_tool] Verification: 100% compliant with NFSU2 binary search table requirements!")

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  dump <file.bin> <file.json>")
        print("  build <file.json> <base.bin> <out.bin>")
        print("  translate-indo <en.bin> <out.bin>")
        return 1
    
    cmd = sys.argv[1]
    if cmd == "translate-indo":
        if len(sys.argv) < 4:
            print("Usage: translate-indo <en.bin> <out.bin>")
            return 1
        generate_indonesian_bin(sys.argv[2], sys.argv[3])
    elif cmd == "dump":
        with open(sys.argv[2], 'rb') as f:
            _, _, entries, _ = parse_bin(f.read())
        data = [{'key': f'0x{k:08X}', 'id': k, 'text': t} for k, t in entries]
        with open(sys.argv[3], 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print(f"Dumped {len(data)} entries to {sys.argv[3]}")
    elif cmd == "build":
        with open(sys.argv[2], 'r', encoding='utf-8') as f:
            data = json.load(f)
        entries = [(e['id'], e['text']) for e in data]
        with open(sys.argv[3], 'rb') as f:
            h0, h2, _, trailing = parse_bin(f.read())
        out_bytes = pack_bin(entries, trailing, h2)
        with open(sys.argv[4], 'wb') as f:
            f.write(out_bytes)
        print(f"Built {len(out_bytes)} bytes to {sys.argv[4]}")
    else:
        print(f"Unknown command: {cmd}")
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
