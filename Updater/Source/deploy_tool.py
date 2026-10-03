#!/usr/bin/env python3
"""
AutoChanger Deployment Tool
----------------------------
Updates an AutoChanger unit running v3.9 (or any version on the same partition
scheme) up to a newer firmware version, WITHOUT touching the device's saved
patterns or settings.

How it stays safe: this writes only to the two app slots, "app0" (0x10000) and
"app1" (0x150000) - the same regions the Arduino IDE / OTA updates write to. It
never touches the bootloader, the partition table, the OTA boot-selector
("otadata"), or the SPIFFS filesystem - so anything stored on the device
(patterns.txt, config.ini, etc) survives untouched.

The same image goes into BOTH slots on purpose. On a unit that has never had an
OTA update the bootloader always runs app0, but once OTA has been used "otadata"
may point at app1 instead, and we don't read otadata to find out. Writing both
means the unit boots the new firmware whichever slot is active. The catch: the
inactive slot normally holds the previous firmware as a fallback, and this
overwrites it.

This assumes the target partition scheme is:
    "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
i.e. app0 lives at 0x10000, app1 at 0x150000, each 0x140000 (1,310,720) bytes.
The tool does NOT currently read the unit's partition table to confirm this: it
only refuses a firmware file too large for the slot. If a unit has ever been
re-flashed with a different partition scheme, this tool is NOT appropriate for it
without adjusting APP0_OFFSET/APP1_OFFSET/APP0_MAX_SIZE below.
"""

import os
import re
import sys
import io
import time
import base64
import hashlib
import platform
import tempfile
import datetime
import contextlib
import threading
import queue

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

try:
    import esptool
except ImportError:
    esptool = None

# --- Partition constants for "Default 4MB with spiffs" -----------------------
APP0_OFFSET = 0x10000
APP1_OFFSET = 0x150000
APP0_MAX_SIZE = 0x140000  # 1,310,720 bytes - do not exceed, or the write
                           # would spill into whatever comes after this slot.
# We write the same image to BOTH ota_0 (app0) and ota_1 (app1) slots. Only
# writing ota_0 would be enough for a factory-fresh, never-OTA'd unit (the
# bootloader defaults to it) - but if a device has ever received an update via
# the web /update page, "otadata" may now point at ota_1 instead, and writing
# only ota_0 would silently do nothing observable (it'd keep booting the old
# firmware from the other slot). Writing both sidesteps needing to inspect
# otadata to know which one is actually active.
DEFAULT_CHIP = "esp32s3"
DEFAULT_BAUD = "921600"

# The other files an Arduino export produces next to the real app image - never
# the thing to offer in the main firmware dropdown.
COMPANION_SUFFIXES = (".bootloader.bin", ".partitions.bin", ".merged.bin")

# --- First-time flash (brand new / blank chip) --------------------------------
# A never-flashed chip has no bootloader or partition table at all, so writing
# just the app (as the normal Update does) produces nothing bootable. These are
# the extra pieces a full flash needs, and are ESP32-S3-SPECIFIC offsets (see
# arduino-esp32's boards.txt: esp32s3.build.bootloader_addr=0x0 - classic ESP32
# and S2 use 0x1000 instead, so this tool would need updating for those chips).
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xe000    # initializes "otadata" to boot ota_0 (app0) first

# boot_app0.bin itself isn't sketch-specific - Arduino's Export Compiled Binary
# doesn't produce a copy of it, because it's one fixed file bundled with the
# ESP32 core install (same bytes for every sketch). Embedded here so a
# first-time flash doesn't require hunting for it in the Arduino installation.
# Source: https://github.com/espressif/arduino-esp32/blob/master/tools/partitions/boot_app0.bin
# SHA-256 of the decoded bytes: f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0
BOOT_APP0_B64 = """
AQAAAP///////////////////////////////5qYQ0f/////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////wAAAAD/////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////8=
"""

# The device's own Serial.begin() rate (command interpreter), separate from
# esptool's flashing baud above.
DEVICE_SERIAL_BAUD = 115200
CMD_TIMEOUT = 5.0    # seconds to wait for a simple command (RM) to report back
FIX_TIMEOUT = 15.0   # FIX rewrites several files, so give it longer

# File groups for the optional post-flash cleanup step, sent as RM <filename>
# commands over serial once the device has rebooted on the new firmware.
FILE_GROUPS = {
    "html_svg": [
        "index.html", "autochanger.svg", "manage.html",
        "ok.html", "edit.html", "failed.html",
    ],
    "config": ["config.ini"],
    "patterns": ["patterns.txt"],
}


def get_app_dir():
    """Directory the running tool lives in - where we look for .bin files
    sitting alongside it. Different depending on whether this is a frozen
    PyInstaller exe (sys.executable is the exe itself) or run from source
    (use the script's own location instead)."""
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))


class _QueueWriter(io.TextIOBase):
    """A file-like object that forwards each completed line to a callback.
    Used to capture esptool's printed output (via redirect_stdout) into the
    GUI's log pane, line by line, as it's produced."""

    def __init__(self, callback):
        self.callback = callback
        self._buffer = ""

    _ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

    def write(self, s):
        self._buffer += s
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            # Keep the saved log readable: drop any terminal colour/cursor codes,
            # and where a progress display redraws a line with carriage returns,
            # keep only its final state rather than every intermediate frame.
            line = self._ANSI_RE.sub("", line).rstrip("\r")
            if "\r" in line:
                line = line.rsplit("\r", 1)[-1]
            self.callback(line)
        return len(s)

    def flush(self):
        pass


class DeployApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("AutoChanger Deployment Tool")
        self.geometry("640x800")
        self.resizable(False, True)
        self.minsize(640, 720)

        self.firmware_path = tk.StringVar()
        self.firmware_combo_label = tk.StringVar()
        self._firmware_files = {}  # display filename -> full path
        self.selected_port = tk.StringVar()

        self.clear_html_svg = tk.BooleanVar(value=True)
        self.clear_config = tk.BooleanVar(value=False)
        self.clear_patterns = tk.BooleanVar(value=False)

        self.first_time_flash = tk.BooleanVar(value=False)
        self.bootloader_path = tk.StringVar()
        self.partitions_path = tk.StringVar()

        self.status_queue = queue.Queue()
        self._log_history = []      # (HH:MM:SS, message) for every line, for the saved log file
        self._log_saved_upto = 0    # history index already written to a log file
        self._run_info = {}         # details of the current update, for the log header

        self._build_ui()
        self._scan_firmware_dir()
        self._refresh_ports()
        self.after(200, self._poll_queue)

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        pad = {"padx": 12, "pady": 8}

        header = tk.Label(
            self,
            text="AutoChanger Deployment Tool",
            font=("Helvetica", 16, "bold"),
        )
        header.pack(anchor="w", **pad)

        subheader = tk.Label(
            self,
            text=(
                "Updates the firmware on a connected AutoChanger unit.\n"
                "Your saved patterns and settings are not touched."
            ),
            justify="left",
            fg="#444",
        )
        subheader.pack(anchor="w", padx=12)

        # --- Firmware file selection ---
        fw_frame = tk.LabelFrame(self, text="1. Choose firmware file", padx=10, pady=10)
        fw_frame.pack(fill="x", **pad)

        fw_row = tk.Frame(fw_frame)
        fw_row.pack(fill="x")
        self.firmware_combo = ttk.Combobox(
            fw_row, textvariable=self.firmware_combo_label, state="readonly", width=45
        )
        self.firmware_combo.pack(side="left", fill="x", expand=True)
        self.firmware_combo.bind("<<ComboboxSelected>>", self._on_firmware_combo_selected)
        tk.Button(fw_row, text="Refresh", command=self._scan_firmware_dir).pack(
            side="left", padx=(8, 0)
        )
        tk.Button(fw_row, text="Browse...", command=self._pick_firmware).pack(
            side="left", padx=(8, 0)
        )
        tk.Label(
            fw_frame,
            text=(
                "Shows any .bin files found next to this tool - or use Browse to pick "
                "one from elsewhere. This is the file from Arduino IDE's Sketch > "
                "Export Compiled Binary."
            ),
            fg="#666",
            justify="left",
            wraplength=580,
        ).pack(anchor="w", pady=(6, 0))

        tk.Checkbutton(
            fw_frame, text="This is a first-time flash on a brand new/blank device",
            variable=self.first_time_flash, command=self._on_first_time_flash_toggled,
        ).pack(anchor="w", pady=(10, 0))

        self.first_time_frame = tk.Frame(fw_frame)
        # not packed yet - _on_first_time_flash_toggled() shows/hides it

        bl_row = tk.Frame(self.first_time_frame)
        bl_row.pack(fill="x", pady=(4, 0))
        tk.Label(bl_row, text="Bootloader:", width=11, anchor="w").pack(side="left")
        tk.Entry(bl_row, textvariable=self.bootloader_path, state="readonly").pack(
            side="left", fill="x", expand=True
        )
        tk.Button(bl_row, text="Browse...",
                 command=lambda: self._pick_companion_file(self.bootloader_path, "bootloader")
                 ).pack(side="left", padx=(8, 0))

        part_row = tk.Frame(self.first_time_frame)
        part_row.pack(fill="x", pady=(4, 0))
        tk.Label(part_row, text="Partitions:", width=11, anchor="w").pack(side="left")
        tk.Entry(part_row, textvariable=self.partitions_path, state="readonly").pack(
            side="left", fill="x", expand=True
        )
        tk.Button(part_row, text="Browse...",
                 command=lambda: self._pick_companion_file(self.partitions_path, "partition table")
                 ).pack(side="left", padx=(8, 0))

        tk.Label(
            self.first_time_frame,
            text=(
                "A blank chip has no bootloader or partition table yet, so this also "
                "writes those two (plus a small fixed file that selects the first boot "
                "slot) alongside the firmware itself. Not needed for a normal update - "
                "only for a chip that's never been flashed before. Auto-filled when found "
                "next to the firmware file (Arduino names them "
                "<sketch>.ino.bootloader.bin / .ino.partitions.bin); Browse otherwise."
            ),
            fg="#666",
            justify="left",
            wraplength=580,
        ).pack(anchor="w", pady=(6, 0))

        # --- Port selection ---
        port_frame = tk.LabelFrame(self, text="2. Choose the device", padx=10, pady=10)
        port_frame.pack(fill="x", **pad)

        port_row = tk.Frame(port_frame)
        port_row.pack(fill="x")
        self.port_combo = ttk.Combobox(
            port_row, textvariable=self.selected_port, state="readonly", width=50
        )
        self.port_combo.pack(side="left", fill="x", expand=True)
        tk.Button(port_row, text="Refresh", command=self._refresh_ports).pack(
            side="left", padx=(8, 0)
        )
        tk.Label(
            port_frame,
            text="Plug the AutoChanger in over USB, then click Refresh if it's not listed.",
            fg="#666",
            justify="left",
            wraplength=580,
        ).pack(anchor="w", pady=(6, 0))

        # --- Clear files on update ---
        clear_frame = tk.LabelFrame(
            self, text="3. Clear files on update (optional)", padx=10, pady=10
        )
        clear_frame.pack(fill="x", **pad)

        tk.Checkbutton(
            clear_frame, text="HTML & SVG files (web pages)", variable=self.clear_html_svg
        ).pack(anchor="w")
        tk.Checkbutton(
            clear_frame, text="Config (WiFi, admin login, etc)", variable=self.clear_config
        ).pack(anchor="w")
        tk.Checkbutton(
            clear_frame, text="Patterns", variable=self.clear_patterns
        ).pack(anchor="w")
        tk.Label(
            clear_frame,
            text=(
                "Deletes the checked files once the unit restarts on the new firmware, "
                "then has it recreate fresh defaults. Web pages and patterns come back "
                "straight away; Config resets the next time the unit restarts. HTML & "
                "SVG is ticked by default so pages refresh with each version - Config "
                "and Patterns are your own data, so they're left alone unless you choose."
            ),
            fg="#666",
            justify="left",
            wraplength=580,
        ).pack(anchor="w", pady=(6, 0))

        # --- Action ---
        action_frame = tk.Frame(self)
        action_frame.pack(fill="x", **pad)
        self.update_btn = tk.Button(
            action_frame,
            text="Update Device",
            font=("Helvetica", 12, "bold"),
            bg="#2e7031",
            fg="white",
            height=2,
            command=self._start_update,
        )
        self.update_btn.pack(fill="x")

        self.progress = ttk.Progressbar(self, mode="indeterminate")
        self.progress.pack(fill="x", padx=12)

        # --- Log output ---
        log_frame = tk.LabelFrame(self, text="Details (a copy is saved as a .log file after each update)", padx=6, pady=6)
        log_frame.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        self.log_text = tk.Text(log_frame, height=8, state="disabled", wrap="word")
        self.log_text.pack(fill="both", expand=True)

    # --------------------------------------------------------------- helpers
    def _log(self, message):
        # One entry per line so multi-line messages (e.g. esptool errors) each get
        # their own timestamp in the saved log and display cleanly in the pane.
        lines = message.rstrip().split("\n") if message.strip() else [""]
        stamp = time.strftime("%H:%M:%S")
        for line in lines:
            self._log_history.append((stamp, line))
            self.status_queue.put(line)

    def _poll_queue(self):
        try:
            while True:
                message = self.status_queue.get_nowait()
                self.log_text.configure(state="normal")
                self.log_text.insert("end", message + "\n")
                self.log_text.see("end")
                self.log_text.configure(state="disabled")
        except queue.Empty:
            pass
        self.after(200, self._poll_queue)

    def _pick_firmware(self):
        path = filedialog.askopenfilename(
            title="Select firmware .bin file",
            filetypes=[("Firmware binary", "*.bin"), ("All files", "*.*")],
        )
        if path:
            label = os.path.basename(path)
            self._firmware_files[label] = path
            self.firmware_combo["values"] = list(self._firmware_files.keys())
            self.firmware_combo_label.set(label)
            self.firmware_path.set(path)
            self._auto_detect_companions()

    def _scan_firmware_dir(self):
        app_dir = get_app_dir()
        self._firmware_files = {}
        try:
            for name in sorted(os.listdir(app_dir)):
                low = name.lower()
                if low.endswith(".bin") and not low.endswith(COMPANION_SUFFIXES):
                    self._firmware_files[name] = os.path.join(app_dir, name)
        except OSError as exc:
            self._log(f"Could not scan {app_dir}: {exc}")

        labels = list(self._firmware_files.keys())
        self.firmware_combo["values"] = labels
        if labels:
            newest = labels[-1]  # sorted ascending, so the last one is the newest version
            self.firmware_combo.set(newest)
            self.firmware_combo_label.set(newest)
            self.firmware_path.set(self._firmware_files[newest])
            self._log(f"Found {len(labels)} .bin file(s) in {app_dir} - defaulting to {newest}")
        else:
            self.firmware_combo_label.set("")
            self.firmware_path.set("")
            self._log(f"No .bin files found in {app_dir} - use Browse to pick one.")
        self._auto_detect_companions()

    def _on_firmware_combo_selected(self, event=None):
        label = self.firmware_combo_label.get()
        path = self._firmware_files.get(label)
        if path:
            self.firmware_path.set(path)
            self._auto_detect_companions()

    def _on_first_time_flash_toggled(self):
        if self.first_time_flash.get():
            self.first_time_frame.pack(fill="x", pady=(4, 0))
            self._auto_detect_companions()
        else:
            self.first_time_frame.pack_forget()

    def _auto_detect_companions(self):
        """Arduino names these <sketch>.ino.bootloader.bin / .ino.partitions.bin,
        sharing everything up to '.bin' with the main firmware file. Only fills in
        a field if it's currently empty, so it won't stomp a manual Browse pick."""
        if not self.first_time_flash.get():
            return
        firmware = self.firmware_path.get()
        if not firmware or not firmware.lower().endswith(".bin"):
            return
        base = firmware[:-len(".bin")]
        for suffix, var in ((".bootloader.bin", self.bootloader_path),
                            (".partitions.bin", self.partitions_path)):
            if var.get():
                continue
            candidate = base + suffix
            if os.path.isfile(candidate):
                var.set(candidate)
                self._log(f"Found {os.path.basename(candidate)} next to the firmware file.")

    def _pick_companion_file(self, target_var, label):
        path = filedialog.askopenfilename(
            title=f"Select {label} .bin file",
            filetypes=[("Firmware binary", "*.bin"), ("All files", "*.*")],
        )
        if path:
            target_var.set(path)

    def _refresh_ports(self):
        if list_ports is None:
            self._log("Could not load pyserial - port list unavailable.")
            return

        labels = [f"{p.device} - {p.description}" for p in list_ports.comports()]
        previous = self.selected_port.get()
        self.port_combo["values"] = labels
        if previous in labels:
            pass  # keep whatever they'd already chosen
        elif labels:
            self.port_combo.current(0)
        else:
            self.selected_port.set("")
            self._log("No serial ports found - plug the AutoChanger in and click Refresh.")

    def _get_selected_device(self):
        label = self.selected_port.get()
        if not label:
            return None
        return label.split(" - ")[0]

    # ---------------------------------------------------------------- action
    def _start_update(self):
        firmware = self.firmware_path.get()
        device = self._get_selected_device()

        if not firmware:
            messagebox.showerror("No firmware selected", "Choose a firmware .bin file first.")
            return
        if not os.path.isfile(firmware):
            messagebox.showerror("File not found", f"Could not find:\n{firmware}")
            return
        if not device:
            messagebox.showerror("No device selected", "Choose the device's serial port first.")
            return

        size = os.path.getsize(firmware)
        if size > APP0_MAX_SIZE:
            messagebox.showerror(
                "Firmware too large",
                (
                    f"This firmware is {size:,} bytes, but the app partition on this "
                    f"partition scheme only holds {APP0_MAX_SIZE:,} bytes.\n\n"
                    "This usually means the firmware was compiled with a different "
                    "Partition Scheme selected in Arduino IDE than the device actually "
                    "has. Re-export with \"Default 4MB with spiffs (1.2MB APP/1.5MB "
                    "SPIFFS)\" selected, or double-check the device's current scheme "
                    "before proceeding."
                ),
            )
            return

        esptool_cmd = esptool
        if esptool_cmd is None:
            messagebox.showerror(
                "esptool not found",
                "Could not find esptool. If you're running this from source, "
                "install it with:\n\n    pip install esptool",
            )
            return

        first_time = self.first_time_flash.get()
        bootloader = self.bootloader_path.get()
        partitions = self.partitions_path.get()

        if first_time:
            missing = []
            if not bootloader or not os.path.isfile(bootloader):
                missing.append("bootloader")
            if not partitions or not os.path.isfile(partitions):
                missing.append("partition table")
            if missing:
                messagebox.showerror(
                    "Missing file(s)",
                    "First-time flash needs a " + " and ".join(missing) + " file - "
                    "browse to it (or place it next to the firmware file, named "
                    "<sketch>.ino.bootloader.bin / .ino.partitions.bin, and it'll be "
                    "found automatically).",
                )
                return

        files_to_clear = []
        if self.clear_html_svg.get():
            files_to_clear += FILE_GROUPS["html_svg"]
        if self.clear_config.get():
            files_to_clear += FILE_GROUPS["config"]
        if self.clear_patterns.get():
            files_to_clear += FILE_GROUPS["patterns"]

        if first_time:
            confirm_lines = [
                "FIRST-TIME FLASH - writes the bootloader and partition table too, not",
                "just the firmware. Only do this on a device that's never been flashed",
                "before.\n",
                f"About to write to device on:\n  {device}",
                f"  Bootloader: {os.path.basename(bootloader)}",
                f"  Partitions: {os.path.basename(partitions)}",
                f"  Firmware:   {os.path.basename(firmware)}\n",
            ]
        else:
            confirm_lines = [
                f"About to write:\n  {os.path.basename(firmware)}",
                f"to device on:\n  {device}\n",
                "Patterns and settings on the device will not be affected by the flash itself.",
            ]
        if files_to_clear:
            confirm_lines.append(
                "\nAfter it restarts, these files will be deleted and fresh defaults "
                "recreated:\n  " + ", ".join(files_to_clear)
            )
        confirm_lines.append("\nDo not disconnect the device during this process. Continue?")

        confirm = messagebox.askyesno("Confirm update", "\n".join(confirm_lines))
        if not confirm:
            return

        try:
            with open(firmware, "rb") as fw:
                firmware_sha256 = hashlib.sha256(fw.read()).hexdigest()
        except OSError:
            firmware_sha256 = "unavailable"
        self._run_info = {
            "port": self.selected_port.get() or device,
            "firmware": firmware,
            "size": size,
            "sha256": firmware_sha256,
            "files_to_clear": list(files_to_clear),
            "first_time_flash": first_time,
        }

        self.update_btn.configure(state="disabled")
        self.progress.start(12)
        self._log("=" * 60)
        self._log(f"Starting update on {device} ...")

        thread = threading.Thread(
            target=self._run_flash,
            args=(device, firmware, files_to_clear, first_time, bootloader, partitions),
            daemon=True,
        )
        thread.start()

    def _run_flash(self, device, firmware, files_to_clear, first_time=False,
                   bootloader=None, partitions=None):
        boot_app0_temp = None
        if first_time:
            # esptool wants a file path, not raw bytes, so the embedded boot_app0.bin
            # has to land on disk somewhere first - a temp file, cleaned up afterwards.
            fd, boot_app0_temp = tempfile.mkstemp(suffix="_boot_app0.bin")
            with os.fdopen(fd, "wb") as f:
                f.write(base64.b64decode(BOOT_APP0_B64))
            argv = [
                "--chip", DEFAULT_CHIP,
                "--port", device,
                "--baud", DEFAULT_BAUD,
                "write-flash",
                hex(BOOTLOADER_OFFSET), bootloader,
                hex(PARTITIONS_OFFSET), partitions,
                hex(BOOT_APP0_OFFSET), boot_app0_temp,
                hex(APP0_OFFSET), firmware,
                hex(APP1_OFFSET), firmware,
            ]
        else:
            argv = [
                "--chip", DEFAULT_CHIP,
                "--port", device,
                "--baud", DEFAULT_BAUD,
                "write-flash",
                hex(APP0_OFFSET), firmware,
                hex(APP1_OFFSET), firmware,
            ]
        self._log("Running esptool (in-process) with: " + " ".join(argv))

        writer = _QueueWriter(self._log)
        try:
            try:
                with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
                    esptool.main(argv)
            except SystemExit as exc:
                # esptool's CLI layer calls sys.exit() on some paths even on success.
                if exc.code in (0, None):
                    self._clear_selected_files(device, files_to_clear)
                    self._finish(True, None)
                else:
                    self._finish(False, f"esptool exited with code {exc.code}")
                return
            except Exception as exc:
                self._log(f"ERROR: {exc}")
                self._finish(False, str(exc))
                return

            self._clear_selected_files(device, files_to_clear)
            self._finish(True, None)
        finally:
            if boot_app0_temp:
                try:
                    os.remove(boot_app0_temp)
                except OSError:
                    pass

    def _send_command(self, ser, command, timeout):
        """Sends one serial command and collects the device's reply lines until its
        '<command>:OK' or '<command>:ERR ...' status line arrives (or we time out).
        Returns (lines, status) - status is 'OK', 'ERR', or None if no status
        line was seen (e.g. firmware without that command)."""
        ser.reset_input_buffer()
        ser.write(f"{command}\n".encode())
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            if line == f"{command}:OK":
                return lines, "OK"
            if line.startswith(f"{command}:ERR"):
                return lines, "ERR"
        return lines, None

    def _run_device_command(self, ser, command, timeout=None):
        lines, status = self._send_command(ser, command, timeout or CMD_TIMEOUT)
        detail = [l for l in lines if not l.startswith(f"{command}:")]
        for line in detail:
            self._log(f"    {line}")
        if status == "OK":
            self._log(f"{command} -> OK")
        elif status == "ERR":
            self._log(f"{command} -> {lines[-1]}")
        else:
            self._log(f"{command} -> (no response - this firmware may not support it)")
        return status

    def _clear_selected_files(self, device, files_to_clear):
        if not files_to_clear:
            return

        if serial is None:
            self._log("pyserial not available - could not send commands to clear files.")
            return

        # Order matters. FIX recreates config.ini from what the unit currently has in
        # memory, so if config.ini were deleted before FIX ran, the old settings would
        # just be written straight back. Config is therefore removed LAST, after FIX,
        # so it's genuinely missing at the next restart and resets to defaults then.
        config_files = [f for f in files_to_clear if f in FILE_GROUPS["config"]]
        other_files = [f for f in files_to_clear if f not in config_files]

        self._log("-" * 60)
        self._log("Waiting for the device to restart before clearing files...")
        time.sleep(3)  # give it a moment to reboot after the flash

        ser = None
        last_exc = None
        for _attempt in range(10):
            try:
                ser = serial.Serial(device, DEVICE_SERIAL_BAUD, timeout=0.5)
                break
            except Exception as exc:
                last_exc = exc
                time.sleep(1)

        if ser is None:
            self._log(f"Could not reconnect to {device} to clear files: {last_exc}")
            self._log(
                "The update itself still succeeded - you can do this yourself over "
                "serial: RM each file, then FIX to recreate the defaults."
            )
            return

        try:
            time.sleep(1)  # let the USB CDC connection settle after reconnecting
            for filename in other_files:
                self._run_device_command(ser, f"RM {filename}")

            if other_files:
                self._log("Recreating default files (FIX)...")
                self._run_device_command(ser, "FIX", FIX_TIMEOUT)

            for filename in config_files:
                self._run_device_command(ser, f"RM {filename}")
            if config_files:
                self._log(
                    "Config removed - it resets to defaults the next time the unit "
                    "restarts (unplug it and plug it back in)."
                )
        except Exception as exc:
            self._log(f"Error while clearing files: {exc}")
        finally:
            ser.close()

    @staticmethod
    def _one_line(text):
        """First line of a (possibly multi-line) error message."""
        lines = (text or "").strip().splitlines()
        return lines[0].strip() if lines else "unknown error"

    def _save_log_file(self, success, error_message):
        """Writes everything logged since the last saved log to a timestamped .log
        file. Tries next to the tool first, then falls back to the home folder and
        finally the system temp folder in case the tool's own folder isn't
        writable (e.g. Program Files). Returns the path written, or None."""
        info = self._run_info or {}
        now = datetime.datetime.now()
        filename = f"AutoChanger-deploy-{now.strftime('%Y-%m-%d_%H-%M-%S')}.log"

        header = [
            "AutoChanger Deployment Tool - update log",
            f"Date:      {now.strftime('%Y-%m-%d %H:%M:%S')}",
            "Result:    " + ("SUCCESS" if success else f"FAILED ({self._one_line(error_message)})"),
            f"Port:      {info.get('port', 'unknown')}",
            "Mode:      " + ("FIRST-TIME FLASH (bootloader + partitions + app)" if info.get("first_time_flash") else "Update (app only)"),
            f"Firmware:  {info.get('firmware', 'unknown')}",
            f"Size:      {info.get('size', 0):,} bytes",
            f"SHA-256:   {info.get('sha256', 'unknown')}",
            "Cleared:   " + (", ".join(info.get("files_to_clear", [])) or "nothing"),
            f"System:    {platform.platform()} / Python {platform.python_version()}"
            f" / esptool {getattr(esptool, '__version__', 'unknown')}",
        ]

        new_entries = self._log_history[self._log_saved_upto:]
        upto = len(self._log_history)
        body = "\n".join(header) + "\n" + "-" * 60 + "\n"
        body += "\n".join(f"[{t}] {m}" for t, m in new_entries) + "\n"

        for folder in (get_app_dir(), os.path.expanduser("~"), tempfile.gettempdir()):
            try:
                path = os.path.join(folder, filename)
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(body)
                self._log_saved_upto = upto
                return path
            except OSError:
                continue
        return None

    def _finish(self, success, error_message):
        def update_ui():
            self.progress.stop()
            self.update_btn.configure(state="normal")

            if success:
                self._log("Update complete.")
            else:
                self._log(f"Update failed: {self._one_line(error_message)}")

            log_path = self._save_log_file(success, error_message)
            if log_path:
                self._log(f"Log saved to: {log_path}")
                log_note = f"\n\nA log of this run was saved to:\n{log_path}"
            else:
                self._log("Could not save a log file (no writable location found).")
                log_note = ""

            if success:
                messagebox.showinfo(
                    "Update complete",
                    "The device has been updated. It should restart automatically."
                    + log_note,
                )
            else:
                messagebox.showerror(
                    "Update failed",
                    (
                        f"Something went wrong:\n{error_message}\n\n"
                        "The device's patterns and settings were not affected, since "
                        "this only writes the app partition. Check the connection and "
                        "try again."
                    ) + log_note,
                )
        self.after(0, update_ui)


if __name__ == "__main__":
    app = DeployApp()
    app.mainloop()
