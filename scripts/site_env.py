# PlatformIO pre-build script: site-specific config (which stops this display
# watches) comes from environment variables, not from the repo, so the
# location isn't committed. Each variable becomes a string macro of the same
# name (e.g. -DTRANSIT_SUBWAY_STOP="A12").
#
# Set them in your shell before building; site.env.example lists them. A
# missing or malformed value stops the build: a display flashed with blank
# stop ids would just show nothing.
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)

import os
import re

REQUIRED = {
    "TRANSIT_SUBWAY_STOP": "GTFS parent stop id of the home subway station (N/S appended)",
    "TRANSIT_XFER_STOP": "GTFS stop id of the flight-mode transfer platform (southbound)",
    "TRANSIT_AIRPORT_SUBWAY_STOP": "GTFS stop id where the A meets the AirTrain",
    "TRANSIT_LIRR_ORIGIN_STOP": "LIRR GTFS stop id of the home LIRR station",
    "TRANSIT_LIRR_JFK_STOP": "LIRR GTFS stop id where the LIRR meets the AirTrain",
    "TRANSIT_BUS_STOP_WEST": "MTA BusTime MonitoringRef, downtown-bound stop",
    "TRANSIT_BUS_STOP_EAST": "MTA BusTime MonitoringRef, other direction",
}
# Stop ids are short alphanumerics; anything else is a typo or an unexpanded
# placeholder, and must never reach a URL or a strcmp.
VALID = re.compile(r"^[A-Za-z0-9]{1,16}$")

bad = []
for name, what in REQUIRED.items():
    value = os.environ.get(name, "")
    if not VALID.match(value):
        bad.append(f"  {name}  ({what}): {'unset' if not value else repr(value)}")
        continue
    env.Append(CPPDEFINES=[(name, env.StringifyMacro(value))])  # noqa: F821

if bad:
    print("\nsite_env.py: missing/invalid site config (see site.env.example):")
    print("\n".join(bad))
    env.Exit(1)  # noqa: F821
