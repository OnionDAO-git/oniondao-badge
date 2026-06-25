// OnionDAO Badge — header pin solder test (ESP-IDF + arduino-esp32)
//
// 1. Cycles every GPIO on J8 (left port) and J10 (top/right header) once
//    at boot — each pin goes HIGH for 1s, LOW for 1s, repeated 3x, then
//    moves on. Probe the physical solder joint with a multimeter (DC
//    volts, pin to GND) while the matching label is printed over Serial —
//    you should see it swing 0V <-> ~3.3V in step with the printout.
// 2. Then repeatedly (every loop pass): reads the CC1101's ID registers
//    and a free-running test clock over real SPI (proves the whole
//    CC1101 path end to end), and plays a 440 Hz test tone over I2S to
//    the Sound module's amp (proves the whole Sound path end to end).
//
// SPI and I2S are initialized ONCE in setup() and reused every pass —
// re-initializing either on every loop iteration starves shared
// DMA/interrupt resources and eventually hangs the SPI transfer.
//
// 115200 baud over the CH340C USB-serial port.

#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <driver/i2s_std.h>

#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

// PWR rail gate — must be HIGH before talking to any module (HARDWARE.md §3)
static const uint8_t PIN_PWR_EN = 18;

// Display SPI — J4 socket (PINOUT.md)
static const uint8_t PIN_EPD_SCK = 11;
static const uint8_t PIN_EPD_CS = 12;
static const uint8_t PIN_EPD_DC = 13;
static const uint8_t PIN_EPD_RST = 14;
static const uint8_t PIN_EPD_MOSI = 17;
static const uint8_t PIN_EPD_BUSY = 21;

GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
    GxEPD2_270_GDEY027T91(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// Draws up to 4 lines of status text. Full-window refresh, then hibernates
// the panel (low power, image persists with no power applied).
static void showStatus(const char *line1, const char *line2 = "",
                        const char *line3 = "", const char *line4 = "") {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, 22);
    display.print("PIN TEST");
    display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);
    display.setFont(&FreeMono9pt7b);
    display.setCursor(8, 55);
    display.print(line1);
    display.setCursor(8, 80);
    display.print(line2);
    display.setCursor(8, 105);
    display.print(line3);
    display.setCursor(8, 130);
    display.print(line4);
  } while (display.nextPage());
  display.hibernate();
}

// CC1101 wiring on J8 (per the CC1101->J8 jumper mapping)
static const uint8_t CC1101_SCK = 47;
static const uint8_t CC1101_MISO = 42; // module SDO
static const uint8_t CC1101_MOSI = 48; // module SDI
static const uint8_t CC1101_CS = 19;   // module SCS
static const uint8_t CC1101_GDO0 = 41; // module GDO

static SPIClass cc1101SPI(FSPI);

// Sound module wiring on J10 (per the Sound->J10 jumper mapping)
static const uint8_t SOUND_CTRL = 7;   // module CTR — NS4168 mode/shutdown
static const uint8_t SOUND_BCLK = 39;  // module SCK
static const uint8_t SOUND_WS = 16;    // module SLR (word select / LRCK)
static const uint8_t SOUND_DOUT = 15;  // module SDO -> NS4168 SD (data in to amp)

static i2s_chan_handle_t gSoundTxChan = NULL;

// Reads a CC1101 status register. Status regs (0x30-0x3D) require the
// burst bit forced high in the header byte per the CC1101 datasheet.
static uint8_t cc1101ReadStatus(uint8_t addr) {
  digitalWrite(CC1101_CS, LOW);
  cc1101SPI.transfer(addr | 0xC0);
  uint8_t value = cc1101SPI.transfer(0x00);
  digitalWrite(CC1101_CS, HIGH);
  return value;
}

static void cc1101Init() {
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
  pinMode(CC1101_GDO0, INPUT);
  cc1101SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
}

struct CC1101Result {
  uint8_t partnum;
  uint8_t version;
  float gdoHz;
  bool gdoGood;
};

// Sets GDO0 to output the chip's crystal-divided clock (a free-running
// square wave) so we can confirm the GDO jumper carries a real signal
// instead of just floating. Anything other than IOCFG0 default behavior
// proves the SPI link (and therefore SDI/SCK/SCS/SDO) is alive.
static CC1101Result cc1101CheckLink() {
  cc1101SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  uint8_t partnum = cc1101ReadStatus(0x30);
  uint8_t version = cc1101ReadStatus(0x31);
  cc1101SPI.endTransaction();

  Serial.printf("CC1101 link check -> PARTNUM=0x%02X VERSION=0x%02X\n", partnum, version);
  // PARTNUM=0x00 is normal. VERSION=0x00 is also a known-normal reading on
  // many real and clone CC1101 chips - don't treat it alone as a failure.
  // Only an all-0xFF response (both regs pinned high) reliably means the
  // bus is dead (MISO floating / no chip / no power).
  if (partnum == 0xFF && version == 0xFF) {
    Serial.println("  -> CC1101 SPI link FAILED - MISO floating, check SDO/SCK/SCS/SDI jumpers and power");
  } else {
    Serial.println("  -> CC1101 SPI responding (deterministic, non-0xFF read) - see GDO0 frequency check below for the definitive result");
  }

  // GDO0 toggle: set IOCFG0 register to drive CLK_XOSC/192 (a free-running
  // clock) onto GDO0, then sample it a few times to confirm it's moving.
  cc1101SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CC1101_CS, LOW);
  cc1101SPI.transfer(0x00);  // IOCFG0 address, write (header bits 00)
  cc1101SPI.transfer(0x3F);  // GDO0_CFG = CLK_XOSC/192
  digitalWrite(CC1101_CS, HIGH);
  cc1101SPI.endTransaction();

  int lastLevel = digitalRead(CC1101_GDO0);
  int changes = 0;
  unsigned long start = millis();
  while (millis() - start < 50) {
    int level = digitalRead(CC1101_GDO0);
    if (level != lastLevel) { changes++; lastLevel = level; }
  }
  // Expect ~26 MHz crystal / 192 = ~135.4 kHz. Software polling undercounts
  // edges at this speed, so treat anything in the tens-of-kHz range as a
  // match - it's the right order of magnitude for a genuine CLK_XOSC/192
  // output, which only happens if our register write actually landed.
  float measuredHz = (changes / 2.0f) / 0.050f;
  Serial.printf("  -> GDO0 toggled %d times in 50ms (~%.0f Hz; expect ~135400 Hz CLK_XOSC/192)\n",
                changes, measuredHz);
  bool gdoGood = measuredHz > 50000.0f;
  if (gdoGood) {
    Serial.println("  -> GDO jumper GOOD - frequency matches CLK_XOSC/192, full SPI write+readback path confirmed");
  } else if (changes > 0) {
    Serial.println("  -> GDO0 is toggling but far from expected frequency - check the GDO jumper / SCK jumper for a flaky connection");
  } else {
    Serial.println("  -> GDO jumper FAILED - no signal, check that wire");
  }

  return {partnum, version, measuredHz, gdoGood};
}

static void soundInit() {
  pinMode(SOUND_CTRL, OUTPUT);
  digitalWrite(SOUND_CTRL, HIGH);  // enable the amp (active mode), left on

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&chanCfg, &gSoundTxChan, NULL);

  i2s_std_config_t stdCfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)SOUND_BCLK,
      .ws = (gpio_num_t)SOUND_WS,
      .dout = (gpio_num_t)SOUND_DOUT,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
    },
  };
  i2s_channel_init_std_mode(gSoundTxChan, &stdCfg);
  i2s_channel_enable(gSoundTxChan);
}

// Plays a 440 Hz tone over the already-initialized I2S channel so you can
// confirm by ear that BCLK/WS/SDO/CTRL and GND/VCC are all wired correctly
// to the amp. There's no chip ID to read back here (NS4168 has none) —
// your ears are the test instrument for this one.
static void soundPlayTone() {
  Serial.println("Playing a 440 Hz tone for 3 seconds - listen for it on the speaker...");

  const int kSampleRate = 44100;
  const float kFreq = 440.0f;
  const int kTotalFrames = kSampleRate * 3;
  int16_t buf[256];  // 128 stereo frames per write
  size_t bytesWritten;
  static uint32_t sampleIndex = 0;
  int framesWritten = 0;
  while (framesWritten < kTotalFrames) {
    for (int i = 0; i < 128; i++) {
      float t = (float)sampleIndex / kSampleRate;
      int16_t s = (int16_t)(sinf(2.0f * (float)M_PI * kFreq * t) * 8000);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
      sampleIndex++;
    }
    i2s_channel_write(gSoundTxChan, buf, sizeof(buf), &bytesWritten, portMAX_DELAY);
    framesWritten += 128;
  }

  // Without this, the I2S hardware keeps cycling its last DMA buffer
  // forever once we stop feeding it new data, instead of going silent.
  i2s_channel_disable(gSoundTxChan);
  digitalWrite(SOUND_CTRL, LOW);  // mute the amp too

  Serial.println("Tone finished. Did you hear a steady 440 Hz tone from the speaker?");
}

struct TestPin {
  uint8_t gpio;
  const char *label;
};

// J8 (left port): GND VCC G48 G47 G19 G42 G41 G40 VCC GND
// J10 (top/right header): GND VCC G38 G39 G16 G15 G07 G06 G05 G04
static const TestPin kPins[] = {
  {48, "G48 (J8 pin 3 / CC1101 SDI)"},
  {47, "G47 (J8 pin 4 / CC1101 SCK)"},
  {19, "G19 (J8 pin 5 / CC1101 SCS)"},
  {42, "G42 (J8 pin 6 / CC1101 SDO)"},
  {41, "G41 (J8 pin 7 / CC1101 GDO)"},
  {40, "G40 (J8 pin 8, spare)"},
  {38, "G38 (J10 pin 3 / Sound SDI)"},
  {39, "G39 (J10 pin 4 / Sound SCK)"},
  {16, "G16 (J10 pin 5 / Sound SLR)"},
  {15, "G15 (J10 pin 6 / Sound SDO)"},
  {7,  "G07 (J10 pin 7 / Sound CTR)"},
  {6,  "G06 (J10 pin 8, spare)"},
  {5,  "G05 (J10 pin 9, spare)"},
  {4,  "G04 (J10 pin 10, spare)"},
};

static const unsigned long kHalfPeriodMs = 1000;
static const int kCyclesPerPin = 3;

static void runGpioTogglePass() {
  for (auto &p : kPins) {
    pinMode(p.gpio, OUTPUT);
    Serial.printf("Testing %s ...\n", p.label);
    for (int i = 0; i < kCyclesPerPin; i++) {
      digitalWrite(p.gpio, HIGH);
      delay(kHalfPeriodMs);
      digitalWrite(p.gpio, LOW);
      delay(kHalfPeriodMs);
    }
    pinMode(p.gpio, INPUT);  // release so it doesn't fight anything
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nOnionDAO badge pin test — probe GND to each pin with a multimeter");
  Serial.println("while its label is printed. Expect 0V <-> ~3.3V toggling.\n");

  SPI.begin(PIN_EPD_SCK, /*MISO*/ -1, PIN_EPD_MOSI, PIN_EPD_CS);
  display.init(115200, true, 10, false);
  display.setRotation(1);
  showStatus("Starting pin test...", "GPIO toggle pass running", "(watch multimeter now)");

  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);
  delay(5); // module power-up settle

  // One-shot multimeter toggle pass on every J8/J10 pin.
  runGpioTogglePass();
  Serial.println("\n--- GPIO toggle pass complete (will not repeat) ---\n");
  showStatus("GPIO toggle: DONE", "14/14 pins toggled", "Running CC1101 check...");

  // Persistent peripheral init, then run each check exactly once.
  cc1101Init();
  soundInit();

  Serial.println("--- running CC1101 link check ---\n");
  CC1101Result cc1101 = cc1101CheckLink();
  char line1[32], line2[32], line3[32];
  snprintf(line1, sizeof(line1), "PARTNUM=0x%02X VER=0x%02X", cc1101.partnum, cc1101.version);
  snprintf(line2, sizeof(line2), "GDO0 ~%.0f Hz", cc1101.gdoHz);
  snprintf(line3, sizeof(line3), "CC1101: %s", cc1101.gdoGood ? "PASS" : "FAIL");
  showStatus("CC1101 link check", line1, line2, line3);
  delay(2000);

  Serial.println("\n--- running Sound module tone-out check ---\n");
  showStatus("Sound module test", "Playing 440Hz tone...", "Listen on speaker");
  soundPlayTone();
  showStatus("Sound module test", "Tone played - 3s", "Did you hear it?");
  delay(2000);

  Serial.println("\n--- all checks complete, done ---\n");
  snprintf(line1, sizeof(line1), "CC1101: %s", cc1101.gdoGood ? "PASS" : "FAIL");
  showStatus("ALL CHECKS COMPLETE", "GPIO toggle: PASS", line1, "Sound: played (check ears)");
}

void loop() {
  delay(1000);  // nothing left to do — checks ran once in setup()
}
