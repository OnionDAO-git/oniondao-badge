# Onion OS Scripts

## Build and Flash Helper

`build-flash.sh` builds Onion OS and flashes the first attached ESP32-S3 board
it can find:

```sh
scripts/build-flash.sh
```

Run it from the Onion OS project directory. Use `--monitor` to open the serial
monitor after flashing, or `--port /dev/cu.usbserial-10` to choose a specific
serial port.

The helper auto-loads common ESP-IDF installs, including
`~/.espressif/v5.5.4/esp-idf/export.sh`. Set
`IDF_EXPORT=/path/to/esp-idf/export.sh` if your ESP-IDF install lives somewhere
else.

## Serial Monitor Helper

`monitor-serial.sh` searches common serial ports, detects ESP board types, lets
you select the matching device, and opens `idf.py monitor`:

```sh
scripts/monitor-serial.sh
```

It defaults to ESP32-S3 badges and 115200 baud. Use `--board any` to show every
connected serial port, `--list` to print detected ports without opening the
monitor, or `--port /dev/cu.usbserial-210` to skip selection. Press `Ctrl-C` to
stop the monitor and close any child monitor process.

## Lua Scripts

Example Lua scripts in this directory are meant to be copied into the Onion OS
script registry or served through the script manifest.

## E-paper Demo

`eink-demo.lua` exercises the Onion SDK e-paper helpers by drawing text, a
border, and a separator line.

## ESP-NOW Beacon

`espnow-beacon.lua` enables ESP-NOW, broadcasts a short badge identity packet,
and displays the first received packet if another badge replies.

## Check-In Range

`checkin-range.lua` listens for Onion check-in beacon advertise packets and
shows whether the current badge RSSI meets the beacon's advertised threshold.
This is useful for testing a tight beacon threshold, such as `-50 dBm`, before
using the normal check-in prompt. Press `CANCEL` to exit back to Onion OS.

## Tamagonion

`tamagonion.lua` is a Tamagotchi-style virtual pet that runs entirely as a Lua
script. The onion creature is drawn natively using `display_begin/commit` and the
vector drawing primitives (`display_circle`, `display_triangle`,
`display_round_rect`, `display_pixel`) — no sprite assets required.

The pet grows through five life stages (EGG → BABY → CHILD → TEEN → ADULT),
has hunger/happiness/energy/health stats that decay over time, can be fed, played
with, put to sleep, and medicated, and can get sick or die. State is saved to NVS
(survives reboots) via `onion.kv_set/kv_get`. Timing uses `onion.millis()` so
decay runs at a real rate regardless of SNTP availability.

Controls:

- `LEFT` / `RIGHT` (or `UP` / `DOWN`): cycle FEED / PLAY / SLEEP / MED action
- `SELECT`: perform the selected action
- `CANCEL`: save and return to Onion OS

On firmware without the vector-primitive additions, `tamagonion.lua` falls back
to a text-only ASCII-bar mode so the game logic still runs.

**Requires:** Path B firmware (`display_begin`, `display_commit`, `display_circle`,
`display_triangle`, `display_round_rect`, `display_pixel`, `kv_set`, `kv_get`,
`millis`). All additions are in `main/main.cpp` and documented in
`docs/03-api-improvement.md`.

## Onion Bookshelf

`onion-bookshelf/` is a plain-text book reader: a main menu (Library / About /
Help), a multi-book library, and paged reading with per-book bookmarks, all in
one Lua script. Books are authored as `.txt` files and folded into an
installable `onion-bookshelf.lua` by `build.py` (no EPUB, no firmware build). A
curated onion-themed shelf ships by default. See
[`onion-bookshelf/README.md`](onion-bookshelf/README.md).

Controls:

- `UP` / `DOWN`: move in the menu / library
- `SELECT`: open
- `LEFT` / `RIGHT`: turn pages while reading
- `CANCEL`: step back (and exit from the menu)

## Image Browser

`image-browser.lua` browses every downloaded image stored in SPIFFS as
`/images_*.pbm` or `/images_*.bmp`.

Controls:

- `UP` / `LEFT`: previous image
- `DOWN` / `RIGHT`: next image
- `SELECT`: redraw current image
- `CANCEL`: return to Onion OS

To install it through a manifest, serve this script and the image assets:

```json
{
  "scripts": [
    {
      "name": "eink-demo.lua",
      "url": "https://example.com/eink-demo.lua"
    },
    {
      "name": "espnow-beacon.lua",
      "url": "https://example.com/espnow-beacon.lua"
    },
    {
      "name": "checkin-range.lua",
      "url": "https://example.com/checkin-range.lua"
    },
    {
      "name": "image-browser.lua",
      "url": "https://example.com/image-browser.lua"
    }
  ],
  "images": [
    {
      "name": "poster.pbm",
      "url": "https://example.com/poster.pbm"
    }
  ]
}
```
