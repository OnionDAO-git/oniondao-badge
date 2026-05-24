# NULL City Badge Workshop — Instructor Notes

This file is for instructors running a workshop session. Participants should use
[CONTRIBUTING.md](../CONTRIBUTING.md) instead.

---

## Suggested 2-Hour Schedule

| Time | Activity |
|------|----------|
| 0:00 – 0:20 | Welcome + hardware tour (use board-explorer.html on a projector) |
| 0:20 – 0:40 | Setup check: everyone has firmware flashed, serial working, art tool running |
| 0:40 – 1:00 | Challenge 1 — Your Name on the Badge |
| 1:00 – 1:30 | Challenge 2 — Add an About Me Screen |
| 1:30 – 2:00 | Challenge 3 / open hacking / show-and-tell |

For a 3-hour session, add a 30-minute hardware segment before the coding challenges
where participants wire an I²C sensor (Challenge 4).

---

## Pre-Workshop Checklist (send to participants 48 hours before)

Participants need these before arriving. Save debugging time by having them verified ahead of time.

- [ ] Install [PlatformIO](https://platformio.org/) — either the CLI or the VS Code extension
- [ ] Install Python 3.8+ and run `pip install pillow pyserial`
- [ ] Install the CH340 USB-UART driver if on Windows or older macOS
  - Windows: [CH340 driver](https://www.wch-ic.com/products/CH340.html)
  - macOS 10.15+: driver is built-in
  - Linux: driver is built-in, but add your user to the `dialout` group: `sudo usermod -aG dialout $USER`
- [ ] Clone or download this repo
- [ ] Confirm `pio run` completes without errors from the `firmware/` directory
- [ ] Bring a USB-C cable (data-capable, not charge-only)

---

## Day-of Setup Checklist

Run through this at the start of the session:

1. Connect badge via USB-C
2. Flip the ON/OFF slide switch on the back to ON
3. Confirm the screen shows something (home screen or menu)
4. Run `pio device monitor` — confirm serial output appears
5. Open `badge-art/badge_art.py`, select the correct port, draw something, click Send
6. Confirm the badge screen updates

If step 3 fails: check the switch position, try a different USB cable, try holding BOOT + pressing RST.

---

## Common Issues and Fixes

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `pio run` fails on first run | Library download timeout | Run again — PlatformIO retries |
| Upload fails: "no serial data" | Badge not in download mode | Hold BOOT, press RST, release BOOT, retry upload |
| Upload fails on Linux: permission denied | User not in dialout group | `sudo usermod -aG dialout $USER`, log out and back in |
| Serial port not found | Wrong port or driver missing | Check Device Manager (Windows) or `ls /dev/tty*` (Linux/macOS) |
| Badge screen stays white after flash | Display driver mismatch | Confirm `GxEPD2_270_GDEY027T91` is used, not `GxEPD2_270` |
| Art tool sends image but screen doesn't update | Badge in deep sleep | Press any button on the badge first, then send within 60 seconds |
| Art tool: port dropdown empty | pyserial not installed | `pip install pyserial` |
| I²C device not found (Challenge 4) | Peripheral rail off | Confirm GPIO18 is HIGH in setup(); power cycle the badge |

---

## Resetting a Badge to Factory Firmware

If a participant's badge is in a bad state and won't boot:

```bash
~/.local/bin/esptool --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 backups/badge-original-firmware-backup.bin
```

This flashes the original factory image. Run it with the badge in download mode
(hold BOOT, press RST, release BOOT).

---

## Projector Setup for the Hardware Tour

Open `docs/board-explorer.html` on the projector machine:

```bash
# From the repo root
python3 -m http.server 8080
```

Then open `http://localhost:8080/docs/board-explorer.html` in Chrome or Firefox.

Click each component button to show a description, pin table, and code snippet.
Good order for the hardware tour: ESP32-S3 → E-Paper Display → TCA9534 →
ATECC608B → Power Transistor Q5 → GPIO Header.

---

## Expanding the Workshop

If participants finish all challenges early:

- **Custom fonts** — Use [fontconvert](https://github.com/adafruit/Adafruit-GFX-Library/tree/master/fontconvert)
  to convert a TTF font to a C header and use it with `display.setFont()`
- **Badge-to-badge comms** — The CC1101 sub-GHz radio module attaches to the bottom
  connector. See `pcb/CC1101_MOD.kicad_sch` for the pinout.
- **Interrupt-driven buttons** — Replace the polling loop with a GPIO1 interrupt
  handler that fires only when a button is pressed
- **Deeper crypto** — The ATECC608B can generate key pairs and sign messages;
  see the Arduino ATECC608 library documentation
