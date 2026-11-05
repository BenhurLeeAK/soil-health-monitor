# 5. Android app

Flutter (Dart), Android only. Built with Claude Code in a separate project folder (`AgroNex`) on the developer's PC. **The app source is not in this repository yet.** Add it as `app/` or as its own repository (see the end of this page).

## What it does

The phone joins the node's own Wi-Fi hotspot `SoilNode-XXXX` and talks to `http://192.168.4.1`. No internet is needed in the field.

| Screen | Content |
|---|---|
| Connect | Instructions to join the hotspot, button to open Wi-Fi settings, checks `/api/info`, shows node id and firmware version, manual IP field for testing against the mock |
| Live | Air, soil, NPK, analog moisture and battery cards. `null` shows "sensor not responding". Soil values are greyed with a countdown while `warming_up` is true. Stale marker when `seq` does not change for 15 s. |
| Crops | Top 3 crop matches with percentage and a reason per parameter, from `assets/crops.json` |
| Map | Phone GPS position on an offline-capable OpenStreetMap view (tiles cached with flutter_map_tile_caching), "Save point" stores position with the current readings |
| Analytics | Charts per parameter over 1 h / 24 h / 7 d / session, min/avg/max, time in target band, soil health score, CSV export |
| Settings | Node IP, poll interval, units, theme, selected crop, clear data |

Position comes from the **phone's** GPS. The node has no GPS. If the API ever returns a `gps` object with `fix: true`, the app shows it as "Node GPS".

## Verified so far

Steps 1 to 5 of the build were verified by Claude Code on a Galaxy A36: Connect, Live, Crops, Map with offline tiles, Analytics with a local log written about every 10 s, CSV export (with BOM), Settings. 121 unit tests pass. A release APK (about 62.6 MB, signed with the debug key) was built and installed. The user also connected the app to the simulated node.

## Still to test on a real node

- The "force Wi-Fi usage" behaviour (traffic to 192.168.4.1 must go over the hotspot even though it has no internet) with mobile data on and off.
- Warm-up greying, fault injection and crop suggestion with a failing sensor, using the simulator. See [06-testing.md](06-testing.md).
- Analytics gaps after at least 10 minutes of logging.
- Location permission denied path. Map download refusal in airplane mode.

## Crop engine

Pure Dart, unit-tested. Each parameter has four numbers `[min, optMin, optMax, max]`. Fit is 1.0 inside the optimum band, falls linearly to 0 at min and max. Match percentage is a weighted average over available parameters (pH 0.25, moisture 0.20, temperature 0.15, N 0.15, P 0.10, K 0.15, re-normalised when some are missing). The ranges are approximate general values for the prototype and **must be reviewed by an agronomist** before the app gives real advice. The NPK values are indicative because the sensor estimates nutrients from conductivity.

## Building

```
flutter build apk --release
flutter build apk --release --split-per-abi   # smaller per-phone APKs (recommended for sharing)
```

The APK is currently signed with the debug key. Create a release keystore before distributing widely. The OpenStreetMap tile requests carry a User-Agent contact address set in the app. Use a project or team address there, not a personal one, before publishing.

## Adding the app to this repository

From the app project folder on the PC:

```
cd path\to\AgroNex
git init            # skip if it is already a git repo
git remote add origin https://github.com/<user>/<app-repo>.git
git add . && git commit -m "Soil Monitor app" && git push -u origin main
```

Or copy it into this repository as `app/` (delete `build/`, `.dart_tool/` and any `*.jks` or `key.properties` first). Do not commit signing keys.
