#pragma once
#include <stddef.h>
#include <time.h>
#include "weather_types.h"

// Weather data by Open-Meteo.com (CC BY 4.0), fetched by the device itself
// (src/weather.cpp). This is the pure part, host-tested in test/weather:
// turning a forecast response into what the panel draws.
//
// The request (see weather.cpp) asks for current weather_code/is_day, the
// next 13 hourly temperature_2m / precipitation_probability / precipitation
// / weather_code / is_day, and today's daily temperature_2m_max/min, in
// Fahrenheit with timeformat=unixtime.

// WMO weather code (Open-Meteo's weather_code) -> the HA-style condition
// string the renderer draws an icon for ("sunny", "rainy", ...)
const char *wmoToCond(int code, bool isDay);

// Fills `wx` (side column: icon, hi/lo, first wet hour in the next 12) and
// `hourly` (HOME-key screen: the next HOURLY_N hours) from a response.
// Hour labels ("6pm") are local time, so TZ must be set. Returns false on a
// malformed response, leaving both untouched.
bool openMeteoParse(const char *json, size_t len, time_t now, WeatherInfo *wx,
                    HourlyInfo *hourly);
