# Setting Up VS Code + ESP-IDF to Build & Flash the Badge

This guide takes you from a fresh machine to flashing badge firmware over USB,
using **Visual Studio Code** and the official **ESP-IDF** extension. It applies
to every firmware project in this repo (`software/examples/…` and
`software/mods/…`).

> **At a glance**
> 1. Install VS Code + the **ESP-IDF** extension
> 2. Install the toolchain — **ESP-IDF v5.5.x** (⚠️ *not* 6.0 or newer)
> 3. Open a firmware **subfolder** (not the repo root)
> 4. Select your serial port → **Build → Flash → Monitor**

---

## ⚠️ Which version? ESP-IDF v5.5.x — not 6.0+

The firmware uses the Arduino core (GxEPD2 display library, etc.) compiled as an
ESP-IDF component. The Arduino core is **version-locked to the ESP-IDF it was
built against**:

| arduino-esp32 | requires ESP-IDF |
|---------------|------------------|
| **3.3.8** (used here) | **5.5.4** |
| 3.3.x | 5.5.x |

There is **no released Arduino core for ESP-IDF 6.0+**, so a 6.x toolchain will
fail to build this firmware. **Install ESP-IDF v5.5.4** (or the latest v5.5.x).
The Arduino core itself is downloaded automatically on the first build — you do
not install it separately.

---

## 1. Install VS Code and the ESP-IDF extension

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Open the **Extensions** sidebar (`Cmd/Ctrl+Shift+X`), search for
   **`ESP-IDF`**, and install the one published by **Espressif Systems**
   (`espressif.esp-idf-extension`).

> **Drivers:** the badge talks to your computer through a **CH340** USB-serial
> chip. macOS 11+ and modern Linux include the driver; on Windows (or older
> macOS) install the [CH340 driver](https://www.wch-ic.com/products/CH340.html)
> if no serial port appears later. On Linux, add yourself to the `dialout`
> group so you can access the port without `sudo`:
> `sudo usermod -aG dialout $USER` (log out/in afterward).

---

## 2. Install the ESP-IDF toolchain (v5.5.x)

> **⚠️ macOS / Linux: install the QEMU runtime libraries *first*.** Before
> downloading v5.5.4, the installer checks for five QEMU dependency libraries.
> If even one is missing the whole install aborts — and on macOS the error is
> misleadingly shown as `Failed to check QEMU prerequisites: %{error}` (the real
> reason, e.g. *"libslirp not satisfied"*, is swallowed). Install all five up
> front so the check passes:
>
> **macOS (Homebrew):**
> ```bash
>brew install libgcrypt glib pixman sdl2 libslirp dfu-util cmake python
> ```
>
> **Linux (Debian/Ubuntu):**
> ```bash
> sudo apt-get install -y libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0
> ```
>
> **Windows:** nothing to do — the installer handles its own dependencies.

1. Open the Command Palette (`Cmd/Ctrl+Shift+P`) → **`ESP-IDF: Configure ESP-IDF
   Extension`**.
2. Choose **EXPRESS** (the simplest path; it downloads ESP-IDF and all tools).
3. **Download server:** *GitHub* (or *Espressif* if GitHub is slow for you).
4. **ESP-IDF version:** select **`v5.5.4`** (or the newest `v5.5.x`).
   **Do not pick v6.0 or newer.**
5. Leave the install directories at their defaults
   (`~/.espressif` on macOS/Linux, `%USERPROFILE%\.espressif` on Windows).
6. Click **Install** and wait — it fetches the Xtensa toolchain and Python
   environment (a few minutes / ~1–2 GB). When it finishes you'll see
   *"All settings have been configured."*

> **Already have v5.5.x installed from the command line?** Choose **USE EXISTING
> SETUP** in the wizard instead and point it at your `…/esp-idf` folder
> (e.g. `~/.espressif/v5.5.4/esp-idf`).

---

## 3. Open a firmware project

> **Important:** open the **project subfolder**, *not* the repository root. The
> repo root has no `CMakeLists.txt`, so the extension will report
> *"CMakeLists.txt not found in project directory"* and target selection will
> fail. Each firmware app is its own ESP-IDF project.

**File → Open Folder…** and pick one of:

| Open this folder | Firmware |
|------------------|----------|
| `software/examples/basic-menu-adjustable-homescreen/firmware` | Menu + adjustable homescreen |
| `software/mods/tamagotchi` | Virtual-pet mod |

Each project folder already contains `CMakeLists.txt`, `main/`,
`sdkconfig.defaults`, and `partitions.csv`.

### Target is preconfigured

The chip target (**ESP32-S3**), 8 MB flash, octal PSRAM (tamagotchi), and the
partition table are all set in `sdkconfig.defaults` — you **do not need to run
"Set Espressif device target."** If the extension ever prompts you, choose
**`esp32s3`**.

---

## 4. Connect the badge and select the serial port

1. Plug the badge into your computer with a **USB-C** cable and power it on.
2. Command Palette → **`ESP-IDF: Select port to use`** (or click the **plug**
   icon in the bottom status bar) and pick the badge's port:

   | OS | Looks like |
   |----|-----------|
   | macOS | `/dev/tty.usbserial-XXXX` (or `/dev/tty.wchusbserialXXXX`) |
   | Linux | `/dev/ttyUSB0` |
   | Windows | `COM3`, `COM4`, … |

   No port listed? Re-seat the cable (use a *data* cable, not charge-only) and
   confirm the CH340 driver is installed (see §1).

---

## 5. Build, Flash, and Monitor

All actions are on the **blue status bar** at the bottom of VS Code, and also in
the Command Palette. Left-to-right, the relevant status-bar icons are:

| Icon | Action | Command Palette equivalent |
|------|--------|----------------------------|
| 🔌 plug | Select port | `ESP-IDF: Select port to use` |
| 🛠️ wrench/chip | Set target | `ESP-IDF: Set Espressif device target` |
| ⚙️ gear | SDK config editor (menuconfig) | `ESP-IDF: SDK Configuration Editor` |
| 🛢️ cylinder | **Build** | `ESP-IDF: Build your project` |
| ⚡ bolt | **Flash** | `ESP-IDF: Flash your project` |
| 🖥️ monitor | **Monitor** (serial output) | `ESP-IDF: Monitor your device` |
| 🔥 flame | **Build + Flash + Monitor** (do everything) | `ESP-IDF: Build, Flash and start a monitor` |

**Recommended first run:**

1. Click **Build** (🛢️). The **first build downloads the Arduino core**
   (`arduino-esp32`) and compiles it — this takes several minutes and a lot of
   output. Later builds are incremental and fast.
2. Make sure the **flash method** is **UART** (status-bar item; this badge
   flashes over the CH340 UART).
3. Click **Build, Flash and Monitor** (🔥). On success the badge reboots into
   the new firmware and the serial monitor shows its output.
   - *Exit the monitor* with `Ctrl+]`.

> **Auto-reset:** the badge's CH340 + DTR/RTS circuit puts the ESP32-S3 into
> download mode for you — you normally don't touch any buttons. If flashing
> can't connect, force download mode: **hold BOOT, tap RST, release BOOT**, then
> flash again.

---

## Command-line alternative

Prefer a terminal? Open the **ESP-IDF Terminal** (Command Palette →
`ESP-IDF: Open ESP-IDF Terminal`), which has the environment pre-loaded. Or load
it manually in any shell:

```bash
. $IDF_PATH/export.sh          # or run the alias the installer prints
cd software/mods/tamagotchi    # a project subfolder
idf.py build
idf.py -p /dev/tty.usbserial-10 flash monitor   # use your port
```

---

## Troubleshooting

| Symptom | Cause / Fix |
|---------|-------------|
| **`Failed to check QEMU prerequisites: %{error}`** during v5.5.4 install (macOS/Linux) | A QEMU runtime lib is missing (often `libslirp`); `%{error}` hides the real one. Install all five prereqs (§2), then **Try Again**. |
| **`CMakeLists.txt not found in project directory`** / *Set target fails with exit code 2* | You opened the **repo root**. Open a firmware **subfolder** instead (§3). |
| Build fails with errors about **removed drivers** / Arduino core won't resolve | Wrong ESP-IDF version. You **must** use **v5.5.x**, not 6.0+ (see top). |
| First build hangs at "Solving dependencies" / downloading | It's fetching the Arduino core from the component registry. Needs internet; just wait. |
| No serial port in the list | Bad/charge-only cable, or missing **CH340 driver** (§1). Linux: join `dialout`. |
| `Failed to connect … No serial data received` while flashing | Force download mode: hold **BOOT**, tap **RST**, release **BOOT**, flash again. |
| PSRAM not detected (tamagotchi) | Already configured for **Octal (OPI)** mode in `sdkconfig.defaults`; ensure you didn't override it in the SDK config editor. |

---

*Once flashed, head back to the firmware's own README for what the app does and
how to use it.*
