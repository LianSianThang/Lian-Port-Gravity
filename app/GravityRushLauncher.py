#!/usr/bin/env python3
"""
GRAVITY RUSH REMASTERED — BESPOKE PC LAUNCHER
Original Bande Dessinée / Kat & Dusty Celestial Aesthetics
Designed specifically for Gravity Rush Remastered Native PC Runtime.
"""

import sys
import os
import ctypes
import math
import json
import zlib
import struct
import subprocess
import threading
import time
import shutil
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from player_setup import needs_preparation, prepare_game
from windows_taskbar import apply_identity, APP_ID

# Color Palette: Bande Dessinée Kat & Dusty Cosmic Theme
COLOR_BG_DARK = "#0B0C10"       # Deep celestial black
COLOR_PANEL = "#161925"         # Dark comic frame panel
COLOR_CARD = "#1F2338"          # Inner card background
COLOR_GOLD = "#D4AF37"          # Kat's signature gold trim
COLOR_GOLD_HOVER = "#F5D77F"    # Radiant celestial gold
COLOR_PURPLE = "#6B2D5C"        # Dusty void indigo
COLOR_CYAN = "#45A29E"          # Gravity stasis cyan
COLOR_BLUE = "#2F8CFF"          # Lian Port brand blue
COLOR_BLUE_LIGHT = "#83D7FF"
COLOR_BLUE_DARK = "#112544"
COLOR_TEXT_MAIN = "#F3E5AB"     # Warm antique parchment white
COLOR_TEXT_MUTED = "#8D99AE"    # Comic ink gray
COLOR_BORDER = "#D4AF37"        # Comic line border

BASE_DIR = (Path(sys.executable).resolve().parent / "app" if getattr(sys, "frozen", False)
            else Path(__file__).resolve().parent)
SETTINGS_FILE = BASE_DIR / "launcher_settings.json"
WORKSPACE_DIR = BASE_DIR.parent
DEFAULT_GAME_DIR = WORKSPACE_DIR / "game" / "CUSA01130"
# Native NGS2 and streamed music are verified in the current candidate. Keep
# the earlier voice-tested build available if the staged candidate is missing.
CURRENT_PROBE_EXE = BASE_DIR / "bin" / "gravity-probe-display17.exe"
FALLBACK_PROBE_EXE = BASE_DIR / "bin" / "gravity-probe-audio-boundary11.exe"


def display_runtime_environment(mode, vsync, fps):
    """Translate launcher labels into the runtime's startup overrides."""
    return {
        "SPARTAN_DISPLAY_MODE": {"Windowed": "windowed", "Borderless Fullscreen": "borderless",
                                 "Exclusive Fullscreen": "fullscreen"}.get(mode, "windowed"),
        "SPARTAN_VSYNC": "1" if vsync else "0",
        "SPARTAN_FPS_LIMIT": "0" if fps.startswith("Uncapped") else (
            "120" if fps.startswith("120") else "60"),
    }

PC_CONTROL_ROWS = (
    ("Left stick", "W / A / S / D", "Move / navigate"),
    ("Right stick", "Mouse movement", "Camera / gravity aim"),
    ("R1", "Left Shift", "Float; press again to shift gravity"),
    ("L1", "C", "Cancel gravity / return to normal"),
    ("Cross / X", "Space or Enter", "Jump / speed fall / confirm"),
    ("Square", "Left mouse button", "Kick / gravity kick"),
    ("Circle", "F; Backspace for menus", "Stasis grab / throw; go back"),
    ("Triangle", "Q", "Special power"),
    ("L2 + R2", "Hold Left Ctrl", "Gravity slide"),
    ("R2", "Left Alt / right or middle-click", "Evade: hold WASD and tap"),
    ("L2", "R", "Left trigger alone"),
    ("Options / map menu", "M or Esc", "Open game map / menu; release mouse"),
    ("Touchpad click", "E or Tab", "Contextual touchpad action"),
    ("D-pad", "Arrow keys", "Menu directions"),
    ("L3 / R3", "Z / V", "Click left / right stick"),
    ("Port settings", "Insert", "Lian Port overlay"),
)


def selected_probe_exe():
    """Resolve staged binaries at launch time so a running launcher sees a new build."""
    for candidate in (BASE_DIR / "bin" / "gravity-probe-local.exe", CURRENT_PROBE_EXE,
                      FALLBACK_PROBE_EXE, BASE_DIR / "bin" / "gravity-probe.exe"):
        if candidate.is_file():
            return candidate
    return CURRENT_PROBE_EXE
GUEST_START_EXE = BASE_DIR / "bin" / "lianport-guest-start.exe"
BOOT_BIN = BASE_DIR / "out" / "boot_gravity.bin"
LOGS_DIR = BASE_DIR / "logs"

# Markers that delimit the import counter report in stdout.
COUNTER_START = "=== Import call counters"
COUNTER_END   = "=== End import call counters ==="
MAX_LOGS = 10   # keep only the 10 newest log files


def prune_logs(logs_dir: Path, keep: int = MAX_LOGS) -> None:
    """Delete oldest run_*.log and counters_*.txt files beyond `keep` pairs."""
    runs = sorted(logs_dir.glob("run_*.log"), key=lambda p: p.stat().st_mtime)
    for old in runs[:-keep]:
        old.unlink(missing_ok=True)
        # also remove the matching counters file
        stamp = old.stem[4:]  # strip "run_"
        counter_file = logs_dir / f"counters_{stamp}.txt"
        counter_file.unlink(missing_ok=True)


def decompress_png_to_ppm(png_bytes):
    """Pure-Python PNG to PPM (P6) uncompressed bitmap converter for native Tkinter."""
    pos = 8
    width, height = 0, 0
    idat = bytearray()
    while pos < len(png_bytes):
        chunk_len, chunk_type = struct.unpack('>I4s', png_bytes[pos:pos+8])
        pos += 8
        data = png_bytes[pos:pos+chunk_len]
        pos += chunk_len + 4
        if chunk_type == b'IHDR':
            width, height, bit_depth, color_type = struct.unpack('>IIBB', data[:10])
        elif chunk_type == b'IDAT':
            idat.extend(data)
        elif chunk_type == b'IEND':
            break

    raw = zlib.decompress(bytes(idat))
    bpp = 3 if color_type == 2 else 4
    stride = width * bpp + 1
    rgb = bytearray(width * height * 3)
    prev = bytearray(width * bpp)
    for y in range(height):
        filter_type = raw[y * stride]
        line = bytearray(raw[y * stride + 1 : (y + 1) * stride])
        for x in range(len(line)):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if filter_type == 1: line[x] = (line[x] + a) & 0xff
            elif filter_type == 2: line[x] = (line[x] + b) & 0xff
            elif filter_type == 3: line[x] = (line[x] + (a + b) // 2) & 0xff
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xff
        prev = line
        if bpp == 3:
            rgb[y * width * 3 : (y + 1) * width * 3] = line
        else:
            for x in range(width):
                rgb[(y * width + x) * 3 : (y * width + x) * 3 + 3] = line[x * 4 : x * 4 + 3]
    return f'P6\n{width} {height}\n255\n'.encode('ascii') + bytes(rgb)


def make_lian_port_icon():
    """Build a small blue LP-orbit app icon without external game artwork."""
    size = 48
    pixels = bytearray((8, 14, 30) * (size * size))
    for y in range(size):
        for x in range(size):
            dx, dy = x - 23.5, y - 23.5
            radius = math.sqrt(dx * dx + dy * dy)
            ring = 17.0 <= radius <= 19.3
            orbit = abs(dy + 0.32 * dx + 6) < 1.2 and 5 <= x <= 42
            letter_l = 18 <= x <= 21 and 13 <= y <= 34
            letter_base = 18 <= x <= 33 and 30 <= y <= 34
            if ring or orbit or letter_l or letter_base:
                t = max(0.0, min(1.0, y / (size - 1)))
                color = (int(27 + 82 * t), int(117 + 93 * t), int(255 - 5 * t))
                offset = (y * size + x) * 3
                pixels[offset:offset + 3] = bytes(color)
    ppm = f"P6\n{size} {size}\n255\n".encode("ascii") + pixels
    return tk.PhotoImage(data=ppm, format="PPM")


class GravityRushLauncher(tk.Tk):
    def __init__(self):
        if sys.platform == "win32":
            # Give the launcher its own taskbar identity instead of Python's.
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)
        super().__init__()
        self.title("LIAN PORT — GRAVITY RUSH REMASTERED")
        self.geometry("960x680")
        self.minsize(860, 600)
        self.configure(bg=COLOR_BG_DARK)
        self._lian_port_icon = make_lian_port_icon()
        self.iconphoto(True, self._lian_port_icon)
        icon_path = BASE_DIR / "branding" / "lianport.ico"
        if sys.platform == "win32" and icon_path.is_file():
            self.iconbitmap(default=str(icon_path))
            self.iconbitmap(str(icon_path))

        self.game_process = None
        self._preparing_game = False
        self._log_file = None        # open file handle for current run
        self._counter_file = None    # open file handle for counter report
        self._in_counter_section = False
        self.load_settings()
        self.setup_styles()
        self.create_widgets()
        self.check_prerequisites()
        self.update_idletasks()
        self._taskbar_identity = {}
        if sys.platform == "win32" and icon_path.is_file():
            command = (f'"{sys.executable}"' if getattr(sys, "frozen", False) else
                       f'"{sys.executable}" "{Path(__file__).resolve()}"')
            try:
                self._taskbar_identity = apply_identity(self, icon_path, command)
            except OSError as exc:
                self.append_log(f"[Warning] Taskbar icon setup: {exc}")

    # ------------------------------------------------------------------
    # Settings
    # ------------------------------------------------------------------
    def load_settings(self):
        defaults = {
            "game_path": str(DEFAULT_GAME_DIR),
            "resolution": "1920x1080",
            "display_mode": "Windowed",
            "vsync": True,
            "fps_cap": "60 FPS",
            "fsr_upscaling": True,
            "rcas_sharpening": 70,
            "motion_gyro": True,
            "gyro_sensitivity": 1.0,
            "vibration": True,
            "comic_borders": True,
            "disable_counters": False,
            "disable_font_hle": False,
            "user_path": "user",
        }
        if SETTINGS_FILE.exists():
            try:
                with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                    self.settings = json.load(f)
                    for k, v in defaults.items():
                        self.settings.setdefault(k, v)
            except Exception:
                self.settings = defaults
        else:
            self.settings = defaults

    def save_settings(self):
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(self.settings, f, indent=2)
        except Exception as e:
            print("Failed to save settings:", e)

    def runtime_user_dir(self):
        path = Path(self.settings.get("user_path", "user"))
        return (path if path.is_absolute() else BASE_DIR / path).resolve()

    def save_data_dir(self):
        return self.runtime_user_dir() / "savedata" / "1" / "CUSA01130"

    # ------------------------------------------------------------------
    # Styles
    # ------------------------------------------------------------------
    def setup_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")

        style.configure("TNotebook", background=COLOR_BG_DARK, borderwidth=0)
        style.configure("TNotebook.Tab", background=COLOR_PANEL, foreground=COLOR_TEXT_MAIN,
                        font=("Segoe UI", 10, "bold"), padding=[16, 6], borderwidth=1)
        style.map("TNotebook.Tab",
                  background=[("selected", COLOR_CARD)],
                  foreground=[("selected", COLOR_GOLD)])

        style.configure("TFrame", background=COLOR_BG_DARK)
        style.configure("Card.TFrame", background=COLOR_CARD, relief="ridge")
        style.configure("TLabel", background=COLOR_BG_DARK, foreground=COLOR_TEXT_MAIN, font=("Segoe UI", 10))
        style.configure("Card.TLabel", background=COLOR_CARD, foreground=COLOR_TEXT_MAIN, font=("Segoe UI", 10))
        style.configure("Muted.TLabel", background=COLOR_CARD, foreground=COLOR_TEXT_MUTED, font=("Segoe UI", 9))
        style.configure("Header.TLabel", background=COLOR_BG_DARK, foreground=COLOR_GOLD, font=("Georgia", 16, "bold"))
        style.configure("TCheckbutton", background=COLOR_CARD, foreground=COLOR_TEXT_MAIN, font=("Segoe UI", 10))
        style.configure("TCombobox", fieldbackground=COLOR_PANEL, background=COLOR_CARD,
                        foreground=COLOR_TEXT_MAIN, arrowcolor=COLOR_TEXT_MAIN,
                        selectbackground=COLOR_BLUE_DARK, selectforeground=COLOR_TEXT_MAIN)
        # Clam's readonly state overrides configure() with a light field. Pair
        # its text/background explicitly, including the separate Tk popup list.
        style.map("TCombobox",
                  fieldbackground=[("disabled", COLOR_PANEL), ("readonly", COLOR_PANEL)],
                  foreground=[("disabled", COLOR_TEXT_MUTED), ("readonly", COLOR_TEXT_MAIN)],
                  background=[("active", COLOR_BLUE_DARK), ("readonly", COLOR_CARD)],
                  arrowcolor=[("disabled", COLOR_TEXT_MUTED), ("!disabled", COLOR_TEXT_MAIN)],
                  selectbackground=[("!disabled", COLOR_BLUE_DARK)],
                  selectforeground=[("!disabled", COLOR_TEXT_MAIN)])
        self.option_add("*TCombobox*Listbox.background", COLOR_PANEL)
        self.option_add("*TCombobox*Listbox.foreground", COLOR_TEXT_MAIN)
        self.option_add("*TCombobox*Listbox.selectBackground", COLOR_BLUE)
        self.option_add("*TCombobox*Listbox.selectForeground", "#FFFFFF")
        self.option_add("*TCombobox*Listbox.font", ("Segoe UI", 10))

    # ------------------------------------------------------------------
    # Widgets
    # ------------------------------------------------------------------
    def create_widgets(self):
        header_frame = tk.Frame(self, bg=COLOR_PANEL, height=80, relief="solid", bd=2, highlightbackground=COLOR_BLUE)
        header_frame.pack(fill="x", padx=14, pady=(12, 6))

        logo = tk.Canvas(header_frame, width=52, height=52, bg=COLOR_PANEL, highlightthickness=0)
        logo.pack(side="left", padx=(12, 8), pady=8)
        logo.create_oval(4, 4, 48, 48, outline=COLOR_BLUE, width=2)
        logo.create_arc(8, 8, 44, 44, start=28, extent=285, style="arc", outline=COLOR_BLUE_LIGHT, width=4)
        logo.create_oval(20, 20, 32, 32, fill=COLOR_BLUE, outline=COLOR_BLUE_LIGHT, width=1)
        logo.create_line(26, 8, 26, 17, fill=COLOR_BLUE_LIGHT, width=2)
        logo.create_line(26, 35, 26, 44, fill=COLOR_BLUE_LIGHT, width=2)

        brand = tk.Frame(header_frame, bg=COLOR_PANEL)
        brand.pack(side="left", anchor="w", pady=8)
        tk.Label(brand, text="LIAN PORT", bg=COLOR_PANEL, fg=COLOR_BLUE_LIGHT,
                 font=("Segoe UI", 17, "bold")).pack(anchor="w")
        tk.Label(brand, text="GRAVITY RUSH REMASTERED  •  NATIVE PC RUNTIME",
                 bg=COLOR_PANEL, fg=COLOR_TEXT_MAIN, font=("Segoe UI", 8, "bold")).pack(anchor="w")

        status_badge = tk.Label(header_frame, text="EXPERIMENTAL  |  VULKAN",
                                bg=COLOR_BLUE_DARK, fg=COLOR_BLUE_LIGHT, font=("Segoe UI", 9, "bold"), padx=10, pady=4, relief="ridge")
        status_badge.pack(side="right", padx=16)

        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill="both", expand=True, padx=14, pady=6)

        self.tab_launch   = ttk.Frame(self.notebook)
        self.tab_graphics = ttk.Frame(self.notebook)
        self.tab_controls = ttk.Frame(self.notebook)
        self.tab_saves    = ttk.Frame(self.notebook)
        self.tab_about    = ttk.Frame(self.notebook)

        self.notebook.add(self.tab_launch,   text="  🌌 Shift Gravity  ")
        self.notebook.add(self.tab_graphics, text="  🖥️ Display & Graphics  ")
        self.notebook.add(self.tab_controls, text="  🎮 Motion & Controls  ")
        self.notebook.add(self.tab_saves,    text="  💾 Saves & Storage  ")
        self.notebook.add(self.tab_about,    text="  📜 Architecture  ")

        self.build_launch_tab()
        self.build_graphics_tab()
        self.build_controls_tab()
        self.build_saves_tab()
        self.build_about_tab()

    def build_launch_tab(self):
        pane = tk.Frame(self.tab_launch, bg=COLOR_BG_DARK)
        pane.pack(fill="both", expand=True, padx=8, pady=8)

        # Left Column
        left_col = tk.Frame(pane, bg=COLOR_CARD, width=340, relief="solid", bd=1, highlightbackground=COLOR_GOLD)
        left_col.pack(side="left", fill="y", padx=(0, 8), pady=0)
        left_col.pack_propagate(False)

        icon_path = self.game_dir() / "sce_sys" / "icon0.png"
        self.icon_image = None
        if icon_path.exists():
            try:
                ppm_data = decompress_png_to_ppm(icon_path.read_bytes())
                raw_img = tk.PhotoImage(data=ppm_data, format="PPM")
                self.icon_image = raw_img.subsample(2, 2)
                art_label = tk.Label(left_col, image=self.icon_image, bg=COLOR_CARD)
                art_label.pack(pady=12)
            except Exception as e:
                print("Could not load icon0.png:", e)

        tk.Label(left_col, text="Bluepoint_GravityDazeHD_PS4", bg=COLOR_CARD,
                 fg=COLOR_TEXT_MUTED, font=("Consolas", 9)).pack(pady=(0, 4))

        self.lbl_game_path = tk.Label(left_col, text=str(self.game_dir()), bg=COLOR_CARD,
                                      fg=COLOR_CYAN, font=("Consolas", 8), wraplength=300)
        self.lbl_game_path.pack(fill="x", padx=12, pady=(0, 4))
        tk.Button(left_col, text="Choose Game Folder…", bg=COLOR_PANEL, fg=COLOR_TEXT_MAIN,
                  font=("Segoe UI", 9), command=self.on_choose_game_dir).pack(fill="x", padx=16, pady=4)

        self.btn_launch = tk.Button(
            left_col, text="✦ SHIFT GRAVITY ✦\n[ LAUNCH GAME ]",
            bg=COLOR_GOLD, fg="#0B0C10", activebackground=COLOR_GOLD_HOVER, activeforeground="#000000",
            font=("Georgia", 13, "bold"), bd=3, relief="raised", cursor="hand2",
            padx=16, pady=12, command=self.on_launch_clicked
        )
        self.btn_launch.pack(fill="x", padx=16, pady=14)

        self.btn_stop = tk.Button(
            left_col, text="⏹ Stop Runtime",
            bg="#8B1E3F", fg="#FFFFFF", activebackground="#A8284F",
            font=("Segoe UI", 10, "bold"), bd=2, relief="groove", state="disabled",
            padx=8, pady=4, command=self.on_stop_clicked
        )
        self.btn_stop.pack(fill="x", padx=16, pady=(0, 6))

        self.var_no_counters = tk.BooleanVar(value=self.settings.get("disable_counters", False))
        def on_toggle_no_counters():
            self.settings["disable_counters"] = self.var_no_counters.get()
            self.save_settings()
        self.chk_no_counters = tk.Checkbutton(
            left_col, text="Disable debug counters",
            variable=self.var_no_counters, command=on_toggle_no_counters,
            bg=COLOR_CARD, fg=COLOR_TEXT_MAIN, selectcolor=COLOR_PANEL,
            activebackground=COLOR_CARD, font=("Segoe UI", 9)
        )
        self.chk_no_counters.pack(pady=(0, 4))

        self.var_no_font_hle = tk.BooleanVar(value=self.settings.get("disable_font_hle", False))
        def on_toggle_no_font_hle():
            self.settings["disable_font_hle"] = self.var_no_font_hle.get()
            self.save_settings()
        self.chk_no_font_hle = tk.Checkbutton(
            left_col, text="Disable font handlers",
            variable=self.var_no_font_hle, command=on_toggle_no_font_hle,
            bg=COLOR_CARD, fg=COLOR_TEXT_MAIN, selectcolor=COLOR_PANEL,
            activebackground=COLOR_CARD, font=("Segoe UI", 9)
        )
        self.chk_no_font_hle.pack(pady=(0, 8))

        tk.Label(left_col, text="CPU: Intel Core 5 210H (12 Cores)", bg=COLOR_CARD,
                 fg=COLOR_TEXT_MUTED, font=("Segoe UI", 9)).pack(pady=1)
        tk.Label(left_col, text="GPU: NVIDIA RTX 3050 6GB (Vulkan 1.4)", bg=COLOR_CARD,
                 fg=COLOR_TEXT_MUTED, font=("Segoe UI", 9)).pack(pady=1)
        tk.Label(left_col, text="Imports: 767 found • HLE incomplete", bg=COLOR_CARD,
                 fg="#E6B85C", font=("Segoe UI", 9, "bold")).pack(pady=1)

        # Log file path label (shown after launch)
        self.lbl_log_path = tk.Label(left_col, text="", bg=COLOR_CARD,
                                     fg=COLOR_CYAN, font=("Consolas", 7), wraplength=300)
        self.lbl_log_path.pack(fill="x", padx=8, pady=(4, 0))

        # Right Column: Live Console
        right_col = tk.Frame(pane, bg=COLOR_CARD, relief="solid", bd=1, highlightbackground=COLOR_GOLD)
        right_col.pack(side="right", fill="both", expand=True)

        tk.Label(right_col, text="  RUNTIME DIAGNOSTICS & SYSTEM ACTIVITY",
                 bg=COLOR_CARD, fg=COLOR_GOLD, font=("Segoe UI", 10, "bold"), anchor="w").pack(
                     fill="x", padx=8, pady=(8, 4))

        self.log_text = tk.Text(right_col, bg="#0D0E15", fg="#D8DEE9", font=("Consolas", 9),
                                bd=0, highlightthickness=0, wrap="word")
        self.log_text.pack(fill="both", expand=True, padx=8, pady=(0, 8))

        self.append_log("[Ready] Lian Port — Gravity Rush Remastered launcher.")
        self.append_log("[Info] In-game rendering, voices and streamed music have been tested.")
        self.append_log("[Info] Chapter transitions, sound/video timing and a full playthrough remain under test.")
        self.append_log("[Info] Launch the game and close it normally; logs are saved to logs\\ automatically.")

    def build_graphics_tab(self):
        pane = tk.Frame(self.tab_graphics, bg=COLOR_CARD, relief="solid", bd=1, highlightbackground=COLOR_GOLD)
        pane.pack(fill="both", expand=True, padx=12, pady=12)

        tk.Label(pane, text="DISPLAY & RESOLUTION SETTINGS", bg=COLOR_CARD, fg=COLOR_GOLD,
                 font=("Georgia", 12, "bold")).grid(row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(16, 12))

        tk.Label(pane, text="Target Resolution:", bg=COLOR_CARD, fg=COLOR_TEXT_MAIN,
                 font=("Segoe UI", 10, "bold")).grid(row=1, column=0, sticky="w", padx=16, pady=8)
        self.cmb_res = ttk.Combobox(pane, values=["1920x1080 (FHD Native)", "2560x1440 (QHD 2K)",
                                                    "3840x2160 (UHD 4K)", "1280x720 (HD 720p)"], state="readonly")
        self.cmb_res.set(self.settings.get("resolution", "1920x1080 (FHD Native)"))
        self.cmb_res.grid(row=1, column=1, sticky="w", padx=16, pady=8)

        tk.Label(pane, text="Window Mode:", bg=COLOR_CARD, fg=COLOR_TEXT_MAIN,
                 font=("Segoe UI", 10, "bold")).grid(row=2, column=0, sticky="w", padx=16, pady=8)
        self.cmb_mode = ttk.Combobox(pane, values=["Windowed", "Borderless Fullscreen", "Exclusive Fullscreen"],
                                     state="readonly")
        self.cmb_mode.set(self.settings.get("display_mode", "Windowed"))
        self.cmb_mode.grid(row=2, column=1, sticky="w", padx=16, pady=8)

        self.var_vsync = tk.BooleanVar(value=self.settings.get("vsync", True))
        tk.Checkbutton(pane, text="Enable VSync (Fifo mode - Tear free)", variable=self.var_vsync,
                       bg=COLOR_CARD, fg=COLOR_TEXT_MAIN, selectcolor=COLOR_PANEL,
                       activebackground=COLOR_CARD).grid(row=3, column=0, columnspan=2, sticky="w", padx=16, pady=8)

        tk.Label(pane, text="Presentation FPS limit:", bg=COLOR_CARD, fg=COLOR_TEXT_MAIN,
                 font=("Segoe UI", 10, "bold")).grid(row=4, column=0, sticky="w", padx=16, pady=8)
        self.cmb_fps = ttk.Combobox(pane, values=["60 FPS (Recommended)",
                                                    "120 FPS (High Refresh)", "Uncapped (Maximum)"], state="readonly")
        self.cmb_fps.set(self.settings.get("fps_cap", "60 FPS (Recommended)"))
        self.cmb_fps.grid(row=4, column=1, sticky="w", padx=16, pady=8)

        self.var_fsr = tk.BooleanVar(value=self.settings.get("fsr_upscaling", True))
        tk.Checkbutton(pane, text="Enable AMD FidelityFX Super Resolution (FSR 3.1)", variable=self.var_fsr,
                       bg=COLOR_CARD, fg=COLOR_TEXT_MAIN, selectcolor=COLOR_PANEL,
                       activebackground=COLOR_CARD).grid(row=5, column=0, columnspan=2, sticky="w", padx=16, pady=8)

        tk.Label(pane, text="RCAS Comic Edge Sharpening:", bg=COLOR_CARD, fg=COLOR_TEXT_MAIN,
                 font=("Segoe UI", 10, "bold")).grid(row=6, column=0, sticky="w", padx=16, pady=8)
        self.scale_rcas = tk.Scale(pane, from_=0, to=100, orient="horizontal", bg=COLOR_CARD,
                                   fg=COLOR_TEXT_MAIN, highlightthickness=0, troughcolor=COLOR_PANEL)
        self.scale_rcas.set(self.settings.get("rcas_sharpening", 70))
        self.scale_rcas.grid(row=6, column=1, sticky="ew", padx=16, pady=8)

        tk.Label(pane, text="Window mode, VSync and presentation FPS apply on the next launch.\n"
                           "Exclusive fullscreen falls back to borderless if unavailable.\n"
                           "Resolution, FSR and sharpening controls remain prototypes.",
                 bg=COLOR_CARD, fg=COLOR_TEXT_MUTED, justify="left", font=("Segoe UI", 9, "italic")).grid(
                     row=7, column=0, columnspan=2, sticky="w", padx=16, pady=(8, 12))

    def build_controls_tab(self):
        pane = tk.Frame(self.tab_controls, bg=COLOR_CARD, relief="solid", bd=1, highlightbackground=COLOR_GOLD)
        pane.pack(fill="both", expand=True, padx=12, pady=12)

        pane.columnconfigure(0, weight=1)
        pane.columnconfigure(1, weight=1)
        pane.rowconfigure(5, weight=1)
        tk.Label(pane, text="CONTROLLER RECOMMENDED", bg=COLOR_CARD, fg=COLOR_BLUE_LIGHT,
                 font=("Georgia", 12, "bold")).grid(row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(12, 4))
        tk.Label(pane, text="Use a controller for the game's original controls and button prompts. Keyboard and mouse are an alternative.",
                 bg=COLOR_CARD, fg=COLOR_TEXT_MAIN, font=("Segoe UI", 9), justify="left", wraplength=780).grid(
                     row=1, column=0, columnspan=2, sticky="w", padx=16, pady=(0, 6))

        self.var_gyro = tk.BooleanVar(value=self.settings.get("motion_gyro", True))
        tk.Checkbutton(pane, text="Enable DualShock 4 / DualSense 6-Axis Motion Sensor (Gravity Aiming)",
                       variable=self.var_gyro, bg=COLOR_CARD, fg=COLOR_TEXT_MAIN,
                       selectcolor=COLOR_PANEL, activebackground=COLOR_CARD).grid(
                           row=2, column=0, columnspan=2, sticky="w", padx=16, pady=4)

        tk.Label(pane, text="Tilt & Gyro Sensitivity:", bg=COLOR_CARD, fg=COLOR_TEXT_MAIN,
                 font=("Segoe UI", 10, "bold")).grid(row=3, column=0, sticky="w", padx=16, pady=4)
        self.scale_gyro = tk.Scale(pane, from_=50, to=200, orient="horizontal", bg=COLOR_CARD,
                                   fg=COLOR_TEXT_MAIN, highlightthickness=0, troughcolor=COLOR_PANEL)
        self.scale_gyro.set(int(self.settings.get("gyro_sensitivity", 1.0) * 100))
        self.scale_gyro.grid(row=3, column=1, sticky="ew", padx=16, pady=4)

        self.var_vib = tk.BooleanVar(value=self.settings.get("vibration", True))
        tk.Checkbutton(pane, text="Enable Controller Haptic Vibration", variable=self.var_vib,
                       bg=COLOR_CARD, fg=COLOR_TEXT_MAIN, selectcolor=COLOR_PANEL,
                       activebackground=COLOR_CARD).grid(row=4, column=0, columnspan=2, sticky="w", padx=16, pady=4)

        mapping = tk.Frame(pane, bg=COLOR_CARD)
        mapping.grid(row=5, column=0, columnspan=2, sticky="nsew", padx=16, pady=(8, 4))
        mapping.rowconfigure(0, weight=1)
        mapping.columnconfigure(0, weight=1)
        style = ttk.Style(self)
        style.configure("Controls.Treeview", background="#0E101A", fieldbackground="#0E101A",
                        foreground=COLOR_TEXT_MAIN, rowheight=25, font=("Segoe UI", 9))
        table = ttk.Treeview(mapping, columns=("prompt", "key", "action"), show="headings",
                             height=7, style="Controls.Treeview", selectmode="none")
        for name, title, width in (("prompt", "Game prompt", 130), ("key", "Keyboard / mouse", 250),
                                   ("action", "Action", 350)):
            table.heading(name, text=title)
            table.column(name, width=width, minwidth=width, stretch=name == "action")
        for row in PC_CONTROL_ROWS:
            table.insert("", "end", values=row)
        table.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(mapping, orient="vertical", command=table.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        table.configure(yscrollcommand=scrollbar.set)

        tk.Label(pane, text="Map: M. Evade: hold WASD and tap Left Alt or right/middle-click. Slide: hold Left Ctrl.\n"
                           "Click the game to recapture the mouse. Motion settings apply on the next launch.",
                 bg=COLOR_CARD, fg=COLOR_TEXT_MUTED, font=("Segoe UI", 9), justify="left", wraplength=780).grid(
                     row=6, column=0, columnspan=2, sticky="w", padx=16, pady=(4, 10))

    def build_saves_tab(self):
        pane = tk.Frame(self.tab_saves, bg=COLOR_CARD, relief="solid", bd=1, highlightbackground=COLOR_GOLD)
        pane.pack(fill="both", expand=True, padx=12, pady=12)

        tk.Label(pane, text="SAVE DATA & STORAGE MANAGER", bg=COLOR_CARD, fg=COLOR_GOLD,
                 font=("Georgia", 12, "bold")).pack(anchor="w", padx=16, pady=(16, 8))
        tk.Label(pane, text="Save files are stored natively in the PC runtime user directory.\n"
                             "Title ID: CUSA01130 | Profile: User 1",
                 bg=COLOR_CARD, fg=COLOR_TEXT_MUTED, font=("Segoe UI", 9), justify="left").pack(
                     anchor="w", padx=16, pady=(0, 12))

        user_dir = self.save_data_dir()
        tk.Label(pane, text=f"Location: {user_dir}", bg="#0E101A", fg=COLOR_CYAN,
                 font=("Consolas", 9), padx=8, pady=6, relief="sunken").pack(fill="x", padx=16, pady=6)

        btn_row = tk.Frame(pane, bg=COLOR_CARD)
        btn_row.pack(fill="x", padx=16, pady=12)

        tk.Button(btn_row, text="📂 Open Save Directory", bg=COLOR_PANEL, fg=COLOR_TEXT_MAIN,
                  font=("Segoe UI", 9, "bold"), padx=10, pady=6,
                  command=lambda: os.startfile(user_dir) if user_dir.exists()
                  else messagebox.showinfo("Info", "Directory will be created upon first in-game save.")
                  ).pack(side="left", padx=(0, 8))

        tk.Button(btn_row, text="🛡️ Create Save Backup", bg=COLOR_PANEL, fg=COLOR_GOLD,
                  font=("Segoe UI", 9, "bold"), padx=10, pady=6,
                  command=self.on_backup_clicked).pack(side="left")

    def build_about_tab(self):
        pane = tk.Frame(self.tab_about, bg=COLOR_CARD, relief="solid", bd=1, highlightbackground=COLOR_GOLD)
        pane.pack(fill="both", expand=True, padx=12, pady=12)

        tk.Label(pane, text="GRAVITY RUSH REMASTERED — PC RUNTIME ARCHITECTURE",
                 bg=COLOR_CARD, fg=COLOR_GOLD, font=("Georgia", 12, "bold")).pack(
                     anchor="w", padx=16, pady=(16, 8))

        tk.Label(pane, text=(
            "ARCHITECTURE HIGHLIGHTS:\n\n"
            "• Runtime execution path:\n"
            "  The Lian Port launcher runs a native x86-64 probe for Gravity Rush.\n"
            "  The execution approach builds on SpartanPort; upstream code and license\n"
            "  attribution are retained. Frame presentation is only partially verified.\n\n"
            "• Decoupled Graphics Pipeline:\n"
            "  PS4 GNM command packets are captured and translated to Vulkan 1.4 using\n"
            "  the vendored shadPS4 video core and GCN shader recompiler.\n\n"
            "• Multi-Module Linkage & Synthetic PRX:\n"
            "  Bundled PS4 dynamic libraries (libc.prx, libSceFios2.prx) are linked directly.\n"
            "  A synthetic gravity_stubs.prx module satisfies 153 missing OS/PSN service APIs.\n"
            "  All 2,752 FS: Thread Control Block sites are patched to GS: TEB TLS slots.\n\n"
            "• Dedicated Bande Dessinée PC UX:\n"
            "  100% custom, original user interface built without reusing assets or code\n"
            "  from other game launchers."
        ), bg="#0E101A", fg=COLOR_TEXT_MAIN, font=("Segoe UI", 9), justify="left",
                 padx=16, pady=12, relief="groove").pack(fill="both", expand=True, padx=16, pady=(0, 16))

    # ------------------------------------------------------------------
    # Log helpers
    # ------------------------------------------------------------------
    def append_log(self, text):
        self.log_text.insert("end", text + "\n")
        self.log_text.see("end")

    def _write_log_line(self, line: str) -> None:
        """Write one line to the run log, flushing immediately."""
        if self._log_file:
            try:
                self._log_file.write(line + "\n")
                self._log_file.flush()
            except Exception:
                pass

    def _handle_counter_line(self, line: str) -> None:
        """Route counter-report lines to counters_*.txt as well as the main log."""
        if COUNTER_START in line:
            self._in_counter_section = True
        if self._in_counter_section and self._counter_file:
            try:
                self._counter_file.write(line + "\n")
                self._counter_file.flush()
            except Exception:
                pass
        if COUNTER_END in line:
            self._in_counter_section = False

    def _open_log_files(self, stamp: str) -> None:
        """Create logs directory, open run and counter log files."""
        LOGS_DIR.mkdir(parents=True, exist_ok=True)
        run_path     = LOGS_DIR / f"run_{stamp}.log"
        counter_path = LOGS_DIR / f"counters_{stamp}.txt"
        self._log_file     = open(run_path,     "w", encoding="utf-8", buffering=1)
        self._counter_file = open(counter_path, "w", encoding="utf-8", buffering=1)
        self._log_file.write(f"# Gravity Rush Remastered — run log {stamp}\n")
        self._log_file.write(f"# Probe: {selected_probe_exe()}\n\n")
        self._log_file.flush()
        self._counter_file.write(f"# Import call counters — {stamp}\n\n")
        self._counter_file.flush()
        # Show path in UI
        self.after(0, self.lbl_log_path.config, {"text": f"Log: {run_path.name}"})

    def _close_log_files(self) -> None:
        """Close log files and prune old ones."""
        for fh in (self._log_file, self._counter_file):
            try:
                if fh:
                    fh.close()
            except Exception:
                pass
        self._log_file = self._counter_file = None
        self._in_counter_section = False
        prune_logs(LOGS_DIR, MAX_LOGS)

    # ------------------------------------------------------------------
    # Launch logic
    # ------------------------------------------------------------------
    def check_prerequisites(self):
        for error in self.launch_preflight():
            if error.startswith("Boot image not found:"):
                self.append_log("[Setup] Your game will be prepared automatically on first launch.")
            else:
                self.append_log(f"[Error] {error}")

    def game_dir(self):
        path = Path(self.settings.get("game_path", str(DEFAULT_GAME_DIR))).expanduser()
        return (path if path.is_absolute() else BASE_DIR / path).resolve()

    def launch_preflight(self):
        errors = []
        game_dir = self.game_dir()
        probe_exe = selected_probe_exe()
        if not probe_exe.is_file():
            errors.append(f"Runtime executable not found: {probe_exe}")
        if sys.platform == "win32" and not GUEST_START_EXE.is_file():
            errors.append(f"Runtime startup helper not found: {GUEST_START_EXE}")
        if not BOOT_BIN.is_file():
            errors.append(f"Boot image not found: {BOOT_BIN}")
        if not game_dir.is_dir():
            errors.append(f"Game folder not found: {game_dir}")
        else:
            for relative in ("eboot.bin", "sce_sys/param.sfo", "arc"):
                if not (game_dir / relative).exists():
                    errors.append(f"Game folder is missing {relative}: {game_dir}")
        return errors

    def on_choose_game_dir(self):
        selected = filedialog.askdirectory(initialdir=str(self.game_dir()),
                                           title="Select extracted Gravity Rush folder")
        if selected:
            self.settings["game_path"] = selected
            self.save_settings()
            self.lbl_game_path.config(text=selected)
            self.append_log(f"[Config] Game folder set to {selected}")

    def on_launch_clicked(self):
        if self._preparing_game:
            return
        if self.game_process and self.game_process.poll() is None:
            messagebox.showinfo("Runtime Active", "The game is already running!")
            return

        try:
            prepare_needed = needs_preparation(self.game_dir(), BASE_DIR / 'out')
        except Exception as exc:
            self.append_log(f"[Setup] {exc}")
            messagebox.showerror("Game Setup", str(exc))
            return
        if prepare_needed:
            if not selected_probe_exe().is_file() or not GUEST_START_EXE.is_file():
                messagebox.showerror("Runtime Missing", "Run Setup.ps1 or install the player runtime package first.")
                return
            self._preparing_game = True
            self.btn_launch.config(state="disabled", text="Preparing game…")
            self.append_log("[Setup] Preparing your extracted game. This may take a moment.")
            game = self.game_dir()
            def prepare_worker():
                error = None
                try:
                    prepare_game(game, BASE_DIR / 'out')
                except Exception as exc:
                    error = str(exc)
                self.after(0, preparation_finished, error)
            def preparation_finished(error):
                self._preparing_game = False
                self.btn_launch.config(state="normal", text="✦ SHIFT GRAVITY ✦\n[ LAUNCH GAME ]")
                if error:
                    self.append_log(f"[Setup] Failed: {error}")
                    messagebox.showerror("Preparation Failed", error)
                else:
                    self.append_log("[Setup] Game prepared. Starting runtime…")
                    self.on_launch_clicked()
            threading.Thread(target=prepare_worker, daemon=True).start()
            return

        errors = self.launch_preflight()
        if errors:
            for error in errors:
                self.append_log(f"[Error] {error}")
            messagebox.showerror("Cannot Launch", "Fix the listed files or select the correct game folder.")
            return

        self.btn_launch.config(state="disabled", text="✦ RUNNING ✦")
        self.btn_stop.config(state="normal")
        self.append_log("\n[Launch] Initializing Gravity Rush Remastered...")

        self.settings["motion_gyro"] = bool(self.var_gyro.get())
        self.settings["gyro_sensitivity"] = float(self.scale_gyro.get()) / 100.0
        self.settings["display_mode"] = self.cmb_mode.get()
        self.settings["vsync"] = bool(self.var_vsync.get())
        self.settings["fps_cap"] = self.cmb_fps.get()
        self.save_settings()
        display_env = display_runtime_environment(self.cmb_mode.get(), self.var_vsync.get(), self.cmb_fps.get())
        motion_enabled = "1" if self.var_gyro.get() else "0"
        motion_sensitivity = f"{self.scale_gyro.get() / 100.0:.2f}"

        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._open_log_files(stamp)
        probe_exe = selected_probe_exe()
        probe_line = f"[Runtime] Probe: {probe_exe}"
        self._write_log_line(probe_line)
        self.append_log(probe_line)

        cmd = [str(probe_exe), str(BOOT_BIN), "--app0", str(self.game_dir()),
               "--user", str(self.runtime_user_dir())]
        if sys.platform == "win32" and GUEST_START_EXE.is_file():
            cmd.insert(0, str(GUEST_START_EXE))

        def run_thread():
            try:
                runtime_env = os.environ.copy()
                runtime_env["SPARTAN_FRAME_STATS"] = "1"
                runtime_env["SPARTAN_AUDIO_STATS"] = "1"
                runtime_env["SPARTAN_MOTION_STATS"] = "1"
                runtime_env["SPARTAN_MOTION_GYRO"] = motion_enabled
                runtime_env["SPARTAN_GYRO_SENSITIVITY"] = motion_sensitivity
                runtime_env["SPARTAN_WINDOW_ICON"] = str(BASE_DIR / "branding" / "lianport.bmp")
                runtime_env["LIAN_PC_CONTROLS"] = "1"
                runtime_env.setdefault("LIAN_NGS2_NATIVE", "1")
                runtime_env.setdefault("LIAN_NGS2_TRACE", "0")
                runtime_env["SPARTAN_MOUSE_LOCK"] = "1"
                runtime_env.update(display_env)
                if self.var_no_counters.get():
                    runtime_env["LIAN_NO_COUNTERS"] = "1"
                if self.var_no_font_hle.get():
                    runtime_env["LIAN_NO_FONT_HLE"] = "1"
                self.game_process = subprocess.Popen(
                    cmd,
                    cwd=str(BASE_DIR),
                    env=runtime_env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
                )
                if sys.platform == "win32":
                    threading.Thread(target=self.brand_runtime_window, daemon=True).start()
                for line in self.game_process.stdout:
                    clean = line.rstrip()
                    if clean:
                        self._write_log_line(clean)
                        self._handle_counter_line(clean)
                        self.after(0, self.append_log, clean)
                self.game_process.wait()
                exit_msg = f"[Exit] Process finished with exit code {self.game_process.returncode}"
                self._write_log_line(exit_msg)
                self.after(0, self.append_log, exit_msg)
            except Exception as e:
                err_msg = f"[Error] Failed to start game: {e}"
                self._write_log_line(err_msg)
                self.after(0, self.append_log, err_msg)
            finally:
                self._close_log_files()
                self.after(0, self.on_game_exited)
        threading.Thread(target=run_thread, daemon=True).start()

    def brand_runtime_window(self):
        """Rename the legacy runtime window without changing the verified executable."""
        if sys.platform != "win32":
            return
        try:
            from ctypes import wintypes

            user32 = ctypes.WinDLL("user32", use_last_error=True)
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
            user32.EnumWindows.restype = wintypes.BOOL
            user32.IsWindowVisible.argtypes = [wintypes.HWND]
            user32.IsWindowVisible.restype = wintypes.BOOL
            user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
            user32.GetWindowTextLengthW.restype = ctypes.c_int
            user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            user32.GetWindowTextW.restype = ctypes.c_int
            user32.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
            user32.SetWindowTextW.restype = wintypes.BOOL

            deadline = time.monotonic() + 20.0
            while time.monotonic() < deadline:
                matches = []

                def inspect_window(hwnd, _):
                    if not user32.IsWindowVisible(hwnd):
                        return True
                    length = user32.GetWindowTextLengthW(hwnd)
                    if length <= 0:
                        return True
                    title = ctypes.create_unicode_buffer(length + 1)
                    user32.GetWindowTextW(hwnd, title, len(title))
                    if title.value == "SpartanPort":
                        matches.append(hwnd)
                    return True

                callback = callback_type(inspect_window)
                user32.EnumWindows(callback, 0)
                if matches:
                    for hwnd in matches:
                        user32.SetWindowTextW(hwnd, "Lian Port — Gravity Rush Remastered")
                    message = "[Brand] Runtime window title set to Lian Port."
                    self.after(0, self._write_log_line, message)
                    self.after(0, self.append_log, message)
                    return
                time.sleep(0.25)
        except Exception as error:
            try:
                self.after(0, self.append_log, f"[Brand] Could not update runtime window title: {error}")
            except Exception:
                pass

    def on_stop_clicked(self):
        if self.game_process and self.game_process.poll() is None:
            self.append_log("[User] Terminating game process...")
            self.game_process.terminate()

    def on_game_exited(self):
        self.btn_launch.config(state="normal", text="✦ SHIFT GRAVITY ✦\n[ LAUNCH GAME ]")
        self.btn_stop.config(state="disabled")

    def on_backup_clicked(self):
        user_dir = self.save_data_dir()
        if not user_dir.exists():
            messagebox.showinfo("Backup", "No save data found to backup yet.")
            return
        backup_dir = BASE_DIR / "user" / "backups"
        backup_dir.mkdir(parents=True, exist_ok=True)
        destination = backup_dir / f"CUSA01130-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
        try:
            shutil.copytree(user_dir, destination)
        except OSError as e:
            messagebox.showerror("Backup Failed", f"Could not copy save data: {e}")
            return
        messagebox.showinfo("Backup", f"Save files copied to {destination}")


if __name__ == "__main__":
    app = GravityRushLauncher()
    if "--self-test" in sys.argv:
        # Exercise the bundled Python/Tcl/Tk stack without launching guest code.
        app.withdraw()
        app.update_idletasks()
        report = {"frozen": bool(getattr(sys, "frozen", False)), "app_dir": str(BASE_DIR),
                  "game_dir": str(app.game_dir()), "runtime": str(selected_probe_exe()),
                  "tk_version": app.tk.call("info", "patchlevel"),
                  "symbol_metadata": (Path(__file__).resolve().parent / "data/aerolib.inl").is_file(),
                  "taskbar": app._taskbar_identity,
                  "attached_console": bool(ctypes.windll.kernel32.GetConsoleWindow()) if sys.platform == "win32" else None}
        LOGS_DIR.mkdir(parents=True, exist_ok=True)
        (LOGS_DIR / "launcher_self_test.json").write_text(json.dumps(report, indent=2) + "\n")
        app.destroy()
        if not report["symbol_metadata"]:
            raise SystemExit(1)
    else:
        app.mainloop()
