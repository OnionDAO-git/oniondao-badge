# Basic Menu with Adjustable Homescreen

Ready-to-flash firmware for the OnionDAO badge. Flash it once and you get a navigable menu and a homescreen you can update from your computer anytime.

Built with [Claude Code](https://claude.ai/code). Use this as your base layer, then customize with your chosen AI.

---

> **Buttons will not respond on fresh hardware.** The badge ships without firmware — nothing happens when you press buttons until you complete Step 3.

---

## What You Need

- OnionDAO badge + USB-C cable
- LiPo battery *(optional — the badge runs on USB power alone)*
- [PlatformIO](https://platformio.org/) for flashing
- Python 3 + pip for the art tool

---

## Step 1 — Download

```bash
git clone https://github.com/OnionDAO-git/oniondao-badge.git
cd oniondao-badge/software/examples/basic-menu-adjustable-homescreen
```

No git? Download the ZIP from the repo page and extract it.

---

## Step 2 — Connect the Battery (optional)

Flip the board over. The LiPo connector is labeled **BAT / J2**.

- Plug the battery in for portable use — the badge also runs fine from USB alone
- Always unplug the battery before any soldering or rework
- Flip the **ON/OFF** slide switch on the back to power on

---

## Step 3 — Flash the Firmware

Connect the badge via USB-C, then:

```bash
cd firmware
pio run --target upload
```

If the upload fails to connect:
1. Hold **BOOT** (back of board)
2. Press and release **RST**
3. Release **BOOT**
4. Run the upload command again

After a successful flash the screen flashes white and shows the home screen. **Buttons are now active.**

---

## Step 4 — Navigate the Menu

Press any button from the home screen to open the menu.

| Button | Action |
|--------|--------|
| UP / DOWN | Move the cursor |
| SELECT | Open the selected item |
| CANCEL | Go back |
| LEFT / RIGHT | Flip pages (inside guide screens) |

| # | Screen | What it shows |
|---|--------|---------------|
| 1 | System Info | Chip model, clock speed, memory |
| 2 | Button Test | Watch button presses light up in real time |
| 3 | I2C Scanner | Every chip found on the internal bus |
| 4 | RNG / Crypto | 16 random bytes from the hardware crypto chip |
| 5 | Display Test | Four screen patterns to confirm the display works |
| 6 | Hardware Guide | 10 short lessons on the physical components |
| 7 | Software Guide | 10 short lessons on the C++ firmware |

After 60 seconds of no activity the badge sleeps automatically. The image stays on screen with no power — press any button to wake it.

---

## Step 5 — Set a Custom Homescreen

**Install dependencies (one time):**
```bash
pip install pillow pyserial
```

**Run the art tool:**
```bash
python3 badge-art/badge_art.py
```

- Draw with left-click, erase with right-click
- Import any photo or PNG with **Open Image** — auto-resized and converted
- Select the serial port and click **Send to Badge**

The image saves to internal flash and persists through sleep and power cycles.

> The badge must be awake to receive an image. If it slept, press any button first, then send within 60 seconds.

---

## Go Further

Drop `firmware/src/main.cpp` into your AI of choice and ask it to add new screens or menu items. This version was built using **[Claude Code](https://claude.ai/code)**.

| File | Purpose |
|------|---------|
| `firmware/src/main.cpp` | All firmware — menus, buttons, image receiver |
| `firmware/include/badge_pins.h` | Every GPIO pin number in one place |
| `firmware/platformio.ini` | Build config and library dependencies |
| `badge-art/badge_art.py` | Art tool — draw, import, and send images |
| `docs/project.md` | Full firmware reference and extension patterns |
| `docs/guide.md` | Hardware and C++ concepts for beginners |
| `docs/hardware-reference.md` | Pinouts, connectors, soldering notes |
