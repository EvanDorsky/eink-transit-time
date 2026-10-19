#include "openmeteo.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// A wet hour, for the umbrella alert: at least Open-Meteo's 0.1 mm
// resolution of rain or snow (water equivalent) forecast for it
static const float WET_MM = 0.1f;
// The umbrella alert looks this many hours ahead (the response carries 13
// hourly entries: the current hour plus the next 12)
static const int RAIN_LOOKAHEAD_H = 12;

const char *wmoToCond(int code, bool isDay) {
  switch (code) {
  case 0:
  case 1: // clear / mainly clear
    return isDay ? "sunny" : "clear-night";
  case 2:
    return "partlycloudy";
  case 3:
    return "cloudy";
  case 45:
  case 48:
    return "fog";
  case 51: case 53: case 55: // drizzle
  case 61: case 63:          // rain, slight/moderate
  case 80: case 81:          // showers, slight/moderate
    return "rainy";
  case 65: case 82: // heavy rain / violent showers
    return "pouring";
  case 56: case 57: case 66: case 67: // freezing drizzle / rain
    return "snowy-rainy";
  case 71: case 73: case 75: case 77: case 85: case 86: // snow
    return "snowy";
  case 95: case 96: case 99: // thunderstorm (with hail)
    return "lightning-rainy";
  default:
    return "cloudy";
  }
}

// "6pm" style local hour label
static void hourLabel(time_t t, char *out, size_t outLen) {
  struct tm lt;
  localtime_r(&t, &lt);
  int h = lt.tm_hour % 12;
  snprintf(out, outLen, "%d%s", h ? h : 12, lt.tm_hour < 12 ? "am" : "pm");
}

bool openMeteoParse(const char *json, size_t len, time_t now, WeatherInfo *wx,
                    HourlyInfo *hourly) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  JsonArrayConst times = doc["hourly"]["time"];
  JsonArrayConst temps = doc["hourly"]["temperature_2m"];
  JsonArrayConst pops = doc["hourly"]["precipitation_probability"];
  JsonArrayConst precip = doc["hourly"]["precipitation"];
  JsonArrayConst codes = doc["hourly"]["weather_code"];
  JsonArrayConst days = doc["hourly"]["is_day"];
  JsonVariantConst cur = doc["current"]["weather_code"];
  JsonVariantConst hi = doc["daily"]["temperature_2m_max"][0];
  JsonVariantConst lo = doc["daily"]["temperature_2m_min"][0];
  size_t n = times.size();
  if (n == 0 || temps.size() != n || pops.size() != n || precip.size() != n ||
      codes.size() != n || days.size() != n || !cur.is<int>() ||
      !hi.is<float>() || !lo.is<float>())
    return false;

  WeatherInfo w = {};
  snprintf(w.cond, sizeof(w.cond), "%s",
           wmoToCond(cur.as<int>(), doc["current"]["is_day"] | 1));
  w.hi = (int)lroundf(hi.as<float>());
  w.lo = (int)lroundf(lo.as<float>());
  w.rainIn = -1;
  for (size_t i = 0; i < n; i++) {
    time_t t = times[i].as<long>();
    // the first entry is the hour we're in (t <= now): rain in it is now
    int in = t <= now ? 0 : (int)((t - now + 3599) / 3600);
    if (in > RAIN_LOOKAHEAD_H) break;
    if (precip[i].as<float>() >= WET_MM) {
      w.rainIn = in;
      hourLabel(t, w.rainAt, sizeof(w.rainAt));
      break;
    }
  }
  w.valid = true;

  HourlyInfo h = {};
  for (size_t i = 0; i < n && h.count < HOURLY_N; i++) {
    HourlyHour &o = h.h[h.count++];
    hourLabel(times[i].as<long>(), o.t, sizeof(o.t));
    snprintf(o.cond, sizeof(o.cond), "%s",
             wmoToCond(codes[i].as<int>(), days[i].as<int>() != 0));
    o.temp = (int)lroundf(temps[i].as<float>());
    o.pop = pops[i].as<int>();
  }
  h.valid = h.count > 0;

  *wx = w;
  *hourly = h;
  return true;
}
