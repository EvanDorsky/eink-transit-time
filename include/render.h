#pragma once
#include <Adafruit_GFX.h>
#include "weather_types.h"

// Pure drawing code, shared between the firmware (GxEPD2 panel) and the
// host-side preview tool (GFXcanvas1). Must not depend on anything but
// Adafruit_GFX.

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

void renderHourly(Adafruit_GFX &gfx, const HourlyInfo *hourly);
