# Beginner's Guide — Hardware and C for the NULL City Badge

This guide explains what is actually happening when you build and run firmware
on the badge. No prior hardware or C experience assumed.

---

## 1. What Is a Microcontroller?

Your laptop runs an operating system (Linux), which manages memory, files,
networking, and hundreds of processes at once. It has gigabytes of RAM and a
CPU designed for general-purpose computing.

A **microcontroller** (MCU) is a completely different animal. It is a single
chip that contains a CPU, a small amount of RAM, and flash storage — all
integrated together, with no operating system. The ESP32-S3 on your badge has:

- **CPU:** two cores running at up to 240 MHz
- **RAM:** 512 KB on-chip + 8 MB external PSRAM
- **Flash:** 8 MB for storing your program
- **Peripherals:** SPI, I²C, UART, PWM, ADC — all built into the same chip

When the badge powers on, the CPU starts executing your program immediately
from address 0 in flash. There is no boot menu, no file system to load, no
kernel — just your code running directly on the hardware.

### Why C/C++ instead of Python?

MicroPython (which this badge started with) does run Python on the MCU, but
it has overhead: an interpreter, a garbage collector, and slower execution.

C and C++ compile directly to machine instructions. The resulting binary is
smaller, runs faster, and gives you precise control over every hardware
register. For a badge that needs to drive an e-paper display with specific
timing, talk to multiple SPI/I²C devices, and respond instantly to button
presses, C is the natural choice.

Arduino is not a language — it is a framework written in C++ that hides a lot
of the chip-specific setup (clock configuration, interrupt tables, peripheral
initialization) so you can focus on your application logic. The functions
`setup()` and `loop()` are Arduino conventions; the compiler wraps them in a
standard `main()` that the chip actually executes.

---

## 2. GPIO — General Purpose Input/Output

Every pin on the ESP32-S3 that isn't dedicated to power is a **GPIO pin**.
You control a GPIO pin entirely in software:

```cpp
// Set GPIO18 as an output, then drive it HIGH (3.3 V)
pinMode(18, OUTPUT);
digitalWrite(18, HIGH);

// Set GPIO21 as an input, then read its voltage
pinMode(21, INPUT);
int level = digitalRead(21);  // returns 0 or 1
```

On your badge, GPIO18 controls a transistor (Q5) that switches power to the
display and I²C devices. Before touching any peripheral, you must set GPIO18
HIGH — otherwise those circuits have no power and nothing responds.

### Voltage levels

The ESP32-S3 is a **3.3 V device**. HIGH = 3.3 V, LOW = 0 V. This is
important: connecting a 5 V signal directly to an ESP32 pin will damage it.
The CH340C USB chip on the badge handles the level translation between your
laptop's USB (5 V) and the ESP32 (3.3 V).

---

## 3. Communication Protocols — How Chips Talk to Each Other

A microcontroller rarely works alone. It needs to talk to sensors, displays,
and other ICs. Two protocols handle almost all of this on your badge: SPI and
I²C.

### SPI — Serial Peripheral Interface

SPI is fast. It uses four wires:

| Wire | Direction | Purpose |
|---|---|---|
| SCK | MCU → device | Clock — a square wave that times every bit |
| MOSI | MCU → device | Master Out Slave In — data going to the device |
| MISO | device → MCU | Master In Slave Out — data coming back (not used by display) |
| CS | MCU → device | Chip Select — pulled LOW to address this specific device |

Data is transferred one bit per clock pulse. At 4 MHz (the badge's SPI
speed), you can transfer 4 million bits per second. The 5808-byte display
image transfers in about 12 milliseconds.

The e-paper display uses SPI with an extra wire: **DC** (Data/Command).
When DC is LOW, the MCU is sending a command (an instruction like "start
a refresh"). When DC is HIGH, the MCU is sending data (pixel values).
The display controller uses this to know how to interpret each byte.

```
Sending the "write RAM" command (0x24), then pixel data:

CS  ────┐                         ┌────
        │_________________________│
DC  ────┐   ┌────────────────────────
        │___│
MOSI ═══╪═══╪════════════════════════
        0x24  0xFF 0xFF 0x00 0xFF ...
             (pixel data bytes)
```

### I²C — Inter-Integrated Circuit

I²C is slower than SPI but uses only **two wires** for potentially dozens of
devices:

| Wire | Purpose |
|---|---|
| SCL | Clock |
| SDA | Data (bidirectional) |

Every device on I²C has a 7-bit **address** baked into its silicon. The MCU
calls out an address at the start of every transaction, and only the device
with that address responds. Your badge has two I²C devices:

- **TCA9534** — 8-bit I/O expander for the 6 buttons, address `0x20`
- **ATECC608B** — cryptographic secure element, address `0x60`

```cpp
// Read one byte from the TCA9534 input register (register 0x00)
Wire.beginTransmission(0x20);   // address the TCA9534
Wire.write(0x00);               // tell it we want register 0x00
Wire.endTransmission();
Wire.requestFrom(0x20, 1);      // ask for 1 byte back
uint8_t buttons = Wire.read();  // that byte has one bit per button
```

The reason I²C runs at 100 kHz instead of the possible 400 kHz is the
ATECC608B — it requires the slower speed during its startup sequence.

---

## 4. How the Buttons Work

The six buttons (PB1–PB6) are not wired directly to GPIO pins. Instead they
connect to the TCA9534 I/O expander. This is common in hardware design to
save GPIO pins — one I²C device handles all six buttons using just SCL and
SDA.

Each button connects one TCA9534 pin to ground. The TCA9534 has internal
pull-up resistors that hold each pin at HIGH (1) when no button is pressed.
When you press a button, it pulls the pin to LOW (0).

This is called **active LOW**: the signal that means "button pressed" is a 0,
not a 1. The firmware inverts this:

```cpp
uint8_t raw = Wire.read();          // e.g. 0b11111011 = button 3 pressed
return (~raw) & 0x3F;               // invert + mask to 6 bits
                                    // result: 0b00000100 = bit 2 set = PB3
```

`~raw` flips all bits (1→0, 0→1). `& 0x3F` masks off the top two bits
since we only have 6 buttons. The result is a bitmask where a **1** means
"this button is pressed."

---

## 5. How E-Paper Displays Work

An e-paper (electronic paper) display is fundamentally different from an LCD
or OLED screen.

**LCD/OLED:** requires constant power to maintain the image. Turn off power,
image disappears immediately.

**E-paper:** tiny capsules of electrically charged black and white particles
are embedded in the display. An electric field pushes particles to the front
or back of each cell. Once positioned, the particles stay there with **zero
power**. The image persists indefinitely without electricity.

This is why your badge display kept showing its old image even after you
switched from MicroPython to Arduino — the physical particles were still in
position.

### The refresh process

Refreshing an e-paper display is slow (~1.5 seconds) and involves several
steps:

1. **Power on** the display's internal voltage booster
2. **Write pixel data** to the controller's RAM (the SSD1680 chip)
3. **Trigger a refresh** — the controller drives high-voltage waveforms through
   every row and column to move the particles
4. **Wait for BUSY** — GPIO21 stays HIGH while the refresh is happening;
   the firmware must wait until it goes LOW before sending more commands
5. **Power off** the voltage booster
6. **Hibernate** — the controller enters deep sleep, drawing microamps

The BUSY pin is critical. The SSD1680 is physically moving charged particles
through a viscous medium. If you send another command before it finishes,
you corrupt the display state and it may show garbage or nothing.

```cpp
// GxEPD2 handles all of this internally, but this is the concept:
display.firstPage();
do {
    display.fillScreen(GxEPD_WHITE);     // fill internal page buffer
    display.print("Hello");              // draw into buffer
} while (display.nextPage());            // triggers refresh, waits for BUSY
display.hibernate();                     // sleep
```

### Why 1 bit per pixel?

E-paper displays are black and white — each pixel is either fully black or
fully white (unlike a grey LCD). So each pixel needs only **1 bit** of
storage: 0 or 1. For a 264×176 display:

```
264 × 176 = 46,464 pixels
46,464 ÷ 8 = 5,808 bytes
```

5808 bytes is the exact size of the image the art tool sends. Each byte holds
8 pixels, packed MSB (most significant bit) first — the leftmost pixel is
stored in bit 7 of each byte.

---

## 6. C Fundamentals for This Firmware

### Variables and types

C requires you to declare the exact type of every variable. The types you
see most in embedded code:

```cpp
uint8_t   // unsigned 8-bit integer:  0 to 255
uint16_t  // unsigned 16-bit integer: 0 to 65535
uint32_t  // unsigned 32-bit integer: 0 to 4,294,967,295
int       // signed integer (size depends on platform, usually 32-bit)
bool      // true or false
```

The `uint` prefix means unsigned (no negative numbers). The number is the bit
width. On an MCU, you often care about exact sizes because you are mapping
values directly to hardware registers that are a specific number of bits wide.

### Pointers and arrays

A **pointer** is a variable that holds a memory address. In embedded code,
pointers let you pass large buffers around without copying them, and let you
work directly with hardware memory addresses.

```cpp
uint8_t img_buf[5808];          // array of 5808 bytes in RAM

// display.drawBitmap expects a pointer to the first byte
display.drawBitmap(0, 0, img_buf, 264, 176, GxEPD_BLACK, GxEPD_WHITE);
//                          ^^^^^
//                          img_buf decays to a pointer to its first element
```

When you write `img_buf` without brackets, C automatically treats it as a
pointer to the first element. This is called "array decay."

### Bit manipulation

The badge firmware uses bit operations constantly because hardware registers
are controlled bit by bit.

```cpp
uint8_t raw = 0b11111011;   // TCA9534 reading: button 3 pressed (bit 2 = 0)

// Test if bit 2 is set:
if (raw & (1 << 2)) { ... }     // (1 << 2) = 0b00000100

// Invert all bits:
uint8_t inverted = ~raw;        // 0b00000100 — now 1 means "pressed"

// Mask to the lower 6 bits:
uint8_t result = inverted & 0x3F;   // 0x3F = 0b00111111
```

`<<` shifts bits left. `1 << 2` shifts the value 1 two positions left,
producing a mask with only bit 2 set. `&` (AND) tests bits. `|` (OR) sets
bits. `~` inverts all bits.

### Static variables

In the firmware, `check_serial_image()` uses a `static` local variable:

```cpp
void check_serial_image() {
    static uint8_t hdr[4] = {0};
    ...
}
```

A normal local variable is destroyed when the function returns. A `static`
local variable persists between calls — the array keeps its value from one
call to the next. This is how the function remembers the last few bytes it
saw, so it can detect the `IMG:` magic header across multiple loop iterations.

### `#define` constants

```cpp
#define IMG_SIZE (264 * 176 / 8)   // 5808
```

`#define` is a **preprocessor directive** — before compilation, the
preprocessor replaces every occurrence of `IMG_SIZE` in the source with the
literal text `(264 * 176 / 8)`. The compiler then evaluates this as a
constant expression. No memory is used; it is purely a compile-time
substitution.

### NVS — Non-Volatile Storage

Flash memory is fast to read but designed to be written in large blocks (sectors
of 4096 bytes). Writing arbitrary key-value data directly to flash is tricky
and would wear out sectors quickly. ESP32 solves this with **NVS** (Non-Volatile
Storage): a driver that manages wear-levelling, atomic writes, and a simple
key-value API.

The Arduino wrapper is `Preferences`:

```cpp
#include <Preferences.h>

Preferences prefs;
prefs.begin("badge", false);              // open namespace "badge", read-write
prefs.putBytes("homescreen", buf, 5808);  // store 5808 bytes under key "homescreen"
prefs.end();

prefs.begin("badge", true);              // open read-only
size_t len = prefs.getBytesLength("homescreen");
prefs.getBytes("homescreen", buf, len);
prefs.end();
```

The badge uses NVS to persist the custom home screen image across power cycles.
The NVS partition lives in a dedicated area of flash and survives firmware
uploads (it is not erased by `pio run --target upload`).

---

## 7. The Arduino Execution Model

Arduino firmware has two required functions:

```cpp
void setup() {
    // Runs once, at power-on or after reset.
    // Initialize everything here.
}

void loop() {
    // Runs repeatedly, forever, after setup() finishes.
    // This is your main program.
}
```

Under the hood, the Arduino framework generates a `main()` function that
looks like this:

```cpp
int main() {
    initArduino();    // chip-level setup (clocks, interrupts, etc.)
    setup();          // your setup
    while (true) {
        loop();       // your loop, called over and over
    }
}
```

There is no scheduler, no sleep between calls — `loop()` is called as fast as
the CPU can execute it. If `loop()` takes 2 seconds (e.g., waiting for a
display refresh), nothing else runs during those 2 seconds. This is called
**cooperative / bare-metal** execution, as opposed to an RTOS (real-time
operating system) where multiple tasks can run concurrently.

### Implications for the badge

In `loop()`, `check_serial_image()` runs first. When it receives an image,
it calls `display.firstPage()` / `nextPage()` which blocks for ~1.5 seconds.
During that time, `read_buttons()` does not run — button presses during a
display refresh are missed. For a badge application this is acceptable, but
in a more complex system you would use an RTOS or interrupts to handle this.

---

## 8. Libraries and Abstraction

Writing a display driver from scratch means reading a 100-page datasheet,
implementing every SPI command sequence, and getting the timing exactly right.
Libraries save this work.

**GxEPD2** is a C++ library written for Arduino that implements drivers for
dozens of e-paper panels. When you write:

```cpp
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>

GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
    GxEPD2_270_GDEY027T91(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);
```

You are declaring a `display` object backed by the `GxEPD2_270_GDEY027T91`
driver. The `<...>` is C++ **template** syntax — it tells the compiler to
specialize the `GxEPD2_BW` class for this specific panel type, so the
internal page buffer is sized correctly at compile time without any runtime
overhead.

**Adafruit GFX** is the drawing layer. GxEPD2 inherits from it, giving you:

```cpp
display.fillScreen(GxEPD_WHITE);       // fill background
display.setFont(&FreeMonoBold9pt7b);   // choose a font
display.setCursor(8, 22);              // move text cursor (x, y in pixels)
display.print("NULL CITY BADGE");      // draw text
display.drawFastHLine(0, 30, 264, GxEPD_BLACK);  // horizontal line
display.drawBitmap(0, 0, data, 264, 176, GxEPD_BLACK, GxEPD_WHITE);
```

These functions transform your drawing commands into SPI transfers to the
SSD1680's RAM, handling the rotation transform, page boundaries, and bit
packing internally.

---

## 9. What Happens When You Flash

`pio run --target upload` does the following:

1. **Compiles** all `.cpp` files in `src/` and the libraries into object
   files (`.o`), then **links** them into a single ELF binary
2. **Packages** the binary into an ESP32 flash image (adding bootloader,
   partition table, and app segments at their correct flash addresses)
3. **Runs esptool** which connects to the CH340C over `/dev/ttyUSB0`,
   resets the ESP32 into download mode via the DTR/RTS pins, erases the
   relevant flash sectors, and writes the new image
4. **Hard resets** the ESP32 via RTS, which causes it to boot and run
   your new firmware

The partition table tells the bootloader where in flash your app lives.
The default 8 MB layout leaves room for OTA (over-the-air) updates,
non-volatile storage (NVS), and the app itself:

```
0x000000  Bootloader    (16 KB)
0x008000  Partition table
0x00E000  OTA data
0x010000  App partition  ← your firmware goes here
```

---

## 10. Where to Go Next

The current firmware already has several of these in place. Items marked ✓
are done; the rest are natural extensions.

**✓ Button-driven UI** — seven-item menu with UP/DOWN/SELECT/CANCEL navigation,
six sub-screens (system info, button test, I²C scanner, RNG, display test), and
pageable Hardware/Software guide lessons.

**✓ Deep sleep** — the badge enters deep sleep (< 10 µA) after 60 seconds of
inactivity, waking on a button press via GPIO1 edge wakeup. The e-paper holds
its image without power.

**✓ NVS — persistent custom home screen** — the art tool sends a 5808-byte
1-bit bitmap over serial; the badge saves it to flash using the ESP32
`Preferences` library. The image survives power cycles and deep sleep.

**Interrupt-driven buttons** — instead of polling TCA9534 every loop, wire
GPIO1 (the IRQ line) to an interrupt handler that fires only when a button
is pressed. This saves power and improves response time during display refreshes.

**CC1101 radio** — a CC1101 sub-GHz radio module attaches via the bottom
module connector on the front face, stacking on top of the badge. It operates
at 433.92 MHz. You can transmit and receive short packets between badges.

**Custom fonts** — Adafruit GFX supports fonts generated by
[fontconvert](https://github.com/adafruit/Adafruit-GFX-Library/tree/master/fontconvert).
You can convert any TTF font to a C header and use it with `setFont()`.
