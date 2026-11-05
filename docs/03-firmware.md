# 3. Firmware

## Arduino IDE setup

- Arduino core: ESP32 by Espressif 3.3.x
- Board: ESP32S3 Dev Module
- USB CDC On Boot: **Enabled**
- PSRAM: **Disabled** (the chip has 2 MB quad PSRAM, so do not pick OPI)
- Upload Speed: **115200**
- Libraries: LiquidCrystal I2C (Frank de Brabander). For the real-sensor firmware no extra library is needed.

## Upload procedure

1. Close the Serial Monitor.
2. Put the board in download mode: hold BOOT, tap RESET, release BOOT (or unplug USB, hold BOOT, plug in).
3. Upload, then press RESET.

## `soil_node_sim`: the simulator (works)

Fake values for everything, real Wi-Fi and API. Used to build and test the app while the sensors are being fixed.

- Hotspot `SoilNode-XXXX`, password `soilnode123`, IP 192.168.4.1. Firmware version string `0.1.0-sim`.
- `GET /api/info` and `GET /api/readings` exactly as in [04-api-contract.md](04-api-contract.md).
- Values are mean-reverting random walks with noise. A "day" cycles every 600 s. Rain/irrigation events add moisture about every 10 minutes. Soil values are offset during a 60 s warm-up (the real ZTS needs 300 s). NPK reads slightly higher when the soil is wetter, as the real sensor does. Battery voltage starts at 3.95 V and drains 0.07 V per hour.
- LCD pages: air, soil (with warm-up countdown), NPK, Wi-Fi name/IP/clients, sequence number/uptime/fault.
- Buttons: UP and DOWN change page. Hold both for 1.5 s to cycle a simulated failure (none, NPK, SOIL, AIR). Serial Monitor 115200: `0` clears faults, `n` NPK, `z` soil, `a` air.
- A failed group goes `null` with `"ok": false` after 3 missed polls (about 12 s).
- Settings at the top of the file: buzzer polarity and type, `USE_REAL_AIR` (set 1 to read the real air sensor on RX 5 / TX 6, address 1), warm-up length, day-cycle length.

### Known Arduino IDE pitfall

The IDE generates function prototypes before the `enum Fault` definition. Functions that take the fault as an argument therefore use `int` (`faultName(int)`, `setFault(int)`) and cast inside. Do not change them back to `Fault`, or the build fails with "'Fault' was not declared".

### Buzzer

The current build has a passive buzzer driven HIGH (`BUZZER_ACTIVE_LOW false`, `BUZZER_PASSIVE true`). The sketch bit-bangs a 2 kHz tone for each beep. `setup()` sets the pin to the silent level before anything else. If the buzzer sounds continuously, the polarity or type setting is wrong for that module.

## `soil_node_api`: real sensors (not yet run)

Same API, but reads the three RS485 sensors over two UARTs, the analog moisture pin and the on-board MAX17048 gauge. Written to the API contract and **never compiled or tested on hardware**. Before using it:

- Set the pins to the ones actually wired (see [02-wiring-and-pins.md](02-wiring-and-pins.md)).
- Set `ADDR_NPK`, `ADDR_TH`, `ADDR_ZTS` to the real sensor addresses. They all ship as address 1. The working air sensor currently answers on address 1.
- The ZTS ships at 4800 baud, address 1. Either set it to 9600 or change the baud for that port.
- Calibrate `ADC_AIR` and `ADC_WATER` for the analog moisture sensor.
- The LCD, buttons and buzzer code from the simulator are not in this file yet. Port them in once the sensors read correctly.

## Modbus details

RS485 Modbus RTU, 9600 8N1, function 0x03, CRC16 low byte first, 200 ms gap between requests, 300 ms reply timeout, 2 to 3 retries.

| Sensor | Registers | Decoding |
|---|---|---|
| JXBS-3001-NPK-RS (addr 1) | 0x001E x3 | N, P, K in mg/kg |
| JXBS-3001-TH-RS | 0x0000 x2 | register 0 = humidity / 10, register 1 = temperature / 10 (signed) |
| ZTS-3001-TR-THPH-N01 | 0x0000 x4 | moisture / 10, soil temperature / 10 (signed), EC absent (ignore), pH / 10 |

Example frames (CRC included): NPK addr 1 `01 03 00 1E 00 03 65 CD`; air TH addr 1 `01 03 00 00 00 02 C4 0B`; air TH addr 2 `02 03 00 00 00 02 C4 38`; ZTS addr 1 `01 03 00 00 00 04 44 09`; ZTS addr 3 `03 03 00 00 00 04 45 EB`.

Changing settings on the ZTS: address register 0x07D0, baud register 0x07D1 (0 = 2400, 1 = 4800, 2 = 9600). Set 9600 baud: `01 06 07 D1 00 02 59 46`. Set address 3: `01 06 07 D0 00 03 C9 46`.

ModbusMaster error codes seen on the bench: 0xE2 = timeout (nothing answered), 0xE0 = invalid slave id (bytes arrived but the first byte was wrong, usually noise), 0xE3 = CRC error.

## Bench test sketches

The one-off sketches used during bring-up (NPK-only, NPK raw probe with baud/address scan, buzzer test, LCD and buttons page test, merged air + LCD sketch) were pasted in chat and are not stored in this repository. Add them under `firmware/bench/` if they are still needed.
