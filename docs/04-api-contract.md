# 4. ESP32 to phone app contract (schema 1)

For the hardware engineer. The phone app needs exactly this from the ESP32 and nothing more. If you keep to it, the app works without changes. `firmware/soil_node_sim` already follows it, and `tools/mock_esp_server.py` serves the same JSON from a PC.

## 1. What the firmware must include

1. **Wi-Fi hotspot (access point mode)**, not station mode. SSID `SoilNode-XXXX` (XXXX = last 4 hex digits of the MAC), password 8+ characters (default `soilnode123`), channel 1, max 4 clients. IP `192.168.4.1`. `WiFi.mode(WIFI_AP); WiFi.softAP(ssid, pass, 1, 0, 4);`
2. **A web server on port 80** (`WebServer.h`) with two GET endpoints, `/api/info` and `/api/readings`.
3. **Background sensor polling.** Keep the latest values in memory. The HTTP handler only formats cached values. It must never run Modbus itself, because a slow sensor would block the response and the app would time out.
4. **No deep sleep in the prototype.** The app shows live values, so the ESP must stay awake.
5. **Failure handling.** If a sensor does not answer 3 times in a row, send `null` for its values and `"ok": false`. Never send 0, NaN, or a stale value as if it were fresh.
6. **ZTS warm-up.** The ZTS needs about 5 minutes after power-up. Report `"warming_up": true` and `"warmup_s_left"` until then. The app greys those values out.
7. Do not print to Serial in the middle of the HTTP response.

## 2. Endpoints

Base URL `http://192.168.4.1`. Responses are `application/json`, UTF-8, with `Cache-Control: no-store`. The app polls `/api/readings` every 2 s. Any other path returns 404 with `{"error":"not found"}`.

### GET /api/info

```json
{"device":"soil-node","id":"A1B2","fw":"0.1.0","schema":1,"ssid":"SoilNode-A1B2","uptime_s":842}
```

`device` must be exactly `soil-node`.

### GET /api/readings

```json
{
  "schema": 1,
  "seq": 210,
  "uptime_s": 842,
  "air":  { "temp_c": 29.4, "rh_pct": 61.2, "ok": true },
  "soil": { "moisture_pct": 23.5, "temp_c": 27.1, "ph": 6.4, "ok": true,
            "warming_up": false, "warmup_s_left": 0 },
  "npk":  { "n_mgkg": 42, "p_mgkg": 18, "k_mgkg": 96, "ok": true },
  "analog_moisture_pct": 31.0,
  "battery_v": 3.92,
  "gps": null
}
```

| Field | Type | Unit / rule | Source |
|---|---|---|---|
| `schema` | int | Always 1. Bump only if the structure changes. | |
| `seq` | int | +1 each time a full set of readings is refreshed. The app uses it to detect fresh data. | |
| `uptime_s` | int | Seconds since boot. | |
| `air.temp_c`, `air.rh_pct` | number or null | C and % RH, 1 decimal | JXBS-3001-TH-RS |
| `soil.moisture_pct`, `soil.temp_c`, `soil.ph` | number or null | %, C, pH, 1 decimal | ZTS |
| `soil.warming_up`, `soil.warmup_s_left` | bool, int | warm-up state | |
| `npk.n_mgkg`, `p_mgkg`, `k_mgkg` | int or null | mg/kg | JXBS-3001-NPK-RS |
| `*.ok` | bool | true only if every value in that group is fresh and valid | |
| `analog_moisture_pct` | number or null | % after calibration (0 dry air, 100 water) | analog sensor |
| `battery_v` | number or null | volts, 2 decimals, null if no gauge | MAX17048 (optional) |
| `gps` | null or object | leave null. `{"lat":9.9,"lon":78.1,"fix":true}` only with a real GNSS fix. The app falls back to the phone GPS. | optional |

Rules the app relies on:

- Every key is always present. A missing value is `null`, never an absent key.
- Numbers are plain JSON numbers (no quotes, no NaN, no inf).
- Units are fixed. Do not change them without bumping `schema`.
- Response under 1 KB.

## 3. Power and hotspot notes

- The 4G DIP switch stays OFF. No SIM is used.
- Hotspot mode keeps the radio on all the time (roughly 100 to 150 mA). Battery-life tuning comes after the demo.

## 4. Optional later

- `GET /api/history?since=<seq>`: RAM ring buffer so the app can fill gaps when the phone was disconnected.
- GNSS from the board's modem: needs the modem powered (4G DIP switch on) and an antenna. Costs power.

## 5. Acceptance test for the ESP

1. Phone joins `SoilNode-XXXX`, the browser opens `http://192.168.4.1/api/readings` and the JSON appears.
2. Refresh 10 times: `seq` increases and values change when sensors are disturbed.
3. Unplug one sensor: its group shows `null` and `"ok": false` within about 15 s, and the other groups keep updating.
4. The page answers within 1 s every time. If a single-loop version is slower, move polling into its own task.
