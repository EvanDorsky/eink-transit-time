# PlatformIO pre-build script: site-specific config comes from environment
# variables, not from the repo, so the location isn't committed. Each
# variable becomes a string macro of the same name (e.g.
# -DTRANSIT_SUBWAY_STOP="A12").
#
# Set them in your shell before building; site.env.example lists them.
# REQUIRED stop ids have no default: a missing or malformed one stops the
# build (a display flashed with blank stop ids would just show nothing).
# OPTIONAL ones override the defaults in include/site.h.
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

bad = []
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
