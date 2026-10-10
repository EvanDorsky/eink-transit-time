#include "flight.h"

#include <stdio.h>
#include <string.h>

// Leg times, minutes, for this site. Walks are from a pedestrian router plus
// time to reach the platform: home -> C station, home -> LIRR station, and
// the transfer station's A/C platform -> its LIRR platform (up out of the
// subway, up to the elevated platform). The C -> A change at the transfer
// station is same-platform, so it gets no extra time beyond the A departing
// after the C arrives. AirTrain has no live data: a fixed walk + wait + ride
// to the terminals from each station.
static const int WALK_HOME_C_MIN = 5;
static const int WALK_HOME_LIRR_MIN = 21;
static const int WALK_XFER_MIN = 9;
static const int AIRTRAIN_JAMAICA_MIN = 14;
static const int AIRTRAIN_HOWARD_BEACH_MIN = 12;
static const int UBER_PICKUP_MIN = 5;

void pairAdd(PairList *list, time_t from, time_t to) {
  if (list->n < FLIGHT_MAX_PAIRS) {
    list->p[list->n++] = {from, to};
    return;
  }
  int latest = 0;
  for (int i = 1; i < list->n; i++)
    if (list->p[i].from > list->p[latest].from) latest = i;
  if (from < list->p[latest].from) list->p[latest] = {from, to};
}

// Earliest trip departing at or after `t`, or nullptr
static const TripPair *firstFrom(const PairList *list, time_t t) {
  if (!list) return nullptr;
  const TripPair *best = nullptr;
  for (int i = 0; i < list->n; i++) {
    const TripPair &p = list->p[i];
    if (p.from >= t && p.to > p.from && (!best || p.from < best->from))
      best = &p;
  }
  return best;
}

static int minutesUp(time_t secs) { return (int)((secs + 59) / 60); }

int planFlight(const FlightInputs &in, time_t now,
               FlightOption out[FLIGHT_OPTIONS]) {
  int n = 0;

  // Walk to the LIRR
  if (const TripPair *l = firstFrom(in.lirr, now + WALK_HOME_LIRR_MIN * 60)) {
    time_t jfk = l->to + AIRTRAIN_JAMAICA_MIN * 60;
    out[n++] = {FR_WALK_LIRR, minutesUp(jfk - now), l->from, 0};
  }

  // C to the transfer station, then either the LIRR or the A. Each C we can reach is
  // tried; the earliest JFK arrival wins (first C achieving it on a tie).
  bool haveCL = false, haveCA = false;
  FlightOption bestCL = {}, bestCA = {};
  time_t bestCLjfk = 0, bestCAjfk = 0;
  if (in.cTrain) {
    for (int i = 0; i < in.cTrain->n; i++) {
      const TripPair &c = in.cTrain->p[i];
      if (c.from < now + WALK_HOME_C_MIN * 60 || c.to <= c.from) continue;

      if (const TripPair *l =
              firstFrom(in.lirr, c.to + WALK_XFER_MIN * 60)) {
        time_t jfk = l->to + AIRTRAIN_JAMAICA_MIN * 60;
        if (!haveCL || jfk < bestCLjfk ||
            (jfk == bestCLjfk && c.from < bestCL.leg1)) {
          bestCL = {FR_C_LIRR, minutesUp(jfk - now), c.from, l->from};
          bestCLjfk = jfk;
          haveCL = true;
        }
      }
      if (const TripPair *a = firstFrom(in.aTrain, c.to)) {
        time_t jfk = a->to + AIRTRAIN_HOWARD_BEACH_MIN * 60;
        if (!haveCA || jfk < bestCAjfk ||
            (jfk == bestCAjfk && c.from < bestCA.leg1)) {
          bestCA = {FR_C_A, minutesUp(jfk - now), c.from, a->from};
          bestCAjfk = jfk;
          haveCA = true;
        }
      }
    }
  }
  if (haveCL) out[n++] = bestCL;
  if (haveCA) out[n++] = bestCA;

  if (in.uberDriveMin >= 0)
    out[n++] = {FR_UBER, UBER_PICKUP_MIN + in.uberDriveMin, 0, 0};

  // fastest first (n <= 4: insertion sort)
  for (int i = 1; i < n; i++) {
    FlightOption k = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].totalMin > k.totalMin) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = k;
  }
  return n;
}

static void clock(time_t t, char *out, size_t outLen) {
  struct tm lt;
  localtime_r(&t, &lt);
  int h = lt.tm_hour % 12;
  snprintf(out, outLen, "%d:%02d", h ? h : 12, lt.tm_min);
}

void flightLabel(const FlightOption &opt, char *out, size_t outLen) {
  char a[8], b[8];
  switch (opt.route) {
  case FR_WALK_LIRR:
    clock(opt.leg1, a, sizeof(a));
    snprintf(out, outLen, "Walk {LIRR} %s", a);
    break;
  case FR_C_LIRR:
    clock(opt.leg1, a, sizeof(a));
    clock(opt.leg2, b, sizeof(b));
    snprintf(out, outLen, "{C} %s {LIRR} %s", a, b);
    break;
  case FR_C_A:
    clock(opt.leg1, a, sizeof(a));
    clock(opt.leg2, b, sizeof(b));
    snprintf(out, outLen, "{C} %s {A} %s", a, b);
    break;
  case FR_UBER:
    snprintf(out, outLen, "Uber");
    break;
  }
}
