# 8. Design notes (original plan, 29 Oct 2026)

This is the planning document written before the build. Several things changed later: the prototype is powered from a 4-cell 18650 pack through a buck converter, the app talks to the node over its own hotspot (not MQTT), and the pins were re-assigned. See [02-wiring-and-pins.md](02-wiring-and-pins.md) for what is actually wired. Power figures below are estimates, not measurements.

## Two products, one board

- Supervisor's version: sensors only, battery alone.
- Student version: same node plus a solar panel on a pole. Students build on the same stack.
- One battery type for both: 18650 Li-ion, 3.7 V, 3000 mAh or more. The board has its own USB-C and solar charging circuits, so students only need the panel (5.5 to 6 V, 5 to 6 W).
- Same firmware, different reading interval: supervisor 2 to 6 h, students about 1 h.

## Uplink (later phase)

Wi-Fi, no SIM. The ESP32-S3 uploads MQTT over TLS to a router or phone hotspot. The 4G DIP switch is OFF so the unused modem draws nothing. Limit: the node must be in Wi-Fi range at the planting spot. A dashboard could run on a local broker (Mosquitto, Node-RED, Grafana) or a cloud broker.

## Power budget (assumptions, unmeasured)

Assumed loads 1.75 W, boost efficiency 85 %, Wi-Fi connect and publish about 10 s at 1 W, sleep 1 mA, 80 % of a 3000 mAh cell usable (8.88 Wh). About 0.19 Wh per reading because the ZTS needs a 5 minute warm-up.

| Interval | Days on one cell (no panel) |
|---|---|
| 1 h | 1.9 |
| 2 h | 3.7 |
| 6 h | 10 |
| 12 h | 19 |
| 24 h | 32 |

A plain boost converter leaks battery voltage to its output through the diode when disabled. Use a true load-disconnect or a load switch. Measure sleep current with a 1 ohm series resistor on the mV range. Do not use the meter's current range while Wi-Fi is on.

## Board facts used

- IO17/IO18 (headers TXD1/RXD1) go to the modem.
- TF card uses IO4/5/6/46. Camera uses IO7 to IO16 and IO34 to IO37.
- Battery gauge MAX17048 on IO3 (SDA) / IO2 (SCL). RGB LED on IO38.
- IO33 drives the battery-to-system power switch. Never use it.
- Octal PSRAM on the S3R8 uses IO33 to IO37.

## Modbus

See [03-firmware.md](03-firmware.md) for registers and example frames.

## Software plan for the battery version

Timer wake, sensor power on, 5 minute warm-up for the ZTS, read (median of 5), score, LCD, buzzer if out of range, Wi-Fi on, MQTT publish (TF card buffer if offline), power off, deep sleep. Buttons wake the node and show the last readings for 30 s.

## Open items

1. Measure real sleep and on-time currents and the ZTS settling time. Check a protected cell fits the holder.
2. Confirm the on/off switch type.
3. Confirm on the Waveshare wiki that the 4G DIP switch cuts the modem supply.
4. Test Wi-Fi signal at the planting spot.
