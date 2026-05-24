#pragma once

// ── Power ─────────────────────────────────────────────────────────────────────
#define PIN_PWR         18  // HIGH = peripheral rail on (transistor Q5)
#define PIN_SE_EN        8  // HIGH = ATECC608B enabled

// ── I²C ───────────────────────────────────────────────────────────────────────
#define PIN_SCL          9
#define PIN_SDA         10

// ── E-ink display (J4, SSD1680, TWE0270NQ23-AO  264×176) ─────────────────────
#define PIN_EPD_MOSI    17  // SDI
#define PIN_EPD_SCK     11
#define PIN_EPD_CS      12  // active LOW
#define PIN_EPD_DC      13  // LOW=cmd  HIGH=data
#define PIN_EPD_RST     14  // active LOW pulse
#define PIN_EPD_BUSY    21  // HIGH while busy

// ── Buttons (TCA9534 @ 0x20, active LOW, PB1-PB6 = bits 0-5) ─────────────────
#define PIN_BTN_IRQ      1  // falling edge on any button press

// ── Secure element (ATECC608B @ 0x60) ────────────────────────────────────────
// Address: 0x60  |  enable: PIN_SE_EN above
