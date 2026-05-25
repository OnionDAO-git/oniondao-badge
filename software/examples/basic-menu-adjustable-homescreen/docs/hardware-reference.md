# NULL City Badge — Hardware Reference

Physical pin locations, connector pinouts, and component callouts captured
from the board photos. Use this as a soldering reference before any rework.

---

## Board Layout (Front)

```
┌─────────────────────────────────────────────────────────────────┐
│  [TOP GPIO HEADER]                                              │
│  GND VCC G38 G39 G16 G15 G07 G06 G05 G04                       │
│  ○   ○   ○   ○   ○   ○   ○   ○   ○   ○                         │
│                                                                 │
│  ┌──────────────────────┐    ┌────────────────┐  ┌──────────┐  │
│  │                      │    │   [SELECT]     │  │ CTR      │  │
│  │   2.7" E-PAPER       │    │   [CANCEL]     │  │ SDO      │  │
│  │   264 × 176 px       │    │   [RIGHT]      │  │ SLR      │  │
│  │   SSD1680            │    │   [UP] [DOWN]  │  │ SCK      │  │
│  │                      │    │   [LEFT]       │  │ SDL      │  │
│  └──────────────────────┘    └────────────────┘  │ VCC      │  │
│                                                  │ GND      │  │
│  ○   ○   ○   ○   ○   ○   ○   ○   ○   ○          ├──────────┤  │
│  [MODULE CONNECTOR — speaker / sub-GHz radio]   │ GND      │  │
│  (optional add-on modules attach here)           │ VCC      │  │
│                                                  │ TDD      │  │
│                                          [ANT]   │ SCK      │  │
│                                            ○     │ SCS      │  │
│                                                  │ SDO      │  │
│                                                  │ GDO      │  │
│                                                  └──────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Board Layout (Back)

```
┌─────────────────────────────────────────────────────────────────┐
│  [ON/OFF_PB1]  [ON/OFF_SW1]   [Display FPC / J4]               │
│                                                                 │
│  ┌─────────────────┐    ┌────────────────────────┐             │
│  │                 │    │  ESP32-S3-WROOM-1  (U1)│             │
│  │   LiPo Battery  │    └────────────────────────┘             │
│  │   (BAT / J2)    │                                            │
│  │                 │         [RST1]  [BOOT1]                    │
│  └─────────────────┘                                            │
│         │                                                       │
│         └── Battery connector: BAT+/BAT-                        │
│                                                                 │
│                         [USB-C  J1]  ← programming + power     │
│                                                                 │
│                           "DEVELOPED BY SKYRIZZ"               │
└─────────────────────────────────────────────────────────────────┘
```

---

## GPIO Expansion Header

A single 10-pin header runs along the top edge of the front face. It exposes
unused ESP32-S3 GPIOs and is the primary solder point for adding new
peripherals. The connector along the bottom edge is a module port, not free
GPIO — see the next section.

### Top Header (10 pins, left → right)

| Pin | Label | Notes |
|-----|-------|-------|
| 1 | GND | Ground |
| 2 | VCC | 3.3 V rail |
| 3 | G38 | GPIO38 |
| 4 | G39 | GPIO39 |
| 5 | G16 | GPIO16 |
| 6 | G15 | GPIO15 |
| 7 | G07 | GPIO7 |
| 8 | G06 | GPIO6 |
| 9 | G05 | GPIO5 |
| 10 | G04 | GPIO4 |

### Bottom Connector — Optional Module Port

This is **not** a free GPIO header. It is the attachment point for optional
add-on modules that stack on top of the badge:

| Module | Schematic |
|--------|-----------|
| CC1101 sub-GHz radio (433.92 MHz) | `pcb/CC1101_MOD.kicad_sch` |
| Speaker / audio mix | `pcb/SOUND_MOD.kicad_sch` |

Pin functions depend on which module is attached — refer to the relevant
KiCad schematic for the exact pinout. Without a module attached this
connector is unpopulated.

---

## Right-Side Expansion Connectors

Two 7-pin connectors on the right edge, used for plug-in modules
(CC1101 radio, LoRa, audio, SD card — schematics in `pcb/`).

### Top Connector (7 pins)

| Pin | Label | Likely function |
|-----|-------|-----------------|
| 1 | CTR | Control / chip enable |
| 2 | SDO | SPI MISO (data out from module) |
| 3 | SLR | SPI CLK |
| 4 | SCK | SPI SCK |
| 5 | SDL | SPI MOSI (data in to module) |
| 6 | VCC | 3.3 V |
| 7 | GND | Ground |

### Bottom Connector (7 pins)

| Pin | Label | Likely function |
|-----|-------|-----------------|
| 1 | GND | Ground |
| 2 | VCC | 3.3 V |
| 3 | TDD | Debug / JTAG TDI |
| 4 | SCK | SPI SCK |
| 5 | SCS | SPI CS (chip select) |
| 6 | SDO | SPI MISO |
| 7 | GDO | GDO (CC1101 interrupt / general data out) |

---

## Button Map

Physical silkscreen labels → firmware constants → TCA9534 bit positions.

| Silkscreen | Firmware constant | TCA9534 bit | PB number |
|------------|-------------------|-------------|-----------|
| SELECT | `BTN_SELECT` | bit 4 | PB5 |
| CANCEL | `BTN_CANCEL` | bit 5 | PB6 |
| RIGHT | `BTN_RIGHT` | bit 3 | PB4 |
| UP | `BTN_UP` | bit 2 | PB3 |
| DOWN | `BTN_DOWN` | bit 1 | PB2 |
| LEFT | `BTN_LEFT` | bit 0 | PB1 |

Physical layout (front face, right side):

```
        [SELECT]
        [CANCEL]
        [RIGHT]
  [UP]        [DOWN]
        [LEFT]
```

---

## Key Components (Back of Board)

| Ref | Part | Purpose |
|-----|------|---------|
| U1 | ESP32-S3-WROOM-1-N8R8 | Main MCU — 240 MHz, 8 MB flash, 8 MB PSRAM |
| U5 | TCA9534 | I²C I/O expander — reads 6 buttons @ 0x20 |
| U15 | ATECC608B | Crypto secure element @ 0x60 |
| U16 | CH340C | USB-UART bridge — connects to `/dev/ttyUSB0` |
| J1 | USB-C | Programming port + power input |
| J2 | BAT | LiPo battery connector |
| J4 | FPC | E-paper display flat flex connector |
| RST1 | Tactile switch | Hard reset the ESP32 |
| BOOT1 | Tactile switch | Hold during reset to enter download mode |
| ON/OFF_PB1 | Push button | Power on/off |
| ON/OFF_SW1 | Slide switch | Power on/off (alternate) |
| Q5 | Transistor | GPIO18 HIGH = enables peripheral 3.3 V rail |
| ANT | SMA/U.FL | RF antenna connector for CC1101 radio |

---

## Soldering Notes

**Adding a peripheral to the GPIO header:**
- Use the top header GPIOs (G04–G38) — they are not assigned in the current
  firmware and are free to use
- The bottom connector is reserved for optional modules (speaker / CC1101) —
  do not treat it as free GPIO
- 3.3 V logic only — do not connect 5 V signals directly to any GPIO
- I²C peripherals: SCL = GPIO9, SDA = GPIO10 (already on the bus; just add
  your device with its own pull-up if the bus doesn't already have one)
- SPI peripherals: SCK = GPIO11, MOSI = GPIO17; pick any free GPIO for CS

**Attaching an optional module:**
- The CC1101 sub-GHz radio and speaker/audio modules plug into the bottom
  connector on the front face — they stack on top of the badge
- Schematics: `pcb/CC1101_MOD.kicad_sch`, `pcb/SOUND_MOD.kicad_sch`
- GDO pin on the CC1101 module wires to an ESP32 GPIO for interrupt-driven
  packet reception

**Before soldering:**
- Disconnect the LiPo battery (unplug J2)
- Double-check orientation on any IC — U5 (TCA9534) and U15 (ATECC608B)
  are small SMD packages; consult `pcb/null-city-badge.kicad_sch` for
  pin 1 orientation
- The display FPC connector (J4) is ZIF (zero insertion force) — lift the
  locking tab before inserting or removing the ribbon cable
