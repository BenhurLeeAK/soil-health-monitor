# 2. Wiring and pin map

Pin numbers are GPIO numbers. Three different pin sets exist in the project history. This page lists them side by side so nobody mixes them up. **Always use the pins in the sketch you are flashing.**

## A. What the simulator sketch uses (`soil_node_sim`, running now)

| Function | Signal | GPIO |
|---|---|---|
| LCD I2C (0x27, 16x2) | SDA | 8 |
| LCD | SCL | 9 |
| Button UP (next page) | INPUT_PULLUP, other side to GND | 15 |
| Button DOWN (previous page) | INPUT_PULLUP, other side to GND | 16 |
| Buzzer (passive) | output, sounds when driven HIGH | 10 |
| Air sensor (only if `USE_REAL_AIR 1`) | RX / TX | 5 / 6, address 1, no DE pin |

## B. Bench-test pin map (checking sketches, 4 Nov 2026)

| Function | Signal | GPIO |
|---|---|---|
| NPK sensor (UART1, address 1) | TX | 14 |
| NPK sensor | RX | 13 |
| NPK sensor | DE+RE | 4 |
| Soil sensor ZTS (UART2) | TX | 12 |
| Soil sensor | RX | 11 |
| Soil sensor | DE+RE | 7 |
| Air TH sensor (as in the first bench sketch, address 2) | TX / RX / DE+RE | 18 / 17 / 6 |
| LCD, buttons, buzzer | | as in table A |

Later the NPK test was re-pinned to TX 12, RX 11, DE+RE 7 to rule out wiring. The working air-sensor sketch uses RX 5, TX 6, address 1 and no DE pin, which supersedes the 18/17/6 entry.

## C. Reference firmware template (`soil_node_api`, untested)

| Function | GPIO |
|---|---|
| Bus A (NPK + ZTS): TX / RX / DE+RE | 14 / 13 / 12 |
| Bus B (TH-RS): TX / RX | 40 / 39 |
| Analog moisture (ADC1) | 1 |
| Battery gauge MAX17048 (on board): SDA / SCL | 3 / 2 |

## Things to verify on the board

- GPIO 17 and 18 are the A7670E modem UART (headers RXD1/TXD1). The 4G DIP switch decides whether they are usable. Avoid them.
- Avoid GPIO 33 to 37 (octal PSRAM and the battery switch), 45, 46, and the TF-card pins (4, 5, 6, 46).
- GPIO 15 and 16 are camera SCCB pins. They are free while the camera connector is empty.
- UART0 on the bus needs "USB CDC On Boot: Enabled" so the serial monitor still works.
- Check solder blobs near IO41/IO40/IO39 for bridges (visible in the build photos).

## RS485 driver module wiring

```
ESP32 TX  -> module DI (TXD)
ESP32 RX  <- module RO (RXD) through 1 kOhm series, then 2 kOhm to GND  (5 V to 3.3 V divider)
ESP32 GPIO-> module DE + RE tied together (omit for auto-direction modules)
module A  -> sensor A (yellow)
module B  -> sensor B (blue)
GND       common between ESP32, module and sensor
```

Idle line reading all 0x00 bytes means RX is stuck low: check the divider, the RO wire and the common ground before suspecting the sensor.
