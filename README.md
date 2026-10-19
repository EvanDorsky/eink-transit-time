# eink-transit-time

NYC subway + bus arrival board on an Elecrow CrowPanel 5.79" e-paper (ESP32-S3). Pulls MTA GTFS-realtime (ACE feed, LIRR in flight mode) and BusTime. On its own it shows arrivals; weather, notices, the hourly screen and standby come from Home Assistant over MQTT (see [Home Assistant](#home-assistant)).

## Setup

Dependencies: [PlatformIO](https://platformio.org) (`pio`), a C/C++ compiler, `freetype` (`brew install freetype pkg-config`), Python 3. Arduino libraries are pulled by PlatformIO.

Three gitignored inputs are needed before the first build:

1. **Fonts.** Copy `Helvetica.ttc` into the repo root (macOS: `/System/Library/Fonts/Helvetica.ttc`), then:
   ```sh
   pio pkg install  # fetch libdeps (font tool needs Adafruit GFX)
   make fonts       # writes include/fonts/
   ```
2. **Secrets.** Create `include/secrets.h`:
   ```c
   #pragma once
   #define WIFI_SSID "your-ssid"
   #define WIFI_PASS "your-password"
   #define BUSTIME_API_KEY "..."   // https://register.developer.obanyc.com
   #define MQTT_USER "..."         // broker found via mDNS at homeassistant.local:1883
   #define MQTT_PASS "..."
   ```
   Changing WiFi = edit this file and reflash.
3. **Stops.** See [Customizing stops](#customizing-stops).

## Flashing

Every build, USB flash and OTA needs the site variables in the environment (see [Customizing stops](#customizing-stops)), so start each shell with:

```sh
set -a; . ./site.env; set +a
```

First flash is over USB (the serial port is auto-detected):

```sh
make flashusb                  # pio run -e crowpanel_epaper_579 -t upload
make monitor                   # serial log
```

After that, update over WiFi (device advertises `transit-display.local`; the OTA password is set once, under `[ota]` in `platformio.ini`):

```sh
make ota                       # pio run -e crowpanel_ota -t upload
make udplog                    # live logs over UDP :5555, no cable needed
```

## Customizing stops

All site config comes from env vars, never the repo. `cp site.env.example site.env` (gitignored), fill it in, and source it before building.

- **Stop ids (required).** The build fails on any missing/invalid one.
  - Subway / LIRR: GTFS stop ids from MTA static GTFS `stops.txt`. `TRANSIT_SUBWAY_STOP` is the parent id; `N`/`S` are appended.
  - Bus: BusTime `MonitoringRef` stop codes (bustime.mta.info).
- **Line and wording (optional).** The subway feed, primary/fallback route, row labels and bus badge default to this display's (C/A on the ACE feed; see `include/site.h`). Set the matching `TRANSIT_*` vars in `site.env` to change them; any subway route letter or number works.

Flight mode (`src/flight.cpp`) is the one site-specific part left in code: it plans specific routes to JFK (C/A to the AirTrain, or the LIRR to Jamaica) with fixed walk and transfer times.

## Home Assistant

Optional. The display registers its sensors and controls (commute mode, standby, flight mode) through MQTT discovery; HA feeds it weather, notices and the hourly forecast. [AGENTS.md](AGENTS.md) is the setup guide.

## Dev

- `make preview`: renders the screen on the host to `preview.png` (`NORTH=3,8 SOUTH=5 BUSW=7 BUSE=4 ROUTE=C` override the sample data; `host/preview.cpp` also has `--commute`, `--flight`, `--hourly` and `--message` modes, and `WX_*`/`NOTE_*` env vars for the side column).
- Flight planner test: see the command at the top of `test/flight/test_flight.cpp`.
- `make symbols`: `compile_commands.json` for clangd.
