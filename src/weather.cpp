#include "weather.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "log.h"
#include "openmeteo.h"

// Open-Meteo forecast for the display's location (TRANSIT_LAT/TRANSIT_LON
// from site.env): current conditions, today's hi/lo and the next 13 hours.
// Free for non-commercial use, no API key; ~1 KB per response. Without a
// location (both unset) nothing is fetched and the panel shows no weather.
#if defined(TRANSIT_LAT) && defined(TRANSIT_LON)
#define WEATHER_ENABLED 1
static const char *WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=" TRANSIT_LAT "&longitude=" TRANSIT_LON
    "&current=weather_code,is_day"
    "&hourly=temperature_2m,precipitation_probability,precipitation,"
    "weather_code,is_day"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&temperature_unit=fahrenheit&timezone=auto&timeformat=unixtime"
    "&forecast_days=1&forecast_hours=13";
#else
#define WEATHER_ENABLED 0
#endif

// Forecasts update hourly at the source; every 15 min keeps the umbrella
// alert and "now" icon timely (~100 requests/day, far under the free limit)
static const uint32_t WEATHER_REFRESH_MS = 15 * 60 * 1000;
static const uint32_t WEATHER_RETRY_MS = 2 * 60 * 1000;

static WeatherInfo weather = {};
static HourlyInfo hourly = {};

WeatherInfo weatherGet() { return weather; }
HourlyInfo hourlyGet() { return hourly; }

#if WEATHER_ENABLED
static bool fetchWeather() {
  static WiFiClientSecure tls;
  static bool tlsInited = false;
  if (!tlsInited) {
    tls.setInsecure(); // public read-only data; skip CA validation
    tlsInited = true;
  }
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(tls, WEATHER_URL)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGB("weather: http %d", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  WeatherInfo w;
  HourlyInfo h;
  if (!openMeteoParse(body.c_str(), body.length(), time(nullptr), &w, &h)) {
    LOGB("weather: unexpected response ignored");
    return false;
  }
  weather = w;
  hourly = h;
  LOGB("weather: %s %d-%d rain_in=%d at=%s (open-meteo)", w.cond, w.lo, w.hi,
       w.rainIn, w.rainAt);
  return true;
}

#endif

void weatherPoll() {
#if WEATHER_ENABLED
  static uint32_t lastTry = 0;
  static bool ok = false;
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t wait = ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS;
  if (lastTry != 0 && millis() - lastTry < wait) return;
  lastTry = millis();
  ok = fetchWeather();
#endif
}
