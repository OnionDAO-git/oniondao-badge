// OnionDAO Badge — header pin solder test
//
// Cycles every candidate GPIO on the top expansion header and the
// module connector (covers all three module wiring variants — L1, L2, R —
// since only one set will actually be populated on any given board).
// Each pin goes HIGH for 1s, LOW for 1s, repeated 3x, then moves on.
// Probe the physical solder joint with a multimeter (DC volts, pin to GND)
// while the matching label is printed over Serial — you should see it
// swing 0V <-> ~3.3V in step with the printout.
//
// Board: ESP32S3 Dev Module (Arduino-ESP32 core). 115200 baud.

struct TestPin {
  uint8_t gpio;
  const char *label;
};

// Top header (always present): G38 G39 G16 G15 G07 G06 G05 G04
// Module connector signal pins, union of L1 / L2 / R variants:
// L1: 48 47 19 42 41   L2: 40 41 42 19 47   R: 38 39 16 15 7 (already above)
static const TestPin kPins[] = {
  {38, "G38 (top header pin 3 / module R)"},
  {39, "G39 (top header pin 4 / module R)"},
  {16, "G16 (top header pin 5 / module R)"},
  {15, "G15 (top header pin 6 / module R)"},
  {7,  "G07 (top header pin 7 / module R)"},
  {6,  "G06 (top header pin 8)"},
  {5,  "G05 (top header pin 9)"},
  {4,  "G04 (top header pin 10)"},
  {48, "G48 (module L1)"},
  {47, "G47 (module L1 / L2)"},
  {19, "G19 (module L1 / L2)"},
  {42, "G42 (module L1 / L2)"},
  {41, "G41 (module L1 / L2)"},
  {40, "G40 (module L2)"},
};

static const unsigned long kHalfPeriodMs = 1000;
static const int kCyclesPerPin = 3;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nOnionDAO badge pin test — probe GND to each pin with a multimeter");
  Serial.println("while its label is printed. Expect 0V <-> ~3.3V toggling.\n");
}

void loop() {
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
  Serial.println("\n--- full pass complete, restarting ---\n");
  delay(2000);
}
