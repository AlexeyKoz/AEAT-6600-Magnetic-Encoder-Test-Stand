/*
 * ============================================================
 *  Magnetic Encoder Test Stand - AEAT-6600-T16
 * ------------------------------------------------------------
 *  Controller : ESP32-C5 (DOIT ESPC5-32)
 *  Display    : SSD1306 0.96" OLED (I2C)
 *  Interface  : SSI (bit-bang) - DO / CLK / NCS
 *  Power      : encoder 3.3V
 * ------------------------------------------------------------
 *  Purpose: go/no-go check. Magnet is moved by hand.
 *  Shows  : angle (16-bit), reaction to magnet,
 *           field status from MAGHI / MAGLO.
 * ============================================================
 *
 *  Libraries (Library Manager):
 *    - U8g2 by oliver
 *    - Adafruit NeoPixel
 *
 *  Board in Arduino IDE: "ESP32C5 Dev Module"
 *  (requires esp32 core 3.1.x+ with C5 support)
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

// ----------------- Pin map -----------------
// SSI to encoder
#define PIN_CLK   4    // GPIO4  -> J1 SE_SSICLK
#define PIN_NCS   5    // GPIO5  -> J1 SE_NCS
#define PIN_DO    6    // GPIO6  <- J1 SE_SSIDATA

// Magnet status (encoder outputs MAGHI / MAGLO)
#define PIN_MAGHI 13   // GPIO13 <- U1 MAGHI
#define PIN_MAGLO 14   // GPIO14 <- U1 MAGLO

// OLED I2C
#define PIN_SDA   2    // GPIO2
#define PIN_SCL   3    // GPIO3

// On-board RGB LED (addressable WS2812, single wire)
#define PIN_RGB   27   // GPIO27 (silkscreen label "GPIO27")
#define RGB_COUNT 1

// ----------------- SSI parameters -----------------
#define SSI_BITS        16      // encoder resolution (16-bit)
#define SSI_CLK_HALF_US 2       // clock half-period (~250 kHz). Increase if wires are long.

// OLED: hardware I2C (software I2C did not work on this board)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// RGB LED
Adafruit_NeoPixel rgb(RGB_COUNT, PIN_RGB, NEO_GRB + NEO_KHZ800);
#define RGB_BRIGHT 40   // brightness 0..255 (keep low, don't blind)

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

// ----------------- Global state -----------------
uint16_t lastRaw      = 0;       // last raw reading
uint16_t prevRaw      = 0;       // for movement detection
bool      everChanged = false;   // has the value ever changed (sign of a live encoder)
uint32_t  readCount   = 0;

// Disconnected-encoder detection
uint8_t   stuckCount  = 0;       // consecutive readings that are "stuck" (0x0000 / 0xFFFF)
#define   STUCK_LIMIT  8         // after this many readings, treat encoder as disconnected
bool      encConnected = true;   // "encoder connected" flag

// Angle range over the session (shows whether the encoder was actually turned)
uint16_t  rawMin = 0xFFFF;
uint16_t  rawMax = 0x0000;

// For blinking the LED on errors
uint32_t  lastBlink  = 0;
bool      blinkState = false;

// Anti-flicker: GOOD is held as long as the encoder reacted recently.
// The main health criterion is a valid CHANGING angle, NOT the magnet status.
uint32_t  lastMoveTime = 0;          // timestamp of the last angle change
#define   GOOD_HOLD_MS  3000         // hold GOOD this many ms after last movement

// ============================================================
//  Read a 16-bit word over SSI (bit-bang, MSB first)
// ============================================================
uint16_t readSSI() {
  uint16_t value = 0;

  // Start: NCS low -> encoder latches data into the shift register
  digitalWrite(PIN_NCS, LOW);
  delayMicroseconds(1);          // tCS - time to capture data

  for (int i = 0; i < SSI_BITS; i++) {
    // Clock pulse
    digitalWrite(PIN_CLK, LOW);
    delayMicroseconds(SSI_CLK_HALF_US);
    digitalWrite(PIN_CLK, HIGH);
    delayMicroseconds(SSI_CLK_HALF_US);

    // Read the bit after the edge (MSB first)
    value <<= 1;
    if (digitalRead(PIN_DO)) value |= 0x01;
  }

  // End of frame
  digitalWrite(PIN_CLK, HIGH);
  digitalWrite(PIN_NCS, HIGH);
  delayMicroseconds(SSI_CLK_HALF_US);

  return value;
}

// ============================================================
//  Magnet status from MAGHI / MAGLO
//  MAGLO=1 -> field weak (no magnet / too far)
//  MAGHI=1 -> field strong (magnet too close)
//  both 0  -> field in range
// ============================================================
const char* magnetStatus(bool &ok) {
  bool hi = digitalRead(PIN_MAGHI);
  bool lo = digitalRead(PIN_MAGLO);
  ok = false;

  if (lo)  return "NO MAGNET";    // weak field / no magnet
  if (hi)  return "TOO CLOSE";    // too close
  ok = true;
  return "FIELD OK";
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_NCS, OUTPUT);
  pinMode(PIN_DO,  INPUT);
  pinMode(PIN_MAGHI, INPUT);
  pinMode(PIN_MAGLO, INPUT);

  digitalWrite(PIN_NCS, HIGH);   // inactive
  digitalWrite(PIN_CLK, HIGH);   // idle high

  // RGB LED
  rgb.begin();
  rgb.setBrightness(RGB_BRIGHT);
  // Power-on LED self-test: R -> G -> B
  setRGB(255, 0, 0); delay(250);
  setRGB(0, 255, 0); delay(250);
  setRGB(0, 0, 255); delay(250);
  setRGB(0, 0, 0);

  Wire.begin(PIN_SDA, PIN_SCL);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x12_tf);

  // Splash
  u8g2.clearBuffer();
  u8g2.drawStr(0, 12, "AEAT-6600 TEST");
  u8g2.drawStr(0, 50, "Encoder check...");
  u8g2.sendBuffer();
  delay(1200);
}

// ============================================================
void loop() {
  lastRaw = readSSI();
  readCount++;

  // ---------- Disconnected-encoder detection ----------
  // When the DO line is disconnected, the value sticks at 0x0000 or 0xFFFF.
  if (lastRaw == 0x0000 || lastRaw == 0xFFFF) {
    if (stuckCount < 255) stuckCount++;
  } else {
    stuckCount = 0;            // got a "live" value -> reset
  }
  encConnected = (stuckCount < STUCK_LIMIT);

  // ---------- Movement detection (only when connected) ----------
  if (encConnected && lastRaw != prevRaw) {
    everChanged = true;
    prevRaw = lastRaw;
    lastMoveTime = millis();        // remember the moment of movement
    if (lastRaw < rawMin) rawMin = lastRaw;
    if (lastRaw > rawMax) rawMax = lastRaw;
  }

  bool magOk;
  const char* mag = magnetStatus(magOk);

  // Angle in degrees
  float angle = (lastRaw * 360.0f) / 65536.0f;

  // ---------- Health criterion ----------
  // The encoder is healthy (GOOD) if it is connected and produces a valid changing angle.
  // The MAGHI/MAGLO status flickers near the threshold and must NOT affect the verdict -
  // it is shown on screen only as extra information.
  // GOOD is held for GOOD_HOLD_MS after the last movement so it doesn't flicker during pauses.
  bool isGood = encConnected && everChanged &&
                (millis() - lastMoveTime < GOOD_HOLD_MS);

  // ---------- Blink timer (for errors) ----------
  if (millis() - lastBlink > 300) {
    lastBlink = millis();
    blinkState = !blinkState;
  }

  // ---------------- RGB LED ----------------
  if (!encConnected) {
    // Encoder not connected -> blinking red
    setRGB(blinkState ? 255 : 0, 0, 0);
  } else if (isGood) {
    setRGB(0, 255, 0);          // green - encoder healthy, angle is being read
  } else if (everChanged) {
    setRGB(255, 180, 0);        // yellow - was active, currently paused (waiting for movement)
  } else {
    setRGB(255, 180, 0);        // yellow - connected, waiting for first movement with magnet
  }

  // ---------------- OLED ----------------
  u8g2.clearBuffer();

  char buf[32];

  if (!encConnected) {
    // -------- "Not connected" screen --------
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(8, 26, "ENCODER");
    u8g2.drawStr(2, 44, "NOT CONNECTED");
    // border for visibility
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.sendBuffer();

    Serial.printf("RAW=%5u  ENCODER NOT CONNECTED (stuck=%u)\n", lastRaw, stuckCount);
    delay(80);
    return;
  }

  // -------- Working screen --------
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 10, "ENCODER TEST");
  u8g2.drawHLine(0, 13, 128);

  // RAW and angle
  snprintf(buf, sizeof(buf), "RAW: %5u", lastRaw);
  u8g2.drawStr(0, 27, buf);

  snprintf(buf, sizeof(buf), "ANG: %6.2f", angle);
  u8g2.drawStr(0, 40, buf);

  // Magnet status
  u8g2.drawStr(0, 53, mag);

  // Angle range over the session (shows whether it was actually turned) - bottom
  if (rawMax >= rawMin) {
    float span = ((rawMax - rawMin) * 360.0f) / 65536.0f;
    snprintf(buf, sizeof(buf), "range:%5.1f", span);
    u8g2.drawStr(64, 53, buf);
  }

  // -------- Large GOOD / BAD status, top right --------
  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (isGood) {
    u8g2.drawStr(88, 11, "GOOD");
  } else {
    u8g2.drawStr(92, 11, "BAD");
  }

  // "Alive" indicator - blinking dot
  if (readCount & 0x04) u8g2.drawDisc(124, 60, 2);

  u8g2.sendBuffer();

  // ---------------- Serial ----------------
  Serial.printf("RAW=%5u  ANG=%6.2f  MAGHI=%d MAGLO=%d  %s  %s\n",
                lastRaw, angle,
                digitalRead(PIN_MAGHI), digitalRead(PIN_MAGLO),
                mag, isGood ? "GOOD" : "BAD");

  delay(80);   // ~12 updates/sec
}
