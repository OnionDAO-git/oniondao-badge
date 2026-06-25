# Pin Test

Firmware mod for verifying hand-soldered header pins on the badge
**before** wiring up plug-in modules with jumper wires. Built out of a
real soldering session — see [`docs/testing-guide.md`](docs/testing-guide.md)
for a from-scratch multimeter walkthrough.

## 1. Functionality

On boot, the firmware runs three checks once, then goes idle:

1. **GPIO toggle pass** — cycles every signal pin on the two side
   expansion ports (`J8` left, `J10` right), driving each HIGH/LOW 3x so
   you can confirm the solder joint with a multimeter (DC volts, pin to
   GND — expect a clean 0V↔3.3V swing).
2. **CC1101 link check** — talks real SPI to a CC1101 sub-GHz radio
   module on `J8`, reads back its `PARTNUM`/`VERSION` ID registers, then
   forces a free-running test clock onto `GDO0` and measures its
   frequency. A match against the expected ~135.4 kHz (`crystal/192`)
   proves the entire path — solder joints, jumper wires, and the chip
   itself — end to end. No multimeter needed for this part.
3. **Sound module tone-out** — drives a Sound module (NS4168 amp +
   SPH0641 mic) on `J10` over I2S and plays a 3-second 440 Hz tone. No ID
   register exists on a dumb amp/mic pair, so hearing the tone is the
   pass condition.

Each phase's result is also drawn to the badge's e-paper display, so a
Serial Monitor isn't required to read results.

A simpler Arduino-IDE-only sketch ([`arduino/pin_test.ino`](arduino/pin_test.ino))
covers just the GPIO toggle test, with no display/CC1101/Sound checks —
useful before you know your module wiring variant, since it covers the
union of pins across all three wiring variants (L1/L2/R).

## 2. Module compatibility

- **CC1101** — yes, tested (SPI link + GDO0 clock check)
- **Sound (NS4168/SPH0641)** — yes, tested (I2S tone-out)
- Works with **no modules attached** too — the GPIO toggle pass needs
  only the bare board.

## 3. GPIO usage

| Pinout label | Used as |
|---|---|
| G48 | CC1101 SDI (J8) |
| G47 | CC1101 SCK (J8) |
| G19 | CC1101 SCS (J8) |
| G42 | CC1101 SDO (J8) |
| G41 | CC1101 GDO0 (J8) |
| G40 | J8 spare |
| G38 | Sound SDI (J10) |
| G39 | Sound SCK (J10) |
| G16 | Sound SLR/WS (J10) |
| G15 | Sound SDO (J10) |
| G07 | Sound CTR/CTRL (J10) |
| G06, G05, G04 | J10 spares |
| G18 | Module power rail enable (asserted HIGH in `setup()`) |
| G11, G12, G13, G14, G17, G21 | On-board e-paper SCK/CS/DC/RST/MOSI/BUSY (not part of the toggle test) |

All names match `docs/PINOUT.md`. If your wiring differs, edit the pin
constants at the top of `firmware/main/main.cpp` (or `arduino/pin_test.ino`).

**BOOT pin / power-management conventions followed:**
- No GPIO0 (BOOT) usage in this mod.
- GPIO18 is asserted HIGH during `setup()` to power the module rail,
  matching the documented convention; this mod doesn't implement deep
  sleep, so there's no corresponding drop.

## 4. Build & flash

Requires ESP-IDF **v5.5.x** (pins `arduino-esp32` v3.3.8, built against
v5.5.4 — keep these in lockstep).

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

First build fetches `arduino-esp32` from the ESP Component Registry
automatically. `GxEPD2`, `Adafruit_GFX`, and `Adafruit_BusIO` are
vendored locally under `components/` so this mod builds standalone.

**Arduino IDE alternative:** open [`arduino/pin_test.ino`](arduino/pin_test.ino),
board "ESP32S3 Dev Module", 115200 baud Serial Monitor. GPIO-toggle-only,
no extra dependencies.

## 5. Visual evidence

The e-paper display shows a live `PIN TEST` status screen at each phase
(toggle pass complete, CC1101 PARTNUM/VERSION + GDO0 Hz + PASS/FAIL, sound
tone played) — see [`docs/testing-guide.md`](docs/testing-guide.md) for
what a passing vs. failing reading looks like on both the display and a
multimeter.

## Continuity testing (before powering anything on)

The GPIO toggle test only catches problems once the board is powered. Do
a cold continuity sweep with a multimeter first — full procedure in
[`docs/testing-guide.md`](docs/testing-guide.md).
