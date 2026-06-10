# EPUB Reader

Read books on the badge's e-paper panel. LEFT/RIGHT turn pages, SELECT opens
a chapter menu, CANCEL bookmarks your spot and returns to Onion OS.

**Module variant:** none — runs on a bare badge.
**GPIOs touched:** none directly; it is a pure Onion OS Lua script using the
firmware's display (J4 socket) and button (`PBINT`/TCA9534) APIs.

The badge has no unzip or XML parser, so EPUBs are converted on your laptop
first: [`epub2badge.py`](epub2badge.py) (Python 3, stdlib only) extracts the
book text, reflows it for the panel, and emits ready-to-install Lua scripts —
each one is the book text plus the reader runtime from
[`reader_template.lua`](reader_template.lua). Big books are split into parts
(`alice-p1.lua`, `alice-p2.lua`, …) that fit the firmware's 256 KB script cap.

```
your-book.epub ──epub2badge.py──▶ out/alice-p1.lua  ──install──▶ badge
                                  out/alice-p2.lua
                                  out/manifest.json
```

A ready-to-run sample is committed at [`sample/raven.lua`](sample/raven.lua)
(Poe, *The Raven*, public domain).

## Convert a book

```sh
python3 epub2badge.py alice.epub -o out --slug alice --base-url http://192.168.1.50:8000
```

Plain-text books work too (`python3 epub2badge.py book.txt --title "My Book"`);
chapter headings are detected heuristically (`CHAPTER …`, roman numerals).

| Flag | Default | Notes |
|------|---------|-------|
| `-o/--out-dir` | `out` | where `.lua` parts + `manifest.json` land |
| `--slug` | from title | output base name, ≤10 chars |
| `--title` | from EPUB metadata | override the displayed title |
| `--rows` | `8` | text lines per page — see refresh note below |
| `--cols` | `23` | characters per line (23 is the panel max) |
| `--line-height` | `16` | pixels per text line |
| `--budget` | `170000` | max page-text bytes per part |
| `--base-url` | `http://BADGE-HOST:8000` | URL prefix baked into `manifest.json` |

## Install on the badge

Over WiFi with the manifest flow:

1. Serve the output folder from your laptop:
   `cd out && python3 -m http.server 8000`
2. Point the badge at it over serial (same WiFi network):
   `scripts-url http://<laptop-ip>:8000/manifest.json`
3. On the badge: home menu → **Scripts** → **Update Scripts**.
4. Select the book (e.g. `alice-p1.lua`) to start reading.

Alternatively publish the part files to the Onion server's Lua registry and
push them from the portal — each part stays under the 256 KB pushed-script
limit. The 1.5 MB SPIFFS partition holds roughly 8 parts at the default
budget; in the script explorer, LEFT deletes a highlighted script if you need
room (finished parts can always be re-downloaded).

## Controls

| Button | Reading | Chapter menu |
|--------|---------|--------------|
| RIGHT / DOWN | next page (hold to keep turning) | move cursor down |
| LEFT / UP | previous page (hold to keep turning) | move cursor up |
| SELECT | open chapter menu | jump to chapter |
| CANCEL | save bookmark, exit to Onion OS | back to reading |

The bookmark also auto-saves a couple of seconds after you stop turning
pages, so losing power mid-chapter loses nothing. Bookmarks live in NVS
(`bk_<bookid>_<part>`) and survive reboots, firmware updates, and
re-downloading the script.

## Why the layout looks the way it does

- **FreeMono9pt7b** (the firmware's `small` font) is monospaced: 11 px per
  character, ASCII 0x20–0x7E only. 264 px wide → **23 characters per line**.
  The converter transliterates typography to fit (curly quotes → straight,
  em-dash → `--`, accents stripped, anything unmappable → `?`, counted and
  reported at convert time).
- **8 lines per page is deliberate.** Onion OS diffs each frame and uses a
  fast ~400 ms partial refresh only while the dirty *bounding box* stays
  under 75 % of the panel; bigger changes get the flashy ~1.7 s full refresh.
  8 lines × 16 px keeps the text band at ~73 %, and the runtime draws the
  text and the footer as **two separate commits** so the page-number update
  doesn't drag the box over the cliff. Net result: page turns are two quick
  flicker-free partials, with the firmware's automatic ghost-clearing full
  refresh every ~15 turns. Pass `--rows 10` if you prefer denser pages and
  don't mind every turn being a full flash (the converter warns).
- Chapters come from the EPUB's NCX or EPUB3 nav TOC (fragment anchors
  resolved into the text flow), falling back to `<h1>/<h2>` headings, then
  file boundaries. Each chapter starts on a fresh page.

## Tests

Desktop tests drive the real generated script through a mock of the `onion`
API ([`test/onion_stub.lua`](test/onion_stub.lua)) — page turns, bookmark
save/resume, chapter jumps, hold-to-repeat, boundary clamps, and that every
drawn line fits 23×8 ASCII:

```sh
python3 epub2badge.py alice.epub -o /tmp/out --slug alice
lua test/run_tests.lua /tmp/out/alice-p1.lua
luac -p /tmp/out/*.lua          # syntax check, matches the badge's Lua 5.5
```

## Known limits

- Images, tables, and per-word styling are dropped — it's a 1-bit 264×176
  panel; you get clean reflowed text.
- Non-Latin scripts (CJK, Cyrillic, Greek…) can't be transliterated to the
  font's ASCII range and come out as `?`.
- DRM'd EPUBs are not supported.
- Requires an Onion OS build with the deferred-frame display API
  (`display_begin`/`display_commit`) and the Lua kv store — current
  `mods/onion-os` main has both.
