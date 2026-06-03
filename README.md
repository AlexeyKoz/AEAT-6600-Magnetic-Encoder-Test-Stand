# AEAT-6600 Magnetic Encoder Test Stand

A simple go/no-go bench tester for the **Broadcom AEAT-6600-T16** magnetic rotary encoder.
Built on an **ESP32-C5** with a 0.96" OLED and the on-board RGB LED for at-a-glance status.

The magnet is moved by hand near the encoder. The stand confirms the encoder is alive,
reads the 16-bit absolute angle over SSI, and reports a clear **GOOD / BAD** verdict.

---

## Features

- 16-bit absolute angle readout over **SSI** (bit-banged: DO / CLK / NCS)
- Angle shown as **raw value** and **degrees (0–360)**
- **GOOD / BAD** verdict on screen and via RGB LED
- **Disconnected-encoder detection** — distinct blinking-red state + "NOT CONNECTED" screen
- **Session angle range** — shows how far the encoder was actually turned
- Magnet field status (MAGHI / MAGLO) shown as extra info
- Power-on LED self-test (R → G → B)
- Serial log at 115200 baud for debugging

---

## Hardware

| Item | Part |
|------|------|
| Controller | ESP32-C5 (DOIT ESPC5-32) |
| Display | SSD1306 0.96" OLED (I2C) |
| Encoder | Broadcom AEAT-6600-T16 |
| LED | On-board addressable RGB (WS2812) |

> Encoder is powered at **3.3V** in this setup — direct connection to the ESP32-C5,
> no level shifter needed. (At 5V a level shifter would be required, since ESP32-C5 GPIO
> is 3.3V only.)

---

## Wiring

| Signal | ESP32-C5 GPIO | Connects to |
|--------|---------------|-------------|
| SSI CLK (out) | GPIO4 | encoder SSICLK |
| SSI NCS (out) | GPIO5 | encoder NCS |
| SSI DO (in)   | GPIO6 | encoder SSIDATA |
| MAGHI (in)    | GPIO13 | encoder MAGHI |
| MAGLO (in)    | GPIO14 | encoder MAGLO |
| OLED SDA      | GPIO2 | SSD1306 SDA |
| OLED SCL      | GPIO3 | SSD1306 SCL |
| Power         | 3V3 | encoder + OLED VCC |
| Ground        | GND | common ground |

```
   ESP32-C5                         AEAT-6600
  +---------+                      +-------------+
  |   GPIO4 |--------- CLK ------->| SSICLK      |
  |   GPIO5 |--------- NCS ------->| NCS         |
  |   GPIO6 |<-------- DO ---------| SSIDATA     |
  |  GPIO13 |<-------- MAGHI ------| MAGHI       |
  |  GPIO14 |<-------- MAGLO ------| MAGLO       |
  |     3V3 |--------- 3.3V ------>| VDD         |
  |     GND |--------- GND ------->| GND         |
  +----+----+                      +-------------+
       |  GPIO2 / GPIO3 (I2C)
       v
  +---------+
  | SSD1306 |  0.96" OLED
  +---------+
```

---

## Status indication

| State | RGB LED | OLED |
|-------|---------|------|
| Encoder not connected | Blinking red | "ENCODER NOT CONNECTED" |
| Healthy, angle changing | Green | GOOD |
| Connected, waiting for movement | Yellow | BAD |

The verdict is based on a **valid, changing angle** — not on the magnet field status,
which flickers near its threshold and is shown for information only.

---

## Build & Flash

1. **Arduino IDE** — install ESP32 core **3.1.x or newer** (Boards Manager → "esp32" by Espressif).
   ESP32-C5 support only exists in 3.1+.
2. Select board: **ESP32C5 Dev Module**.
3. Install libraries (Library Manager):
   - **U8g2** by oliver
   - **Adafruit NeoPixel**
4. Set **Upload Speed = 115200** (C5 can fail at higher speeds).
5. Flash via the **UART** USB-C port. If it won't connect: hold **BOOT**, tap **RESET**, release **BOOT**.

---

## Repository contents

```
.
├── encoder_test_stand.ino     # firmware
├── AEAT-6600_datasheet.pdf    # encoder datasheet
└── README.md
```

---

## Notes / Troubleshooting

- **OLED blank but I2C scan finds 0x3C** → use hardware I2C (already set in code).
  If still blank, the controller may be SH1106 — change the constructor to `U8G2_SH1106_...`.
- **Raw value jumps randomly** even with a steady magnet → the SSI frame may have extra
  status bits after the 16 angle bits; the bit parsing needs adjustment.
- **Flashing fails with "chip magic value 0x5fd1406f"** → ESP32 core is too old, update to 3.1+.

---

## License

MIT
