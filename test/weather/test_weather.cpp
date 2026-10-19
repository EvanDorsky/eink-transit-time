// Host test for the Open-Meteo parser:
//   c++ -std=c++17 -Iinclude -I.pio/libdeps/crowpanel_epaper_579/ArduinoJson/src \
//     test/weather/test_weather.cpp src/openmeteo.cpp -o /tmp/tw && TZ=America/New_York /tmp/tw
// (needs the libdeps checkout: `pio pkg install`)
#include "openmeteo.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <string.h>

static int fails = 0;
static void check(bool ok, const char *what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) fails++;
}

// 2026-10-19 16:00 EDT, the start of the current hour; "now" is 16:30
static const long H0 = 1792440000;
static const time_t NOW = H0 + 1800;

// A response with 13 hourly entries starting at H0. `precip` and `code`
// override single hours: {hour index, value}
static std::string response(int curCode, int curDay, int wetHour, float mm,
                            int nightFrom = 99) {
  std::string t, temp, pop, pr, wc, day;
  for (int i = 0; i < 13; i++) {
    const char *sep = i ? "," : "";
    t += sep + std::to_string(H0 + i * 3600);
    temp += sep + std::to_string(64 - i);
    pop += sep + std::to_string(i == wetHour ? 70 : 5);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", i == wetHour ? mm : 0.0f);
    pr += sep + std::string(buf);
    wc += sep + std::to_string(i == wetHour ? 61 : 2);
    day += sep + std::string(i >= nightFrom ? "0" : "1");
  }
  return "{\"current\":{\"time\":" + std::to_string(NOW) +
         ",\"weather_code\":" + std::to_string(curCode) +
         ",\"is_day\":" + std::to_string(curDay) + "},"
         "\"hourly\":{\"time\":[" + t + "],\"temperature_2m\":[" + temp +
         "],\"precipitation_probability\":[" + pop + "],\"precipitation\":[" +
         pr + "],\"weather_code\":[" + wc + "],\"is_day\":[" + day + "]},"
         "\"daily\":{\"time\":[1792382400],\"temperature_2m_max\":[64.4],"
         "\"temperature_2m_min\":[50.6]}}";
}

int main() {
  WeatherInfo wx;
  HourlyInfo h;

  // dry afternoon
  std::string dry = response(0, 1, -1, 0);
  check(openMeteoParse(dry.c_str(), dry.size(), NOW, &wx, &h), "dry: parses");
  check(wx.valid && strcmp(wx.cond, "sunny") == 0, "dry: sunny now");
  check(wx.hi == 64 && wx.lo == 51, "dry: hi/lo rounded");
  check(wx.rainIn == -1 && wx.rainAt[0] == 0, "dry: no rain alert");
  check(h.valid && h.count == HOURLY_N, "dry: 8 hourly entries");
  check(strcmp(h.h[0].t, "4pm") == 0 && strcmp(h.h[8 - 1].t, "11pm") == 0,
        "dry: hourly labels 4pm..11pm local");
  check(h.h[0].temp == 64 && h.h[0].pop == 5 &&
            strcmp(h.h[0].cond, "partlycloudy") == 0,
        "dry: hourly fields");

  // rain at 7pm: the 16:00 entry is the current hour, so 19:00 is 3 h out
  std::string later = response(2, 1, 3, 0.8f);
  openMeteoParse(later.c_str(), later.size(), NOW, &wx, &h);
  check(wx.rainIn == 3 && strcmp(wx.rainAt, "7pm") == 0,
        "rain at 7pm: alert in 3 h");

  // raining now
  std::string now = response(61, 1, 0, 1.2f);
  openMeteoParse(now.c_str(), now.size(), NOW, &wx, &h);
  check(wx.rainIn == 0 && strcmp(wx.cond, "rainy") == 0, "raining now: 0 h");

  // a trace below Open-Meteo's 0.1 mm resolution is not rain
  std::string trace = response(2, 1, 2, 0.0f);
  openMeteoParse(trace.c_str(), trace.size(), NOW, &wx, &h);
  check(wx.rainIn == -1, "0.0 mm with a rain code: no alert");

  // night: clear sky reads as clear-night, in the hourly entries too
  std::string night = response(0, 0, -1, 0, 3);
  openMeteoParse(night.c_str(), night.size(), NOW, &wx, &h);
  check(strcmp(wx.cond, "clear-night") == 0, "night: clear-night now");

  // malformed responses leave the old values alone
  WeatherInfo keep = wx;
  check(!openMeteoParse("{\"error\":true}", 14, NOW, &wx, &h) &&
            strcmp(wx.cond, keep.cond) == 0,
        "error body: rejected, old weather kept");
  check(!openMeteoParse("not json", 8, NOW, &wx, &h), "garbage: rejected");

  // WMO code mapping spot checks
  check(strcmp(wmoToCond(3, true), "cloudy") == 0, "wmo 3 = cloudy");
  check(strcmp(wmoToCond(45, true), "fog") == 0, "wmo 45 = fog");
  check(strcmp(wmoToCond(65, true), "pouring") == 0, "wmo 65 = pouring");
  check(strcmp(wmoToCond(75, false), "snowy") == 0, "wmo 75 = snowy");
  check(strcmp(wmoToCond(95, true), "lightning-rainy") == 0,
        "wmo 95 = lightning-rainy");

  printf(fails ? "%d FAILED\n" : "all weather tests passed\n", fails);
  return fails ? 1 : 0;
}
