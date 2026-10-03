# AutoChanger Deployment Tool

A simple GUI tool for updating an AutoChanger unit's firmware over USB,
without touching anything saved on the device (patterns, config, etc).

## Why this is safe

This tool writes **only** to the two app slots, `app0` (flash offset `0x10000`) and
`app1` (`0x150000`) — the same regions a normal Arduino IDE upload or an OTA update
writes to. It never touches:

- the bootloader
- the partition table
- the OTA boot-selector (`otadata`)
- the SPIFFS filesystem (patterns, config, web files)

**Both slots get the same image, on purpose.** On a unit that has never used OTA
the bootloader always runs `app0`. Once OTA has been used, `otadata` may point at
`app1` instead, and the tool doesn't read `otadata` to find out which is active.
Writing both means the unit boots the new firmware either way. The trade-off: on
an OTA-enabled unit the inactive slot normally holds the *previous* firmware as a
fallback, and this overwrites it — so there's no falling back to the old version
after an update.

This is correct for any unit on the **"Default 4MB with spiffs (1.2MB APP/1.5MB
SPIFFS)"** partition scheme — which includes any v3.9 unit, since those were never
re-partitioned. The tool does **not** currently read the unit's partition table to
confirm that: it only refuses a firmware file that's too large for a slot. If a
unit's scheme is ever changed (e.g. to use more of a 16MB chip's flash), the fixed
offsets and size in `deploy_tool.py` (`APP0_OFFSET`, `APP1_OFFSET`,
`APP0_MAX_SIZE`) would be wrong for it, and the tool would write to the wrong
place without complaint.

## Using it

1. Get the firmware `.bin`: in Arduino IDE, **Sketch > Export Compiled
   Binary**. This drops a `.bin` file next to your sketch.
2. Open the AutoChanger Deployment Tool.
3. Browse to that `.bin` file.
4. Plug the AutoChanger in over USB, click Refresh if needed, and select its
   port.
5. Click **Update Device**.

The tool checks the firmware isn't larger than the app partition can hold
before doing anything, and shows esptool's live output in the Details pane
so you can see exactly what's happening.

## First-time flash (brand new / blank device)

A chip that's never been flashed has no bootloader or partition table, so a
normal update - which only writes the app itself - produces nothing bootable.
Tick "This is a first-time flash on a brand new/blank device" under section 1
and the tool also writes:

| File | Offset | Purpose |
|---|---|---|
| bootloader.bin | `0x0` | second-stage bootloader (ESP32-S3-specific address) |
| partitions.bin | `0x8000` | the partition table itself |
| *(built into the tool)* | `0xe000` | `boot_app0.bin` - selects the first boot slot |
| firmware.bin | `0x10000` / `0x150000` | same as a normal update |

`boot_app0.bin` isn't sketch-specific - it's one fixed file from the ESP32 core
install, embedded directly in the tool rather than something you need to find.

`bootloader.bin` and `partitions.bin` **are** sketch-specific. Arduino's Export
Compiled Binary produces them next to your `.bin`, named `<sketch>.ino.bootloader.bin`
and `<sketch>.ino.partitions.bin` - the tool looks for them there automatically
and fills the fields in if found, or Browse to them otherwise.

Only use this on a device that's genuinely never been flashed. On a device that's
already running AutoChanger it's unnecessary - a normal update is all that's
needed - though since the bootloader/partition table/app all sit before SPIFFS
on the flash, it wouldn't touch your patterns or config even if run by mistake,
as long as the partition scheme itself doesn't change.

## Clearing files after an update (optional)

Ticking files under "3. Clear files on update" makes the tool, once the unit has
restarted on the new firmware, delete them over serial (`RM`) and then run `FIX`
so the unit recreates fresh defaults. The firmware only writes its default web
pages at startup when `config.ini` is missing, so `FIX` is what actually brings
the pages back straight away.

- **HTML & SVG** (ticked by default) and **Patterns** are recreated immediately.
- **Config** is removed *last*, after `FIX`. `FIX` rebuilds `config.ini` from what
  the unit currently has in memory, so removing it first would just write your
  old settings back. It resets to defaults the next time the unit restarts.
- This needs firmware with the serial command feature (`RM`/`FIX`, v4.1+). On
  anything older the tool logs "no response" for each step and carries on.

## Update logs

After every update attempt - successful or not - the tool saves the contents of
the Details pane to a timestamped file such as
`AutoChanger-deploy-2026-09-28_14-30-05.log`, and shows the path when it
finishes. It's written next to the tool (falling back to your home folder, then
the temp folder, if that location isn't writable). Each log starts with a
header - result, port, firmware file with its size and SHA-256, which files were
cleared, and OS/Python/esptool versions - followed by every line of output with a time. Handy to attach when
something goes wrong. Each log only contains what happened since the previous
one, so logs from several updates in one session don't repeat each other.

## Building the executable yourself

PyInstaller can't cross-compile — a Windows `.exe` has to be built on
Windows, a Mac app on a Mac. This folder includes:

- `deploy_tool.py` — the actual tool (pure Python, runs anywhere Python 3 +
  the two requirements are installed, no build needed for that).
- `requirements.txt` — `esptool` and `pyserial`.
- `build_windows.bat` — run on a Windows machine to produce
  `dist\AutoChangerDeployTool.exe`.
- `build_unix.sh` — run on Mac or Linux to produce
  `dist/AutoChangerDeployTool` (Mac users get an unsigned binary; first
  launch may need a right-click > Open to bypass Gatekeeper once).

A Linux build was already produced and tested as part of building this tool
(the build completed cleanly with all dependencies resolving, and the
resulting binary launches and initializes without error) — running the
matching script on Windows and Mac will produce the equivalents for those
platforms.

## Known limitation

Right now this only performs the **safe, app-only update**. It does not yet
support the (destructive) full repartition-and-reflash path for moving a
unit onto a larger partition scheme to use more of a 16MB chip's flash —
that needs a SPIFFS backup/restore step built in before it's safe to offer,
and is intentionally left out of this first version.
