// Host test for the flight-mode planner:
//   c++ -std=c++17 -Iinclude test/flight/test_flight.cpp src/flight.cpp -o /tmp/tf && TZ=America/New_York /tmp/tf
#include "flight.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const time_t T0 = 1790000000; // arbitrary "now"
static time_t m(int minutes) { return T0 + minutes * 60; }

static void check(bool ok, const char *what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) assert(false);
}

int main() {
  PairList c = {}, a = {}, l = {};
  // C: leaves home at +4 (too soon: 5-min walk), +8, +20; 2 min ride
  pairAdd(&c, m(4), m(6));
  pairAdd(&c, m(8), m(10));
  pairAdd(&c, m(20), m(22));
  // A from Xfer: +12, +30; 22 min to Howard Beach
  pairAdd(&a, m(12), m(34));
  pairAdd(&a, m(30), m(52));
  // LIRR from Xfer: +18, +25, +45; 12 min to Jamaica
  pairAdd(&l, m(18), m(30));
  pairAdd(&l, m(25), m(37));
  pairAdd(&l, m(45), m(57));

  FlightOption o[FLIGHT_OPTIONS];
  FlightInputs in = {&c, &a, &l, 35};
  int n = planFlight(in, T0, o);
  check(n == 4, "all four routes present");
  for (int i = 1; i < n; i++) check(o[i - 1].totalMin <= o[i].totalMin, "sorted fastest first");

  for (int i = 0; i < n; i++) {
    switch (o[i].route) {
    case FR_WALK_LIRR: // walk 21 -> first LIRR >= +21 is +25 -> Jamaica +37 -> +14 = 51
      check(o[i].leg1 == m(25) && o[i].totalMin == 51, "walk+LIRR catches the +25 LIRR, 51 min");
      break;
    case FR_C_LIRR: // C +8 -> Xfer +10 -> +9 walk = +19 -> LIRR +25 -> 51 (the +4 C is unreachable)
      check(o[i].leg1 == m(8) && o[i].leg2 == m(25) && o[i].totalMin == 51, "C+LIRR: C +8, LIRR +25, 51 min");
      break;
    case FR_C_A: // C +8 -> +10 -> A +12 -> HB +34 -> +12 = 46
      check(o[i].leg1 == m(8) && o[i].leg2 == m(12) && o[i].totalMin == 46, "C+A: C +8, A +12, 46 min");
      break;
    case FR_UBER:
      check(o[i].totalMin == 40, "Uber = 5 pickup + 35 drive");
      break;
    }
  }

  // C/LIRR misaligned: C arrives Xfer +10, walk -> +19, the +18 LIRR is missed
  PairList l2 = {};
  pairAdd(&l2, m(18), m(30));
  pairAdd(&l2, m(48), m(60));
  FlightInputs in2 = {&c, nullptr, &l2, -1};
  n = planFlight(in2, T0, o);
  bool sawCL = false;
  for (int i = 0; i < n; i++)
    if (o[i].route == FR_C_LIRR) { sawCL = true; check(o[i].leg2 == m(48), "misaligned C waits for the next LIRR"); }
  check(sawCL, "C+LIRR still offered when misaligned");

  // No LIRR running (weekend) and no Uber data: only C+A
  PairList empty = {};
  FlightInputs in3 = {&c, &a, &empty, -1};
  n = planFlight(in3, T0, o);
  check(n == 1 && o[0].route == FR_C_A, "no LIRR rows when the LIRR isn't running");

  // Labels
  FlightOption opt = {FR_C_LIRR, 51, m(8), m(25)};
  char buf[48];
  flightLabel(opt, buf, sizeof(buf));
  printf("     label: \"%s\"\n", buf);
  check(strstr(buf, "{C} ") == buf && strstr(buf, "{LIRR} "), "label has route tokens");

  // pairAdd keeps the earliest departures when full
  PairList big = {};
  for (int i = 40; i > 0; i--) pairAdd(&big, m(i), m(i + 2));
  bool earliest = true;
  for (int i = 0; i < big.n; i++) if (big.p[i].from > m(FLIGHT_MAX_PAIRS)) earliest = false;
  check(big.n == FLIGHT_MAX_PAIRS && earliest, "pairAdd keeps the earliest 16");
  printf("all flight tests passed\n");
}
