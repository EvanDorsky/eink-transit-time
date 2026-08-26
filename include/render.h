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
};
void renderArrivals(Adafruit_GFX &gfx, const char *routeLetter,
                    const char *northRow, const char *southRow,
                    const char *busWestRow, const char *busEastRow,
                    const WeatherInfo *wx);
void renderCommute(Adafruit_GFX &gfx, const char *routeLetter,
                   const char *minutesText, const WeatherInfo *wx);
void renderMessage(Adafruit_GFX &gfx, const char *msg);
