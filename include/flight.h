#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Flight mode: ranked ways to get from home to JFK, recomputed every
// fetch cycle from the live feeds. Pure logic (no Arduino), so it's unit
// tested on the host with real feed data.
//
// Routes:
//   WALK_LIRR  walk to the home LIRR station -> LIRR to Jamaica -> AirTrain
//   C_LIRR     C from the home station to the transfer station, walk to its
//              LIRR platform -> LIRR to Jamaica -> AirTrain
//   C_A        C to the transfer station, transfer to a Howard Beach-bound A
//              -> AirTrain from Howard Beach
//   UBER       pickup wait + Waze driving time (pushed from HA)
// A route that isn't running (e.g. no LIRR service at the home station on
// weekends) simply produces no option.

// One train trip seen at two stops: departure from the first, arrival at the
// second (epoch seconds)
struct TripPair {
  time_t from;
  time_t to;
};

static const int FLIGHT_MAX_PAIRS = 16;
struct PairList {
  TripPair p[FLIGHT_MAX_PAIRS];
  int n;
};

// Adds a pair, keeping the FLIGHT_MAX_PAIRS earliest departures
void pairAdd(PairList *list, time_t from, time_t to);

enum FlightRoute { FR_WALK_LIRR, FR_C_LIRR, FR_C_A, FR_UBER };

struct FlightOption {
  FlightRoute route;
  int totalMin;  // leave now -> at the JFK terminals, minutes (rounded up)
  time_t leg1;   // first train to catch (C or LIRR), 0 if none
  time_t leg2;   // second train (LIRR or A), 0 if none
};

static const int FLIGHT_OPTIONS = 4;

struct FlightInputs {
  const PairList *cTrain; // C: home station dep -> transfer station arr
  const PairList *aTrain; // A: transfer station dep -> Howard Beach arr
  const PairList *lirr;   // LIRR: home LIRR station dep -> Jamaica arr
  int uberDriveMin;       // Waze driving minutes, -1 if unknown
};

// Fills `out` with every route that's currently possible, fastest first.
// Returns how many.
int planFlight(const FlightInputs &in, time_t now,
               FlightOption out[FLIGHT_OPTIONS]);

// Row label for the panel, with route tokens the renderer draws as badges:
// "{C}" / "{A}" = subway bullet, "{LIRR}" = LIRR badge. Times are local
// ("7:51"), so the caller must have TZ set.
void flightLabel(const FlightOption &opt, char *out, size_t outLen);
