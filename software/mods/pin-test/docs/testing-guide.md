# Testing Guide — Verifying Solder Joints From Zero

This walks through the entire process used to verify hand-soldered header
pins on the OnionDAO Badge, written for someone who had never touched a
multimeter before. If you already know your way around a DMM, skip to
[Continuity test](#2-continuity-test-power-off) or
[GPIO toggle test](#3-powered-gpio-toggle-test).

## 0. Why test before wiring up modules

If you solder header pins to a board and then immediately wire jumpers to
a plug-in module, a bad joint (cold joint, solder bridge, missing
connection) shows up as a confusing module malfunction with no obvious
cause. Testing the bare board first isolates "is my soldering good" from
"does my module work" — two completely different failure domains.

## 1. Multimeter basics

**Parts of the meter:**
- **Screen** — shows the number.
- **Dial** — selects what you're measuring. Look for:
  - **Continuity mode** — a diode/sound-wave icon, beeps when connected.
    On meters with a shared dial position for resistance/continuity/diode
    (e.g. AstroAI AM33D), a yellow **SELECT** button cycles between them —
    press it until the screen shows the continuity icon, then confirm by
    touching the two probes together: you should hear a beep and see ~0.
  - **DC voltage (DCV)** — a `V` with a straight line (as opposed to `V`
    with a wavy line, which is AC). Auto-ranging meters need no further
    setup; manual-range meters need a range above 3.3V (e.g. "20V").
- **Probes** — black plugs into `COM`. Red plugs into `VΩ` (ignore any
  separate high-current `10A` jack — never needed for this).

**Taking a reading:** touch black probe to your reference point (usually
GND), red probe to the thing you're testing, read the screen (or listen
for the beep in continuity mode). That's the entire skill — everything
below is just "which two points, in which mode."

## 2. Continuity test (power OFF)

Unplug USB and any battery before this section — continuity testing
should always be done unpowered.

**Step 1 — visual check.** Magnifier + raking light. Look for solder
bridges between adjacent pins (the most common defect on small headers),
dull/cracked cold joints, and pins sitting crooked.

**Step 2 — GND-to-GND (expect a beep).** Touch black probe to one GND
pin, red probe to another GND pin (different header is fine — it's all
one ground plane). A beep confirms that joint has real continuity through
the board, not just looking soldered.

**Step 3 — adjacent-pin sweep (expect NO beep).** This is the one that
actually catches bridges. Walk down every header, checking each
*physically neighboring* pair of pins:

```
pin1 vs pin2, pin2 vs pin3, pin3 vs pin4, ... 
```

Any beep between two pins that shouldn't be connected means a solder
bridge — touch it up with the iron (don't just add more solder) and
re-test.

**Step 4 — repeat for every header** you soldered. On this project that
was 4 headers: the two side expansion ports (`J8`, `J10`) on the main
board, and the two module connectors (CC1101, Sound) on their own boards.

If you also have a VCC rail on the header, check it the same way — VCC
should NOT beep to GND or to neighboring signal pins, but two VCC pins on
the same header (e.g. both ends of a 10-pin header) SHOULD beep to each
other, confirming they're the same rail.

## 3. Powered GPIO toggle test

Continuity alone can miss a *cracked* joint that happens to still touch at
rest but opens under any flex. The fix: make the chip actually drive each
pin and watch it swing.

Flash the firmware in [`../firmware`](../firmware) (see the main
[README](../README.md) for build steps), then:

1. Multimeter on DC volts, black probe on any GND pin.
2. Open Serial Monitor (115200 baud) to see which pin is currently being
   driven.
3. Touch the red probe to that pin's solder joint.
4. Expect the reading to swing **0V → ~3.3V → 0V**, three times, in step
   with the printout.

A pin stuck at 0V, stuck at 3.3V, or not moving at all = bad joint, wrong
pin, or wrong wiring variant. A small flickering reading (e.g. 0.1–0.2V)
*after* a pin has been released back to floating/input is normal noise,
not a defect — floating pins always pick up tiny ambient noise.

## 4. Automated functional checks (after wiring up jumpers)

Once the bare board's GPIOs pass, wire the jumpers to your plug-in
modules and re-flash. The same firmware now runs two stronger,
fully-automated tests:

- **CC1101 link check** — reads the chip's `PARTNUM`/`VERSION` registers
  over real SPI, then forces a known clock frequency onto `GDO0` and
  measures it. A frequency match (within a few percent — software polling
  always undercounts a little) is a far stronger proof than continuity
  alone, because it requires the *entire* path (jumpers + solder + the
  actual silicon) to work correctly.
- **Sound module tone-out** — plays a tone over I2S. No chip ID exists to
  read back on a dumb amp/mic pair, so your ears are the test instrument
  here. Hearing a clean steady tone = pass.

Both results are also drawn to the badge's e-paper screen, so you don't
need to keep the Serial Monitor open.

## 5. Common pitfalls hit during this process

- **Re-initializing a peripheral (SPI/I2S) on every loop iteration**
  eventually starves a shared DMA/interrupt resource and hangs the next
  transfer, tripping the watchdog. Initialize once in `setup()`, reuse the
  handle.
- **I2S hardware keeps repeating its last DMA buffer** once you stop
  feeding it new data, instead of going silent — explicitly call
  `i2s_channel_disable()` when you're done playing audio.
- **A multimeter's continuity mode shows noisy fluctuating digits (not a
  beep) on an open/disconnected probe** — that's normal floating-pin
  noise, not a fault. No beep is the pass condition; ignore the wiggling
  number.
- **`VERSION=0x00` from a CC1101** is a known-normal reading on many real
  and clone chips — don't treat it alone as a failure. The frequency-match
  result on `GDO0` is the decisive test.
- **VCC on a module connector reads 0V even when wiring is correct** if
  it's fed from a switched/gated power rail (this badge gates peripheral
  power through a GPIO-controlled transistor) — 0V before firmware
  asserts that GPIO is expected, not a fault.
