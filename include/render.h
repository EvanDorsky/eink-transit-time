#pragma once
#include <Adafruit_GFX.h>

// Pure drawing code, shared between the firmware (GxEPD2 panel) and the
// host-side preview tool (GFXcanvas1). Must not depend on anything but
// Adafruit_GFX.

// Current conditions pushed from Home Assistant over MQTT. `cond` is an HA
// weather state string (sunny, rainy, partlycloudy, ...); hi/lo are today's
// forecast range in Fahrenheit. Drawn in the top-right corner in every mode;
// pass a null/invalid one and the corner is simply left empty.
struct WeatherInfo {
  char cond[24];
  int hi;
  int lo;
  bool valid;
  // Rain outlook for the middle of the side column: hours until the first
  // wet hour in the next 12 (0 = now, -1 = none), and that hour as a short
  // local label ("6pm"). Left blank when dry.
  int rainIn;
  char rainAt[8];
};

// One notification for the bottom of the side column, pushed from HA.
// `pct` >= 0 adds a progress bar between title and text (print jobs);
// idx/count drive the pager dots when several notices are rotating.
struct Notice {
  char title[16];
  char text[20];
  int pct;
  uint8_t idx;
  uint8_t count;
};
void renderArrivals(Adafruit_GFX &gfx, const char *routeNorth,
                    const char *routeSouth, const char *northRow,
                    const char *southRow, const char *busWestRow,
                    const char *busEastRow, const WeatherInfo *wx,
                    const Notice *note);
void renderCommute(Adafruit_GFX &gfx, const char *routeLetter,
                   const char *minutesText, const WeatherInfo *wx,
                   const Notice *note);
void renderMessage(Adafruit_GFX &gfx, const char *msg);

// Flight mode: up to four ranked routes to JFK. `label` uses route tokens
// ({C}, {A}, {LIRR}) drawn as badges; `minutes` is the total trip time.
struct FlightRow {
  char label[48];
  int minutes;
};
void renderFlight(Adafruit_GFX &gfx, const FlightRow *rows, int count,
                  const WeatherInfo *wx, const Notice *note);

// Hourly outlook screen (toggled with the HOME key): the next HOURLY_N hours
// as time labels, condition icons, a temperature line and precipitation
// probability bars. Pushed from HA on transit-display/hourly.
static const int HOURLY_N = 8;
struct HourlyHour {
  char t[6];     // "12pm"
  char cond[20]; // HA weather condition
  int temp;      // Fahrenheit
  int pop;       // precipitation probability, 0-100
};
struct HourlyInfo {
  HourlyHour h[HOURLY_N];
  int count;
  bool valid;
};
void renderHourly(Adafruit_GFX &gfx, const HourlyInfo *hourly);
