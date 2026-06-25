# OnionDAO Badge — Pin Test 🔌

## What is this thing, in one sentence?

This is a little computer program that checks if someone **soldered the
metal pins on a circuit board correctly** — like a doctor giving a checkup
to a freshly-built electronic badge, before anything else gets plugged
into it.

## Wait, what's a "badge" and why does it need a checkup?

The [OnionDAO Badge](https://github.com/OnionDAO-git/oniondao-badge) is a
small electronic gadget (about the size of a name tag) built around a chip
called an **ESP32-S3** — basically a tiny computer brain. People build
these badges by hand, which means **soldering**: melting a bit of metal to
glue wires and pins onto the board so electricity can flow through them.

Soldering by hand is a bit like tying your shoelaces really, really
small — it's easy to do it slightly wrong without noticing. A "bad joint"
(a pin that isn't actually connected, or is touching its neighbor when it
shouldn't be) can make the whole badge act broken later, and it's hard to
tell *why* it's broken unless you check the soldering itself first.

So: **solder first → test with this project → THEN plug in the fun
add-on parts.** That way, if something doesn't work later, you already
know your soldering was fine, and the problem is somewhere else.

## A 30-second crash course on electricity (skip if you already get this)

- **Voltage** is basically "electrical pressure" — like water pressure in a
  pipe. This chip uses two voltage levels to mean something: **0 volts**
  means "off" / "logic 0", and **3.3 volts** means "on" / "logic 1". That's
  it — there's no in-between meaning, it's just a digital on/off switch
  made out of electricity instead of a mechanical lever.
- **GND** (ground) is the "0 volts" reference point that every other
  measurement compares against — like sea level on a map of mountain
  heights. You always put one multimeter probe on GND so the other probe's
  number means something.
- **A pin** is just one little metal leg of the chip (or the header it's
  soldered to) that can be told "be 3.3V" or "be 0V", or can be read to see
  which one it currently is.
- **GPIO** stands for "General Purpose Input/Output" — a fancy way of
  saying "a pin the program can freely turn on/off (output) or read
  (input)," as opposed to pins with one fixed special job.

## What does this program actually check? (3 tests)

Think of it like a 3-question quiz the badge has to pass:

### 1. "Can every pin turn on and off?" (the GPIO toggle test)
The badge has two little plastic connector strips called `J8` and `J10`
sticking out of its sides — each one has several metal pins. This test
makes the chip turn **each pin** on (3.3 volts of electricity) and off
(0 volts) three times in a row, one pin at a time, like flicking a light
switch on and off.

While it does that, **you** hold a multimeter (a tool that measures
electricity) against that same pin and watch the number jump between
`0V` and `3.3V`. If it jumps the way it's supposed to, that pin's solder
joint is good. If it's stuck, or doesn't move, that pin has a soldering
problem.

### 2. "Does the radio chip actually work?" (the CC1101 check)
A **CC1101** is a small radio chip — it can send and receive radio
signals, kind of like a tiny walkie-talkie part. If you've soldered one of
these onto the badge, this test talks to it over a digital connection
called **SPI** (think of SPI as a tiny, very fast, 4-wire conversation
between two chips: one wire to send data, one to receive data, one
"ticking" clock wire so both sides stay in sync, and one "are you
listening?" select wire) and asks it "what are you?" The chip answers back
with two ID numbers, called **PARTNUM** and **VERSION**.

Then, just to be extra sure, the program tells the chip "send me a
ticking clock signal" on one of its wires (called **GDO0**) and
**measures how fast it ticks** by counting how many times the wire
flips between 0V and 3.3V in a tiny 50-millisecond window. If the speed
matches what's expected (about 135,400 ticks per second — which comes
from the chip's internal crystal, divided by 192), that proves the wire
is connected correctly all the way from the chip to the badge — no
multimeter needed for this one, the program checks it automatically!

Why bother measuring a clock instead of just trusting the ID numbers?
Because, surprisingly, **a CC1101 reporting `VERSION = 0x00` is normal**
on plenty of genuine chips — so the ID alone can't tell you "good" from
"bad". A wire that's actually carrying a fast, correct-speed clock signal
is much harder to fake by accident, so it's the stronger proof.

### 3. "Can it actually make a sound?" (the speaker test)
If you've soldered on a **Sound module** (a tiny amplifier chip called an
**NS4168** paired with a microphone chip called an **SPH0641**), this test
sends digital audio data to it over a connection called **I2S** (a
standard way of streaming digital sound between chips — kind of like SPI,
but specialized for audio: it has a clock wire, a "left channel or right
channel right now?" wire, and a data wire) and plays a steady musical note
(440 Hz — the same note an orchestra tunes to before a concert) for
3 seconds.

There's no clever electronic way to "ask" a speaker if it's working — the
only test is **your own ears**. If you hear a clean, steady beep, it
passed. If you hear nothing, or something crackly, the wiring needs a
second look. (The microphone half of the module isn't tested here — only
the speaker/amplifier side.)

A small detail that trips people up: right after the tone finishes, the
program explicitly switches the audio channel off. Without that step, the
sound chip's hardware would keep looping its very last chunk of sound data
forever instead of going quiet — so a properly-finished test should end in
total silence, not a stuck buzzing noise.

### Bonus: it draws the results on the badge's little screen
The badge has a small black-and-white screen (an "e-paper" display — the
same kind of screen used in e-readers like a Kindle). E-paper is special
because, once it draws a picture, **it keeps showing that picture even
with zero power applied** — it only needs electricity for the brief
moment it's actually changing the image, not to keep displaying it. After
each test, the program writes the results right onto that screen, so you
don't even need a computer or Serial Monitor open to see how it went —
just look at the badge itself.

At the very end, the program tells the screen to "hibernate" (go into its
lowest power-use state) since there's nothing left to draw.

## What order does everything happen in? (the boot sequence)

When you plug the badge in and it powers up, here's exactly what happens,
in order — this matches the code in `firmware/main/main.cpp` top to
bottom:

1. **Serial port starts up** at 115200 baud (baud = "how many bits per
   second the text connection sends" — 115200 is just a commonly-used
   speed both sides agree on ahead of time) and waits 2 seconds so you
   have time to open the Serial Monitor before anything important prints.
2. **The screen turns on** and shows "Starting pin test... GPIO toggle
   pass running (watch multimeter now)" — your cue to grab the
   multimeter.
3. **The module power rail switches on** (`PIN_PWR_EN` goes HIGH). This
   badge deliberately routes power to the plug-in modules through a
   transistor controlled by one GPIO, instead of wiring them straight to
   the battery — that way the chip can cut power to modules it isn't
   using, to save battery. The program waits 5 milliseconds for the
   modules' own power supplies to settle before touching them.
4. **Test 1 runs**: all 14 candidate pins toggle HIGH/LOW/HIGH/LOW/HIGH/LOW
   (3 full cycles), one at a time, 1 second per half-cycle — this is your
   multimeter-watching window. The screen updates to "GPIO toggle: DONE".
5. **The CC1101 radio link is set up once** (SPI bus started) and
   **the Sound module is set up once** (I2S channel started, amplifier
   enabled) — both are initialized a single time here and reused, on
   purpose (see the pitfalls section below for why).
6. **Test 2 runs**: the CC1101 link check (ID registers + GDO0 clock
   frequency). Result goes to both the Serial Monitor and the screen.
7. **Test 3 runs**: the 3-second, 440 Hz tone plays through the Sound
   module. Result (well, the fact that it ran) goes to both Serial and
   the screen.
8. **A final summary screen** shows all three results at once, and the
   program settles into doing nothing forever (just an idle loop) — it
   will not repeat any test until you power-cycle or re-flash it.

All three tests run **once** per power-on, then the program just sits
there quietly — it won't keep re-triggering the tone or re-running the
SPI check on a loop.

## What's inside this folder? (a map)

```
oniondao-pin-test/
├── README.md                 ← you are here!
├── docs/
│   └── testing-guide.md      ← a full step-by-step guide, written for
│                                someone who has NEVER used a multimeter
│                                before. Start here if testing is new to you.
├── firmware/                  ← the "real" version of the program
│   ├── main/
│   │   └── main.cpp          ← all the actual test code lives here
│   ├── CMakeLists.txt        ← build instructions (tells the computer
│   │                            how to compile the program)
│   └── sdkconfig.defaults    ← chip configuration settings
├── arduino/
│   └── pin_test.ino          ← a simpler, beginner-friendly version of
│                                test #1 (the pin toggle test) only —
│                                no screen, no radio test, no sound test.
│                                Good if you just want to quickly check
│                                pins using the easier Arduino IDE.
└── components/                ← borrowed code other people wrote, that
                                  this project needs to draw on the
                                  e-paper screen. You don't need to read
                                  or touch these — they're just helper
                                  libraries, tucked in here so this project
                                  works completely on its own without
                                  needing the main badge repo too.
```

A bit more on that `components/` folder, since it's the biggest pile of
files in here and might look intimidating: it contains three popular,
open-source Arduino libraries, copied in as-is so the build doesn't have
to fetch them separately:

- **GxEPD2** — the library that actually knows how to talk to e-paper
  screens (there are hundreds of slightly different e-paper screen models
  in the world; this library supports a huge number of them, which is why
  there are so many similarly-named files in `components/GxEPD2/src/`).
  This badge only uses one of those screen drivers
  (`GxEPD2_270_GDEY027T91`), but the whole library comes along anyway.
- **Adafruit_GFX** — a generic "draw shapes and text" toolkit that GxEPD2
  builds on top of (so the same drawing code style works across all sorts
  of Adafruit displays, not just e-paper).
- **Adafruit_BusIO** — small helper code for talking over SPI/I2C, used
  internally by the other two libraries.

You will never need to edit anything inside `components/` — if you do
need to update one of these libraries, the normal way is to drop in a
newer copy of the whole library, not hand-edit files inside it.

## Which pins go where?

The badge's two connectors are wired to two add-on modules like this:

**CC1101 radio module → connector `J8`:**

| Radio module wire | Goes to badge pin | What it's for |
|---|---|---|
| SDI | G48 | data going INTO the radio chip |
| SCK | G47 | the shared clock "tick" wire |
| SCS | G19 | "are you listening?" select wire |
| SDO | G42 | data coming OUT of the radio chip |
| GDO | G41 | the chip's general-purpose signal pin (used here for the clock test) |

**Sound module → connector `J10`:**

| Sound module wire | Goes to badge pin | What it's for |
|---|---|---|
| SDI | G38 | data wire (despite the name, used as I2S clock input on this badge) |
| SCK | G39 | bit clock for the audio stream |
| SLR (WS) | G16 | word select — "is this sample for the left or right speaker?" |
| SDO | G15 | the actual digital audio data |
| CTR (CTRL) | G07 | turns the amplifier chip on/off |

(Plus a GND-to-GND wire and a VCC-to-VCC wire on each module, for power
and a shared ground reference.) If your own badge is wired differently,
you can change these pin numbers near the top of `firmware/main/main.cpp`.

Every pin the GPIO toggle test cycles through, in the exact order it runs,
straight from `firmware/main/main.cpp` (the Arduino sketch version checks
the same set, just labeled slightly differently since it has to cover
three possible wiring variants at once):

| Pin | Connector / slot | Used for |
|---|---|---|
| G48 | J8 pin 3 | CC1101 SDI |
| G47 | J8 pin 4 | CC1101 SCK |
| G19 | J8 pin 5 | CC1101 SCS |
| G42 | J8 pin 6 | CC1101 SDO |
| G41 | J8 pin 7 | CC1101 GDO |
| G40 | J8 pin 8 | spare (not used by this wiring) |
| G38 | J10 pin 3 | Sound SDI |
| G39 | J10 pin 4 | Sound SCK |
| G16 | J10 pin 5 | Sound SLR |
| G15 | J10 pin 6 | Sound SDO |
| G07 | J10 pin 7 | Sound CTR |
| G06 | J10 pin 8 | spare |
| G05 | J10 pin 9 | spare |
| G04 | J10 pin 10 | spare |

The "spare" pins still get toggled and tested even though no module wire
is currently planned for them — partly to check that header's solder
joint anyway, and partly so the same firmware keeps working if a future
module uses one of them.

There's also a separate set of pins, **not part of the toggle test**,
permanently wired to the badge's built-in e-paper screen (these don't
need testing the same way, since the screen either visibly works or it
visibly doesn't):

| Pin | Wire to the screen |
|---|---|
| G11 | SCK (clock) |
| G12 | CS (chip select) |
| G13 | DC (data/command select) |
| G14 | RST (reset) |
| G17 | MOSI (data) |
| G21 | BUSY (screen says "I'm still drawing, wait") |

And one more important pin: **G18** is `PIN_PWR_EN` — the single GPIO that
switches power to *both* plug-in modules on and off. If a module isn't
powering up at all, this is the first pin worth checking.

## How do I actually run it?

There are two ways, depending on how much you want to test:

### Option A — the full version (all 3 tests + screen)

This needs a tool called **ESP-IDF** (version 5.5.x) installed on your
computer — it's the official toolkit for programming ESP32 chips. Once
that's set up:

```powershell
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COM<N> flash monitor
```

(Swap `COM<N>` for whatever COM port your badge shows up as when you plug
it in via USB — on Windows you can find this in Device Manager.)

The first time you build, it automatically downloads one more helper
library (`arduino-esp32` — the Arduino-style programming layer for ESP32
chips, here used inside an ESP-IDF project) from the internet, based on
what's listed in `firmware/main/idf_component.yml`. Everything else
(the screen-drawing libraries) is already included in the `components/`
folder, so the project doesn't depend on anything outside this repo
except that one download.

A quick word on what each build-related file actually does, if you're
curious:
- `firmware/CMakeLists.txt` — the top-level "how to build this whole
  project" file. It also points the build at the `components/` folder so
  the vendored libraries get found.
- `firmware/main/CMakeLists.txt` — the same idea, scoped to just the
  `main` folder (the actual program code).
- `firmware/main/idf_component.yml` — a short list of which external
  libraries to automatically download (just `arduino-esp32` here) and
  which version to use.
- `firmware/sdkconfig.defaults` — chip-level settings (things like memory
  layout) that get baked in before the first build.

**Important version note:** this firmware is pinned to `arduino-esp32`
v3.3.8, which itself is built against ESP-IDF v5.5.4. If you have a
different ESP-IDF version installed (check with `idf.py --version`),
either switch to v5.5.x or expect possible build errors — these two
pieces need to stay in step with each other.

### Option B — the simple version (just the pin toggle test)

If you don't have ESP-IDF set up, or you just want the quickest possible
check of your soldering with no screen and no radio/sound checks, open
[`arduino/pin_test.ino`](arduino/pin_test.ino) in the **Arduino IDE**
instead. Pick "ESP32S3 Dev Module" as the board, upload it, and open the
Serial Monitor at 115200 baud. It covers every possible pin across all
the different wiring layouts, so it works no matter which exact module
wiring your badge uses.

## I don't even have a multimeter yet / I've never used one

No problem — that's exactly who [`docs/testing-guide.md`](docs/testing-guide.md)
was written for. It explains, completely from scratch:
- what a multimeter is and which buttons/dials to use,
- how to check your soldering is even connected at all, **before you ever
  turn the badge on** (this is called a "continuity test" and it can find
  a bad joint with no power needed),
- how to read the results from this program once the badge is powered on,
- a list of weird-looking readings that are actually totally normal
  (so you don't panic over nothing).

## Frequently asked questions

**Do I need both the CC1101 radio and the Sound module to use this?**
No. The GPIO toggle test (test 1) works on a bare board with nothing
plugged in. Tests 2 and 3 simply won't tell you anything useful about a
module you haven't soldered/wired yet — but they also won't crash or
break anything if you skip them.

**Can I test just one module and not the other?**
Yes — just don't wire up the one you're skipping, or ignore its result on
the screen. Each test is independent of the other two.

**What if my badge's wiring doesn't match the tables above?**
Edit the pin number constants near the top of `firmware/main/main.cpp`
(or `arduino/pin_test.ino` for the simple version) to match your actual
wiring, then rebuild and re-flash.

**Why does the program only run the tests once instead of looping?**
Because re-initializing SPI or I2S on every loop pass eventually starves
shared hardware resources (DMA channels / interrupts) and freezes the
next transfer — a real bug hit during development of this project. Doing
each setup step exactly once in `setup()` and reusing it avoids that
entirely.

**I plugged in the badge but see nothing on Serial / the screen — is it
broken?**
Give it the full 2-second startup delay, double-check the COM port and
baud rate (115200) in your Serial Monitor, and check the screen's own
wiring (G11/G12/G13/G14/G17/G21 above) — a screen wiring problem won't
stop the actual tests from running, it'll just stop you from seeing the
results that way (check the Serial Monitor instead in that case).

## Quick troubleshooting cheat-sheet

- **A pin's voltage doesn't move during the toggle test** → that pin's
  solder joint (or the wire to it) likely has a problem.
- **CC1101 `VERSION` reads as `0x00`** → that's actually fine, lots of
  real radio chips report that. Trust the GDO0 frequency check instead.
- **CC1101 readings are all `0xFF`** → that usually means nothing is
  responding at all (bad SDO wire, no power, or no chip).
- **No sound plays** → double check the speaker module's wiring and that
  its power is actually on.
- **A module's power pin reads 0 volts** → this badge deliberately turns
  module power on/off through software, so 0V is expected *before* the
  program tells it to turn on — not a fault by itself.

## Why was this built?

It came out of a real, hands-on soldering session for the OnionDAO
Badge — figuring out, in person, what actually goes wrong when an
ordinary person solders these tiny pins for the first time, and turning
those lessons into an automatic check anyone else can run.

## Glossary — every techy word in this README, explained simply

| Word | What it means here |
|---|---|
| **Solder / solder joint** | The blob of melted metal that physically and electrically connects a pin to the board. A "bad joint" means a weak, missing, or accidental connection. |
| **Firmware** | The program that runs directly on a chip (as opposed to an app on your phone or computer) — that's what everything in `firmware/` and `arduino/` is. |
| **GPIO** | A general-purpose pin the program can set to on/off or read from — see the crash-course section above. |
| **Voltage / Volts (V)** | "Electrical pressure." This chip uses 0V = off, 3.3V = on. |
| **GND (ground)** | The 0-volt reference point everything else is measured against. |
| **Multimeter** | A handheld tool for measuring voltage, continuity (is something connected?), and more. |
| **Continuity test** | Checking, with the power OFF, whether two points are physically connected through solder/wire — usually shown with a beep. |
| **SPI** | A fast 4-wire way for two chips to talk: data-out, data-in, clock, and select. Used here for the CC1101 radio. |
| **I2S** | A standard way to stream digital audio between chips. Used here for the Sound module. |
| **CC1101** | A specific brand/model of sub-GHz radio chip. |
| **NS4168 / SPH0641** | The specific amplifier chip and microphone chip that make up the "Sound module." |
| **GDO0** | One of the CC1101's general-purpose signal pins; this firmware uses it to output a test clock signal. |
| **PARTNUM / VERSION** | Two ID numbers a CC1101 chip reports back when asked "what are you?" |
| **e-paper / e-ink** | A type of screen that keeps showing its last image with no power, only using electricity while actually changing. |
| **Baud rate** | How fast a serial (text) connection sends data, measured in bits per second. This project uses 115200. |
| **Serial Monitor** | A window in your computer's IDE/tool that shows text the chip is printing over USB. |
| **ESP32-S3** | The specific computer chip this badge is built around. |
| **ESP-IDF** | Espressif's (the chip maker's) official software toolkit for programming ESP32 chips — the "full version" build path uses this. |
| **Arduino IDE** | A simpler, beginner-friendly programming tool that the "Option B" simple test uses instead of ESP-IDF. |
| **`idf.py`** | The command-line tool ESP-IDF gives you to build and flash (upload) firmware. |
| **Flash (verb)** | Uploading a compiled program onto the chip's permanent memory. |
| **DMA** | "Direct Memory Access" — a hardware shortcut that lets peripherals like SPI/I2S move data without constantly bothering the main chip; mentioned here because re-initializing it carelessly can freeze a transfer. |
