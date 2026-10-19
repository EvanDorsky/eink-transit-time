#pragma once
// Site wording and lines: which subway feed and routes the display follows,
// and what the panel (and the HA sensor names) call each row. The defaults
// below are this display's. Override any of them with an environment
// variable of the same name at build time (scripts/site_env.py; see
// site.env.example). The stop ids themselves are always required from the
// environment and have no defaults.

// MTA GTFS-realtime feed path after .../mtagtfsfeeds/ ("nyct%2Fgtfs-ace",
// "nyct%2Fgtfs-bdfm", "nyct%2Fgtfs-g", "nyct%2Fgtfs-nqrw", "nyct%2Fgtfs" for
// the 1-7, ...). It must carry both routes below.
#ifndef TRANSIT_SUBWAY_FEED
#define TRANSIT_SUBWAY_FEED "nyct%2Fgtfs-ace"
#endif
// Route shown normally, and the one shown when it isn't running (late nights
// the C stops and the A runs local through the same platforms)
#ifndef TRANSIT_ROUTE_PRIMARY
#define TRANSIT_ROUTE_PRIMARY "C"
#endif
#ifndef TRANSIT_ROUTE_FALLBACK
#define TRANSIT_ROUTE_FALLBACK "A"
#endif

// Row labels, top to bottom. Keep them short (about 10 characters) so they
// clear the minute numbers.
#ifndef TRANSIT_LABEL_NORTH
#define TRANSIT_LABEL_NORTH "Manhattan" // subway, N-bound platform
#endif
#ifndef TRANSIT_LABEL_SOUTH
#define TRANSIT_LABEL_SOUTH "Euclid" // subway, S-bound platform
#endif
#ifndef TRANSIT_LABEL_BUS_WEST
#define TRANSIT_LABEL_BUS_WEST "Downtown" // bus stop TRANSIT_BUS_STOP_WEST
#endif
#ifndef TRANSIT_LABEL_BUS_EAST
#define TRANSIT_LABEL_BUS_EAST "Eastbound" // bus stop TRANSIT_BUS_STOP_EAST
#endif

// Bus badge on both bus rows: one or two route names, space-separated
#ifndef TRANSIT_BUS_BADGE
#define TRANSIT_BUS_BADGE "B25 B26"
#endif
