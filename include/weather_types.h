#pragma once
// Weather data the panel draws: plain structs, no Arduino or GFX dependency,
// so the Open-Meteo parser (openmeteo.cpp) builds and tests on the host.

// Current conditions, from Open-Meteo (weather.cpp). `cond` is an HA-style
// weather condition string (sunny, rainy, partlycloudy, ...); hi/lo are today's
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

// Hourly outlook screen (toggled with the HOME key): the next HOURLY_N hours
// as time labels, condition icons, a temperature line and precipitation
// probability bars. From Open-Meteo (weather.cpp).
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
