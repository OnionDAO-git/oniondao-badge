# NULL City Badge — Project Documentation

## What This Is

The NULL City Badge is a wearable hardware badge built around an ESP32-S3
microcontroller. It has a 2.7-inch e-paper display, six buttons, a secure
element chip, and expansion ports for radio (CC1101) or audio modules.

This repo contains everything needed to build and flash custom firmware,
and a desktop tool to draw artwork and send it to the badge over USB.

---

## Hardware Summary

| Component | Part | Interface | Notes |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N8R8 | — | 240 MHz, 8 MB flash, 8 MB PSRAM |
| USB-UART | CH340C | UART → USB | Connects to `/dev/ttyUSB0` |
| Display | TWE0270NQ23-AO | SPI | 2.7" e-paper, SSD1680, 264×176 px |
| Buttons | TCA9534 I/O expander | I²C @ 0x20 | 6 buttons (PB1–PB6), active LOW |
| Secure element | ATECC608B | I²C @ 0x60 | Crypto, needs GPIO8 HIGH to enable |
| Power rail | Transistor Q5 | GPIO18 | HIGH enables display + I²C devices |

### Pin Reference

```
Power rail enable : GPIO18 (HIGH = on)
Secure element EN : GPIO8  (HIGH = on)

I²C               : SCL = GPIO9   SDA = GPIO10

E-paper SPI       : MOSI = GPIO17   SCK  = GPIO11
                    CS   = GPIO12   DC   = GPIO13
                    RST  = GPIO14   BUSY = GPIO21

Button IRQ        : GPIO1 (falling edge, unused — we poll instead)
```

---

## Repository Layout

```
null-city-badge-main/
├── README.md              Top-level overview and quick start
│
├── firmware/              Active Arduino/PlatformIO project
│   ├── platformio.ini     Board config, dependencies
│   ├── include/
│   │   └── badge_pins.h   All pin numbers in one place
│   └── src/
│       └── main.cpp       Entire firmware source
│
├── badge-art/             Desktop tool: draw or import images, send to badge
│   ├── badge_art.py       Launch this to open the art tool
│   └── requirements.txt   Python deps: pillow, pyserial
│
├── pcb/                   KiCad schematic and PCB design files
│   ├── *.kicad_sch        Schematics (main + module sub-schematics)
│   ├── *.kicad_pcb        Board layout
│   ├── 3d/                3D step model
│   └── production/        Gerbers, IPC netlist, BOM spreadsheets
│
├── backups/
│   └── badge-original-firmware-backup.bin   Factory firmware image
│
└── docs/
    ├── project.md          This file — full project reference
    ├── hardware-reference.md Pin headers, connectors, components — soldering ref
    ├── guide.md            Beginner hardware + C/C++ learning guide
    └── claude-commands.md  Claude Code workflows for badge development
```

---

## Building and Flashing

### Requirements

- [PlatformIO](https://platformio.org/) (already installed at `~/.local/bin/pio`)
- Badge connected via USB on `/dev/ttyUSB0`

### Build only

```bash
cd firmware
pio run
```

The compiled binary lands at `firmware/.pio/build/null-city-badge/firmware.bin`.

### Build and flash

```bash
cd firmware
pio run --target upload
```

If the upload fails to connect, put the badge into download mode manually:
1. Hold **BOOT** button
2. Press and release **RST** button
3. Release **BOOT**
4. Run `pio run --target upload` again

### Serial monitor

```bash
cd firmware
pio device monitor
```

Or directly: `python3 -m serial.tools.miniterm /dev/ttyUSB0 115200`

---

## Firmware Overview (`firmware/src/main.cpp`)

### State machine

All screens are managed by an `AppState` enum and a `g_state` global.
A `g_needs_redraw` flag triggers `dispatch_render()`, which calls the
correct `draw_*()` function for the current state.

| State | Screen |
|---|---|
| `STATE_HOME` | Home screen — custom image (from NVS) or text fallback |
| `STATE_MENU` | 8-item main menu |
| `STATE_SYS_INFO` | ESP32 chip info |
| `STATE_BTN_TEST` | Live button press display |
| `STATE_I2C_SCAN` | I²C bus scan results |
| `STATE_RNG` | 16 bytes of hardware random output |
| `STATE_DISPLAY_TEST` | Four graphics patterns |
| `STATE_GUIDE` | Pageable lesson screens (Hardware or Software guide) |
| `STATE_ESPNOW` | ESPNow three-tab UI (BEACON / PEERS / INBOX) |
| `STATE_ESPNOW_EDIT` | Callsign character editor |
| `STATE_ESPNOW_TARGET` | Unicast action picker (PING / MESSAGE) |
| `STATE_ESPNOW_MSG` | Message text composer |

### `setup()`

Runs once at boot:

1. Set UART RX buffer to 8192 bytes (needed for the 5808-byte image transfer)
2. Enable GPIO18 → powers the peripheral rail
3. Enable GPIO8 → wakes the ATECC608B secure element
4. Wait 50 ms for regulators to settle
5. Init I²C, scan for TCA9534 and ATECC608B
6. Configure TCA9534 (all pins = inputs for buttons)
7. Init SPI, init the e-paper display via GxEPD2
8. Set `g_state = STATE_HOME`, `g_needs_redraw = true`

### `loop()`

Runs continuously after `setup()`:

1. `check_serial_image()` — watches for `IMG:` on serial; when found, reads
   5808 bytes, displays the image, saves it to NVS, then sets
   `g_state = STATE_HOME` and `g_needs_redraw = true`
2. `dispatch_render()` — if `g_needs_redraw`, calls the draw function for
   `g_state` then clears the flag
3. `read_buttons()` — polls TCA9534; rising-edge detection fires
   `handle_buttons()` which updates `g_state` / `g_needs_redraw`
4. **Idle timeout** — after 60 seconds of no activity, calls `go_to_sleep()`

### Navigation

| From | Button | Goes to |
|---|---|---|
| Home screen | Any | Menu |
| Menu | UP / DOWN | Move cursor |
| Menu | SELECT | Enter highlighted item |
| Menu | CANCEL | Home screen |
| Any sub-screen | CANCEL | Menu |
| Hardware/Software Guide | LEFT / RIGHT | Prev / next of 10 pages |

### Deep Sleep

The badge goes to deep sleep automatically after **60 seconds** of inactivity.
During deep sleep the ESP32 draws ~10 µA and the e-paper display holds its
image without any power. Press any button (PB1–PB6) to wake.

| State | Current draw |
|---|---|
| Active (loop running) | ~90 mA |
| Deep sleep + e-paper holding image | ~15 µA |

To send a new image from the art tool, the badge must be awake. If it has gone
to sleep, press any button first, then click **Send to Badge ▶** within 60
seconds.

### Home Screen — Custom Image

On boot, `draw_home_screen()` checks NVS for a previously saved image:

- **Image found** — loads the 5808-byte bitmap from NVS and draws it
- **No image** — draws a text fallback with chip specs

Use the art tool to send an image; the badge saves it to NVS automatically and
the custom image persists across power cycles and deep sleep.

### Serial Image Protocol

The art tool sends:

```
"IMG:" (4 bytes)  +  5808 bytes bitmap
```

Bitmap format: 264×176 pixels, 1 bit per pixel, MSB first.
Bit = 1 → black pixel. Bit = 0 → white pixel (Adafruit GFX convention).

Badge replies `OK\n` after displaying and saving the image to NVS.

---

## Display Notes

**Panel:** TWE0270NQ23-AO  
**Controller:** SSD1680  
**GxEPD2 class:** `GxEPD2_270_GDEY027T91` (in `src/gdey/`)

> **Important:** `GxEPD2_270` looks like the right match (same 264×176 size)
> but targets the completely different IL91874 controller. Using it silently
> resets the SSD1680 instead of refreshing — the display shows nothing.
> Always use `GxEPD2_270_GDEY027T91` for this badge.

The driver's native orientation is portrait (WIDTH = 176, HEIGHT = 264).
`display.setRotation(1)` gives a landscape canvas of 264×176, which matches
the physical badge orientation.

Full refresh time: ~1512 ms. Call `display.hibernate()` after every refresh
to put the SSD1680 into low-power deep sleep.

---

## Art Tool (`badge-art/badge_art.py`)

### Dependencies

```bash
pip install pillow pyserial
```

`python3-tk` is usually pre-installed; if not: `sudo apt-get install python3-tk`

### Launch

Double-click **Badge Art** on the Desktop, or:

```bash
python3 badge-art/badge_art.py
```

### Features

| Action | How |
|---|---|
| Draw black | Left-click / left-drag |
| Erase | Right-click / right-drag, or select Erase radio button |
| Import image | Open Image → any PNG/JPG; auto-resized and dithered to 264×176 |
| Save work | Save PNG → reopen later with Open Image |
| Invert | Swap all black/white pixels |
| Send to badge | Select port, click Send to Badge ▶ |

The tool opens the serial port with DTR/RTS held low so the badge does **not**
reset when the connection is made. The badge must already be running the
current firmware to receive images.

---

## Restoring Factory Firmware

If you want to go back to the original badge firmware:

```bash
~/.local/bin/esptool --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 backups/badge-original-firmware-backup.bin
```

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| GxEPD2 | ^1.5.9 | E-paper display driver |
| Adafruit GFX Library | ^1.11.9 | Drawing primitives (text, shapes, bitmaps) |
| Adafruit BusIO | ^1.16.1 | SPI/I²C abstraction used by GFX |
| Preferences | (ESP32 core) | NVS key-value store — persists home screen image |

---

## ESPNow (Menu Item 8)

ESPNow is Espressif's direct 2.4 GHz peer-to-peer protocol. Badges talk
to each other without a router or access point — range is roughly 100–200 m
open air, 10–50 m indoors.

### Lifecycle

ESPNow is **fully on-demand**. It only runs while the user is inside the
ESPNow screen family. Entering menu item 8 calls `init_espnow()`, which starts
Wi-Fi STA mode and registers the receive callback. Pressing CANCEL from any
ESPNow screen calls `shutdown_espnow()`, which runs `esp_now_deinit()` and
`WiFi.mode(WIFI_OFF)`. The badge returns to its normal low-power state with
no background radio activity.

### Three-Tab UI

LEFT / RIGHT cycles between three sub-screens:

| Tab | What it shows | Key buttons |
|---|---|---|
| BEACON | Own callsign, MAC, sent count, peer count, last RTT, last received badge | SELECT = send beacon, UP = edit callsign |
| PEERS | Scrollable list of all discovered badges with RSSI bar and proximity tier | UP/DN = scroll, SELECT = target a peer |
| INBOX | Last 5 received messages (beacon, ping, or text) | UP/DN = scroll |

### Callsign

On first boot, each badge derives a unique callsign from its MAC address:

```
NCB-XXXXXX   (last 3 MAC bytes in hex — 16 million unique combinations)
```

`NCB` = Null City Badge. The callsign is written to NVS and used on every
subsequent boot until the user overrides it.

To edit: from the BEACON tab press UP. Use UP/DOWN to scroll the character
set (A–Z, 0–9, space, -), RIGHT to advance to the next position, and SELECT
to save. CANCEL discards all changes. The new callsign is immediately saved
to NVS and persists across power cycles.

### Packet Format

```cpp
enum MsgType : uint8_t { MSG_BEACON, MSG_PING, MSG_PONG, MSG_TEXT };

struct BeaconMsg {
    MsgType  type;       // 1 byte
    char     name[16];   // sender callsign
    uint8_t  mac[6];     // sender MAC (ground-truth identity)
    uint32_t counter;    // rolling send count
    char     text[32];   // payload for MSG_TEXT / MSG_PING
};                       // total: 59 bytes (well under 250-byte ESPNow limit)
```

MAC is the authoritative unique ID. The callsign is human-readable but not
trusted for uniqueness — the peer table keys on MAC.

### Peer Discovery

Every received packet calls `upsert_peer()`, which adds the sender to
`g_peers[10]` (keyed by MAC) or updates their name and timestamp. The PEERS
tab shows all discovered badges; RSSI updates via promiscuous mode each time
a new beacon arrives.

### Ping / RTT

From the PEERS tab, press SELECT on a peer and choose PING. The badge sends
a `MSG_PING` unicast; the receiving badge auto-replies with `MSG_PONG` in its
receive callback. Round-trip time is displayed on the BEACON tab (`RTT: N ms`).

> **Both badges must be on the ESPNow screen and have discovered each other
> (via at least one beacon exchange) before a ping will receive a reply.**
> If only one badge is in the ESPNow screen, the ping will show "waiting..."
> indefinitely until the second badge enters the screen and beacons.

### Messaging

From the PEERS tab, SELECT a peer → MESSAGE. A character picker opens (same
controls as the callsign editor). SELECT sends the text as a `MSG_TEXT`
unicast. The message appears in the recipient's INBOX tab.

### RSSI Proximity

Each peer in the PEERS tab shows:

- An 8-block signal bar (filled = strong, empty = weak)
- A proximity tier: **CLOSE** (> −50 dBm), **NEAR** (−50 to −70 dBm), **FAR** (< −70 dBm)
- The raw dBm reading

RSSI is captured by a promiscuous-mode callback that reads signal strength
from the 802.11 management frame header. It updates every time a new beacon
is received — **RSSI does not refresh between beacons**. To see a new reading
after moving badges, press SELECT on either badge to send a fresh beacon.

Typical readings for ESP32-S3 at 2.4 GHz:

| Distance | Approximate RSSI |
|---|---|
| ~1 inch | −30 to −40 dBm (6–8 bars, CLOSE) |
| ~1 metre | −50 to −60 dBm (4–5 bars, NEAR) |
| ~10 metres | −70 to −80 dBm (1–2 bars, FAR) |

### Encryption (AES-128)

All unicast packets (ping, pong, text) are encrypted with a shared AES-128
key. The keys are compiled into the firmware:

```cpp
static const uint8_t ESPNOW_PMK[] = "NullCity-Badge-1";  // Primary Master Key
static const uint8_t ESPNOW_LMK[] = "Badge-LinkKey-01";  // Link Key (per peer)
```

When a peer is added via `ensure_unicast_peer()`, `peer.encrypt = true` and
the LMK is set. Encrypted peers show a `[E]` indicator in the PEERS tab.

Broadcast beacons (`FF:FF:FF:FF:FF:FF`) are always unencrypted — ESPNow does
not support encryption on broadcast peers.

> **Testing encryption requires three badges.** Flash badge C with a
> *different* PMK (e.g. change `"NullCity-Badge-1"` to `"NullCity-Badge-X"`).
> Badge C will see the radio traffic but its receive callback will not fire
> for unicast packets from badges A and B — confirming the payload is
> opaque without the matching key.

---

## Extending the Firmware

All firmware lives in `firmware/src/main.cpp`. The patterns below cover the most
common extension points.

### Adding a menu item and new screen

The badge uses a state machine — every screen is an `AppState` enum value. Adding a
screen means adding a state, a draw function, and wiring them into the two dispatch
points (`dispatch_render` and `handle_buttons`).

1. **Add an enum value** to `AppState` (after `STATE_DISPLAY_TEST`, before `STATE_GUIDE`):
   ```cpp
   STATE_MY_SCREEN,
   ```

2. **Bump `MENU_COUNT`** and add the label to `MENU_LABELS[]`:
   ```cpp
   #define MENU_COUNT 8
   static const char* MENU_LABELS[MENU_COUNT] = {
       ...
       "8. My Screen",
   };
   ```
   The menu uses dynamic spacing — it already handles up to 7 items at 15 px each.
   For 8+ items you may need to reduce `spacing` and `start_y` inside `draw_menu()`.

3. **Write a draw function:**
   ```cpp
   static void draw_my_screen() {
       display.setFullWindow();
       display.firstPage();
       do {
           display.fillScreen(GxEPD_WHITE);
           display.setFont(&FreeMono9pt7b);
           display.setTextColor(GxEPD_BLACK);
           display.setCursor(4, 20);
           display.print("MY SCREEN");
       } while (display.nextPage());
       display.hibernate();
   }
   ```

4. **Add a case to `dispatch_render()`:**
   ```cpp
   case STATE_MY_SCREEN: draw_my_screen(); break;
   ```

5. **Add a case to `handle_buttons()`** inside the `STATE_MY_SCREEN` block:
   ```cpp
   case STATE_MY_SCREEN:
       if (pressed & BTN_CANCEL) { g_state = STATE_MENU; g_needs_redraw = true; }
       break;
   ```

6. **Wire SELECT in the `STATE_MENU` block** — add a `case 7:` (0-indexed cursor):
   ```cpp
   case 7: g_state = STATE_MY_SCREEN; break;
   ```

---

### Adding a guide page

Hardware Guide and Software Guide content lives in two `const GuideScreen` arrays
near the top of `main.cpp`. Each entry is a struct with a title and up to 6 lines
of text:

```cpp
struct GuideScreen {
    const char* title;
    const char* lines[6];   // nullptr terminates early
};
```

**Line length limit: 23 characters** — FreeMono9pt7b has an 11 px x-advance on a
264 px canvas, so 23 chars × 11 px = 253 px, keeping a small right margin.

To add or edit a page, find the `HW_GUIDE[]` or `SW_GUIDE[]` array and modify the
relevant entry:

```cpp
{ "MY NEW PAGE",
  { "First line here",
    "Second line here",
    "Third line",
    nullptr } },
```

The page counter footer (`N/10  LFT/RGT  CXL:back`) is drawn automatically by
`draw_guide()`. The count updates based on the array length passed when entering the
guide state.

---

### Adding an I²C peripheral

The I²C bus is already initialised in `setup()` with `Wire.begin(PIN_SDA, PIN_SCL)`
(SDA = GPIO10, SCL = GPIO9). Pull-up resistors are already on the board.

Add your device's init sequence after the existing `Wire.beginTransmission` calls
in `setup()`:

```cpp
Wire.beginTransmission(MY_DEVICE_ADDR);
Wire.write(CONFIG_REGISTER);
Wire.write(CONFIG_VALUE);
Wire.endTransmission();
```

Use the badge's built-in **I²C Scanner** (menu item 3) to confirm your device's
address appears on the bus before writing code.

---

### Adding an SPI peripheral

The SPI bus is started by GxEPD2 during `display.init()`. You can share it with
additional peripherals by picking a free GPIO from the top expansion header for CS.

```cpp
// After display.init() in setup()
SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
digitalWrite(MY_CS_PIN, LOW);
SPI.transfer(MY_COMMAND);
digitalWrite(MY_CS_PIN, HIGH);
SPI.endTransaction();
```

Free GPIOs on the top header: GPIO4, GPIO5, GPIO6, GPIO7, GPIO15, GPIO16, GPIO38, GPIO39.

---

### Debugging tips

**Serial monitor**

```bash
pio device monitor          # from the firmware/ directory
# or
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

**Display not updating**

- Confirm `display.hibernate()` is called after every `nextPage()` loop — skipping it
  leaves the SSD1680 in an undefined state.
- Confirm the driver class is `GxEPD2_270_GDEY027T91`. Using `GxEPD2_270` targets
  the IL91874 controller and silently resets the SSD1680 instead of refreshing it.

**Button not responding**

The TCA9534 returns an inverted byte (active LOW). Reading `0b11111110` means bit 0
is pulled low, which means LEFT (BTN_LEFT = `1 << 0`) is pressed. The `read_buttons()`
function handles the inversion — if you see unexpected button values, check that you
are reading the `pressed` delta (rising edge) not the raw register value.

**I²C device not found**

Use the built-in I²C Scan screen (menu item 3) to verify the address. If the device
is missing: check that GPIO18 (peripheral power rail) is HIGH, and that the device
is wired to GPIO9 (SCL) and GPIO10 (SDA) on the top expansion header.
