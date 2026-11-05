# 9. Troubleshooting log

Problems met during bring-up and what fixed them.

## Arduino IDE / build

| Symptom | Cause and fix |
|---|---|
| `ModbusMaster.h: No such file` | Install the ModbusMaster library (Doc Walker). |
| `SoftwareSerial.h: No such file` | The sketch was written for ESP8266. On ESP32 use `HardwareSerial` with explicit pins. |
| `'DE_RE_PIN' was not declared` | The pin `#define` names do not match the ones used in the code. Rename consistently. |
| Sketch ends mid-statement after pasting | Truncated copy. Use the Copy button on the code block. |
| `'Fault' was not declared` | Arduino IDE auto-prototypes appear before the enum. Use `int` in function signatures (done in `soil_node_sim`). |
| Hundreds of `undefined reference` errors to core symbols | Corrupt build cache. Delete the `sketches` and `cores` folders under `C:\Users\<name>\AppData\Local\arduino\` and rebuild. |

## Upload

| Symptom | Fix |
|---|---|
| `No serial data received`, `Invalid head of packet (0x42)` | Put the board in download mode (BOOT + RESET), set Upload Speed 115200, close the Serial Monitor, use a data-capable USB cable. |

## Hardware behaviour

| Symptom | Cause and fix |
|---|---|
| Buzzer sounds constantly | Polarity or type mismatch, or old firmware still running. Final config: passive buzzer, active high (`BUZZER_ACTIVE_LOW false`, `BUZZER_PASSIVE true`). The buzzer pin is set to its silent level first thing in `setup()`. |
| LCD shows garbage | Old firmware, or supply noise. Check the 5 V rail, keep SDA/SCL short, add a decoupling capacitor. |
| LCD blank | Try I2C address 0x3F instead of 0x27. |

## RS485 / Modbus

| Symptom | Meaning |
|---|---|
| ModbusMaster `0xE2` | Timeout. Nothing answered: wiring, power, baud, address, A/B. |
| ModbusMaster `0xE0` | Invalid slave id. Bytes arrived but the first byte was wrong: line noise or RX stuck low. |
| ModbusMaster `0xE3` | CRC error: noise, wrong baud, or swapped A/B. |
| Idle line full of `0x00` | The ESP RX pin is held low: check the RO divider (1 kOhm series + 2 kOhm to GND), a shorted RO wire, or missing common ground. |

### NPK power

The manufacturer manual says 12 to 24 V DC, but this unit's label says 5 V and measures 5 V at brown/black, so a 5 V variant exists. Always read the label of the unit before choosing the supply. Wrong voltage can damage it.

### Raw probe

When ModbusMaster gives no useful answer, send the frame by hand and print every byte received. Scan 9600, 4800, 2400 and 19200 baud and addresses 1 to 5, and also listen to the idle line. Frame for NPK address 1: `01 03 00 1E 00 03 65 CD`.
