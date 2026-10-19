# PlatformIO pre-build script: site-specific config comes from environment
# variables, not from the repo, so the location isn't committed. Each
# variable becomes a string macro of the same name (e.g.
# -DTRANSIT_SUBWAY_STOP="A12").
#
# Set them in your shell before building; site.env.example lists them.
# REQUIRED stop ids have no default: a missing or malformed one stops the
# build (a display flashed with blank stop ids would just show nothing).
# The LOCATION is optional: without it the display shows no weather, and the
# build says so. OPTIONAL ones override the defaults in include/site.h.
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)

import os
import re

STOP_ID = re.compile(r"^[A-Za-z0-9]{1,16}$")
REQUIRED = {
    "TRANSIT_SUBWAY_STOP": "GTFS parent stop id of the home subway station (N/S appended)",
    "TRANSIT_XFER_STOP": "GTFS stop id of the flight-mode transfer platform (southbound)",
    "TRANSIT_AIRPORT_SUBWAY_STOP": "GTFS stop id where the A meets the AirTrain",
    "TRANSIT_LIRR_ORIGIN_STOP": "LIRR GTFS stop id of the home LIRR station",
    "TRANSIT_LIRR_JFK_STOP": "LIRR GTFS stop id where the LIRR meets the AirTrain",
    "TRANSIT_BUS_STOP_WEST": "MTA BusTime MonitoringRef, downtown-bound stop",
    "TRANSIT_BUS_STOP_EAST": "MTA BusTime MonitoringRef, other direction",
}
# Labels go on the panel and into HA names: plain text, no quotes or
# backslashes (they become C string literals)
LABEL = re.compile(r"^[A-Za-z0-9 .,'&/()-]{1,14}$")
OPTIONAL = {
    "TRANSIT_SUBWAY_FEED": (re.compile(r"^nyct%2Fgtfs(-[a-z]+)?$"), "MTA feed path, e.g. nyct%2Fgtfs-ace"),
    "TRANSIT_ROUTE_PRIMARY": (re.compile(r"^[A-Z0-9]$"), "one route letter or digit"),
    "TRANSIT_ROUTE_FALLBACK": (re.compile(r"^[A-Z0-9]$"), "one route letter or digit"),
    "TRANSIT_LABEL_NORTH": (LABEL, "row label"),
    "TRANSIT_LABEL_SOUTH": (LABEL, "row label"),
    "TRANSIT_LABEL_BUS_WEST": (LABEL, "row label"),
    "TRANSIT_LABEL_BUS_EAST": (LABEL, "row label"),
    "TRANSIT_BUS_BADGE": (re.compile(r"^[A-Za-z0-9]{1,4}( [A-Za-z0-9]{1,4})?$"), "one or two route names, e.g. 'B25 B26'"),
}

# The display's location, for its weather (Open-Meteo). Two decimals (~1 km)
# is plenty and keeps the exact address out of the request.
COORD = re.compile(r"^-?[0-9]{1,3}(\.[0-9]{1,6})?$")
LOCATION = {
    "TRANSIT_LAT": ("latitude for the weather forecast, e.g. 40.71", -90, 90),
    "TRANSIT_LON": ("longitude for the weather forecast, e.g. -74.01", -180, 180),
}

bad = []
location = {name: os.environ.get(name, "") for name in LOCATION}
if not any(location.values()):
    print("\nsite_env.py: TRANSIT_LAT / TRANSIT_LON not set, so this build shows "
          "no weather. Set both in site.env to get it (see site.env.example).\n")
else:
    for name, (what, lo, hi) in LOCATION.items():
        value = location[name]
        if not COORD.match(value) or not lo <= float(value) <= hi:
            bad.append(f"  {name}  ({what}, set both or neither): "
                       f"{'unset' if not value else repr(value)}")
            continue
        env.Append(CPPDEFINES=[(name, env.StringifyMacro(value))])  # noqa: F821

for name, what in REQUIRED.items():
    value = os.environ.get(name, "")
    if not STOP_ID.match(value):
        bad.append(f"  {name}  ({what}): {'unset' if not value else repr(value)}")
        continue
    env.Append(CPPDEFINES=[(name, env.StringifyMacro(value))])  # noqa: F821

for name, (pattern, what) in OPTIONAL.items():
    value = os.environ.get(name, "")
    if not value:
        continue  # include/site.h default
    if not pattern.match(value):
        bad.append(f"  {name}  ({what}): {value!r}")
        continue
    env.Append(CPPDEFINES=[(name, env.StringifyMacro(value))])  # noqa: F821

if bad:
    print("\nsite_env.py: missing/invalid site config (see site.env.example):")
    print("\n".join(bad))
    env.Exit(1)  # noqa: F821
