# 7. Status and roadmap

Last updated 5 Nov 2026.

## Done

- Hardware design and enclosure built (two shelves, LCD, buttons, buzzer, probes mounted).
- Simulator firmware that exposes the final API, LCD pages, buttons, buzzer and fault injection. Running on the board.
- API contract written for the hardware engineer.
- Android app built and verified through the Connect, Live, Crops, Map, Analytics and Settings screens. Connected to the simulated node.
- Air temperature and humidity sensor reads correctly on the bench.
- Passive buzzer polarity sorted out; LCD and buttons working.

## Open problems

| Problem | Notes |
|---|---|
| NPK sensor gives no valid reply | Raw probe saw only silence or an idle line stuck low (0x00 bytes). Power is 5 V as labelled. Suspects: receiver path (RO divider), A/B swapped, missing common ground, driver module. Next: swap A and B, measure RO to GND with RO disconnected, check the 1k/2k divider, then re-run the raw probe and report the idle byte count. |
| ZTS soil sensor not tested | Check its address and baud first (ships 4800, address 1). 5 minute warm-up. |
| Real-sensor firmware untested | `soil_node_api.ino` has never been compiled or run on hardware. |
| Pin map has three versions | See [02-wiring-and-pins.md](02-wiring-and-pins.md). Settle on one and update the docs. |
| LCD showed garbage once | Suspected old firmware or supply noise. Re-check with the final sketch; if it returns, check 5 V supply, shorten SDA/SCL, add a capacitor. |

## Next steps

1. Fix the NPK link, then bring up the ZTS.
2. Merge the working read code, LCD, buttons and buzzer into one real-sensor firmware.
3. Run the simulator checklist in [06-testing.md](06-testing.md) with the app.
4. Test the hotspot behaviour on a real phone with mobile data on and off.
5. Agronomist review of the crop ranges.
6. Release keystore and `--split-per-abi` APKs.
7. Hand `esp-api-contract` to the hardware engineer (this repo's [04-api-contract.md](04-api-contract.md)).

## Later

- Battery-life work: deep sleep, switched sensor power, Wi-Fi uplink with MQTT (see [08-design-notes.md](08-design-notes.md)).
- Node GPS from the A7670E GNSS (modem powered, antenna needed).
- Solar version with a pole-mounted panel for the student build.
- `/api/history` so the app can fill gaps.
