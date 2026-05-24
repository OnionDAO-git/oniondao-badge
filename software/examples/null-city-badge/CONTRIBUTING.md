# Contributing to the NULL City Badge

Whether you're at a workshop with a badge in hand or found this repo online —
welcome. This file covers both workshop challenges and general contribution guidelines.

---

## Workshop Participants

You're in the right place. Start by getting your badge working:

1. Read [docs/beginner.md](docs/beginner.md) — 5 minutes, no code, gets the badge running
2. Pick a challenge below based on how far you want to go
3. Flash your changes and see them on the badge

All challenges work with the badge as-is. No extra hardware needed for Challenges 1–3.

---

## Workshop Challenges

### Challenge 1 — Your Name on the Badge
**Skill: find and edit text strings | Time: ~20 min**

The badge shows a text home screen when no custom art has been sent. Change it to
display your name, your handle, and anything else you want.

**Where to look:** `firmware/src/main.cpp`, function `draw_home_screen()`.
Find the `display.print(...)` calls inside the "fallback" text block (the section
that runs when no NVS image is found). Change the strings.

**How to flash:**
```bash
cd firmware
pio run --target upload
```

**Done when:** The badge boots and shows your name instead of the default text.

---

### Challenge 2 — Add an "About Me" Screen
**Skill: add a new menu item and state | Time: ~30 min**

Add a new menu item that shows a screen with your name, badge number, and one fact
about yourself. This teaches the core pattern for extending the firmware.

**Follow the 6-step recipe in** `docs/project.md → Extending the Firmware → Adding a menu item`.

In brief:
1. Add `STATE_ABOUT_ME` to the `AppState` enum
2. Bump `MENU_COUNT` to 8 and add `"8. About Me"` to `MENU_LABELS[]`
3. Write a `draw_about_me()` function
4. Add `case STATE_ABOUT_ME: draw_about_me(); break;` to `dispatch_render()`
5. Add a `STATE_ABOUT_ME` block in `handle_buttons()` that returns to menu on CANCEL
6. Add `case 7: g_state = STATE_ABOUT_ME; break;` in the SELECT handler

**Done when:** Menu item 8 appears, selecting it shows your info, CANCEL returns to menu.

---

### Challenge 3 — Teach Something in the Guide
**Skill: edit structured content arrays | Time: ~20 min**

Add a new page to the Hardware Guide or Software Guide. It can be a concept you
already know, something you learned today, or whatever you want to share.

**Where to look:** `firmware/src/main.cpp`, the `HW_GUIDE[]` or `SW_GUIDE[]` arrays.

Each page is a struct:
```cpp
{ "YOUR TITLE HERE",        // shown as the page header
  { "Line one — 23 chars",  // max 23 characters per line
    "Line two",
    "Line three",
    nullptr } },            // nullptr ends the content early
```

**Hard limit: 23 characters per line.** FreeMono9pt7b at 11 px per character fills
253 px of the 264 px display width. Longer lines get clipped.

To add a new page, copy an existing entry, paste it at the end of the array
(before the closing `}`), and edit the content. Bump the array size `[10]` → `[11]`.

**Done when:** Your new page appears at the end of the guide and displays correctly.

---

### Challenge 4 — Hardware Expansion
**Skill: I²C peripheral integration | Time: 45+ min | Requires: I²C sensor**

Wire an I²C sensor (temperature, humidity, light, accelerometer — anything with an
I²C interface) to the top GPIO expansion header and display its reading on a new screen.

**Top header pinout (left → right):** GND, VCC (3.3V), G38, G39, G16, G15, G07, G06, G05, G04

**I²C wiring:** SCL → GPIO9 solder pad or use a breakout; SDA → GPIO10. The bus
already has pull-ups on the board.

**Code pattern:** Follow Challenge 2 for the menu item. Inside your draw function,
read from your sensor using `Wire.requestFrom()` / `Wire.read()` and print the value.

**Use the built-in I²C Scan (menu item 3)** to confirm your sensor's address before
writing code.

**Done when:** A new menu item shows a live sensor reading that updates on each visit.

---

## Ways to Contribute (all skill levels)

| Contribution | What it means |
|---|---|
| Fix a bug | Open an issue describing what happened, then submit a PR |
| Add a guide page | Edit `HW_GUIDE[]` or `SW_GUIDE[]` in `main.cpp` and submit a PR |
| Improve documentation | Edit any file in `docs/` — typos, clarity, accuracy all welcome |
| Add a module schematic | Add a KiCad sub-schematic in `pcb/` for a new expansion module |
| Port to a new badge | Open an issue to discuss — hardware variations can fork the firmware |

---

## Submitting a Change

1. Fork the repository
2. Create a branch: `git checkout -b your-name/what-you-changed`
3. Make your changes and test on real hardware if possible
4. Submit a pull request with a short description of what you changed and why

---

## Style Guide

- **Line length in guide screens:** 23 characters maximum (hard limit from font metrics)
- **Naming:** snake_case for functions and variables; `UPPER_CASE` for `#define` constants
- **Comments:** only when the *why* is non-obvious — don't describe what the code does
- **New states:** always wire CANCEL to return to `STATE_MENU` in `handle_buttons()`
- **Display:** always call `display.hibernate()` after every `firstPage()` / `nextPage()` loop
