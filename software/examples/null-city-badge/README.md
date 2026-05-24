# Start Here: NULL City Badge

A wearable hardware badge built on the ESP32-S3. It has a 2.7" e-paper display,
six buttons, a cryptographic secure element, and expansion ports for radio or
audio modules. This repo contains the Arduino/C++ firmware, a desktop art tool
for sending custom images to the badge, PCB design files, and documentation.

---

## Which doc should I read?

| I am… | Start here |
|---|---|
| New to the badge | [docs/beginner.md](docs/beginner.md) |
| Developer / hacking firmware | [docs/project.md](docs/project.md) |
| Learning C++ and hardware concepts | [docs/guide.md](docs/guide.md) |
| Adding hardware / soldering | [docs/hardware-reference.md](docs/hardware-reference.md) |
| Using Claude Code for badge work | [docs/claude-commands.md](docs/claude-commands.md) |

---

## Workshop

This repo is the foundation for the NULL City Badge workshop. If you have a badge
in hand, open [CONTRIBUTING.md](CONTRIBUTING.md) and start with Challenge 1.

Not at a workshop? The challenges work just as well at home. Instructors: see
[workshop/README.md](workshop/README.md) for the session timeline and setup checklist.

---

## Quick Start

```bash
# 1. Build and flash firmware
cd firmware
pio run --target upload

# 2. Open the art tool (send a custom home screen image)
python3 badge-art/badge_art.py

# 3. Read the serial monitor
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

Badge must be connected via USB on `/dev/ttyUSB0`. If upload fails, hold
**BOOT**, press **RST**, release **BOOT**, then retry.

---

## Directory Layout

```
null-city-badge-main/
│
├── README.md                  ← you are here
│
├── firmware/                  ← Arduino/PlatformIO project (the active firmware)
│   ├── platformio.ini         Board, upload port, library dependencies
│   ├── include/
│   │   └── badge_pins.h       All GPIO pin numbers in one place
│   └── src/
│       └── main.cpp           Entire firmware — state machine, all draw
│                              functions, button handling, deep sleep,
│                              serial image receiver, Hardware/Software guides
│
├── badge-art/                 ← Desktop tool: draw or import images, send to badge
│   ├── badge_art.py           Launch this (requires pillow + pyserial)
│   └── requirements.txt       pip install -r requirements.txt
│
├── docs/                      ← Written documentation
│   ├── project.md             Full project reference: hardware, firmware
│   │                          architecture, build steps, display notes,
│   │                          art tool usage, flash/restore instructions
│   ├── guide.md               Beginner hardware + C/C++ learning guide
│   │                          (what the on-badge Hardware/Software guides
│   │                          are based on)
│   └── claude-commands.md     Claude Code slash commands and AI workflow
│
├── pcb/                       ← KiCad schematic and PCB design files
│   ├── null-city-badge.kicad_sch   Main schematic
│   ├── null-city-badge.kicad_pcb   Board layout
│   ├── null-city-badge_top.png     Board photo (top)
│   ├── null-city-badge_btm.png     Board photo (bottom)
│   ├── CC1101_MOD.kicad_sch   CC1101 radio module sub-schematic
│   ├── LORA_MOD.kicad_sch     LoRa module sub-schematic
│   ├── SOUND_MOD.kicad_sch    Audio module sub-schematic
│   ├── SDCARD_MOD.kicad_sch   SD card module sub-schematic
│   ├── 3d/
│   │   └── null-city-badge.step   3D model of assembled board
│   └── production/
│       ├── null-city-badge.zip    Gerber files for fabrication
│       ├── netlist.ipc            IPC-2581 netlist for assembly
│       └── bom-list_*.xlsx        Bill of materials (three variants)
│
├── backups/
│   └── badge-original-firmware-backup.bin   Factory firmware image
│                                             (restore with esptool if needed)
│
└── badge-env/                 ← Python virtual environment (auto-created)
    └── ...                    Not committed; recreate with: python3 -m venv badge-env
```

---

## Further Reading

| Document | What it covers |
|---|---|
| [docs/board-explorer.html](docs/board-explorer.html) | Interactive board explorer — click components to learn what they do |
| [docs/project.md](docs/project.md) | Full project reference: firmware architecture, build/flash, display notes, art tool |
| [docs/hardware-reference.md](docs/hardware-reference.md) | Pin headers, connector pinouts, component callouts — soldering reference |
| [docs/guide.md](docs/guide.md) | Hardware and C/C++ concepts explained for beginners |
| [docs/claude-commands.md](docs/claude-commands.md) | Claude Code workflows for badge development |
