# 6. Testing

## A. Simulator and app checklist

Flash `soil_node_sim`, join `SoilNode-XXXX`, open the app.

- [ ] Connect screen shows the node id and firmware `0.1.0-sim`.
- [ ] Live updates about every 2 s, `seq` keeps increasing.
- [ ] For the first 60 s the soil values are greyed with a warm-up countdown, then normal.
- [ ] Hold UP and DOWN for 1.5 s (or send `n` on the Serial Monitor): after about 12 s the NPK card shows "sensor not responding" and the JSON has `null` and `"ok": false` for NPK. The other groups keep updating.
- [ ] Repeat for SOIL (`z`) and AIR (`a`). Send `0` to clear.
- [ ] Crops screen still works with one sensor failing and says which parameter is missing.
- [ ] Switch the node off: the app shows disconnected within 15 s and recovers when it is back.
- [ ] Leave it connected for 10 minutes or more: Analytics shows the chart and CSV export opens in a spreadsheet app.
- [ ] Hotspot behaviour on a real phone with mobile data on, then off.

## B. API check without the app

```
curl http://192.168.4.1/api/info
curl http://192.168.4.1/api/readings
```

Run the second one repeatedly and watch `seq`. Response time must stay under 1 s.

## C. PC mock (no hardware)

```
python3 tools/mock_esp_server.py --port 8080 [--fail npk|zts|th] [--warmup 60]
```

App node address: `http://<PC-IP>:8080` on a phone on the same Wi-Fi, or `http://10.0.2.2:8080` in the Android emulator.

## D. Verification already done on the simulator firmware

The sketch was compiled and exercised on a host machine with stub Arduino headers: JSON output validated against the contract, the 3-miss null rule, fault injection, and the Arduino IDE auto-prototype ordering. It then ran on the real board and the app connected.

## E. Real-sensor bring-up (to do)

1. Air sensor: working (address 1, RX 5 / TX 6). Confirm readings against a reference thermometer.
2. NPK: see [09-troubleshooting.md](09-troubleshooting.md). Needs a valid frame `01 03 06 ...` in reply to `01 03 00 1E 00 03 65 CD`.
3. ZTS: confirm its real address and baud (ships as 4800, address 1). Let it warm up 5 minutes before judging values.
4. Compare ZTS moisture against the analog sensor and a known soil sample (wet/dry).
5. Port the working read code into `soil_node_api.ino` and run the acceptance test in [04-api-contract.md](04-api-contract.md).
