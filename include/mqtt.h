#pragma once
#include <stdint.h>
#include "render.h"

// MQTT bridge to Home Assistant (Mosquitto add-on on the HAOS Pi).
// Publishes availability (LWT) + arrival rows; entities are created in HA
// via MQTT discovery. Display behavior is unaffected except for commute
// mode, whose window is configurable from HA (see below).

// Commute-mode window config. Set from HA via MQTT and persisted in NVS:
//   transit-display/commute/enabled/set   "ON" / "OFF"
//   transit-display/commute/start/set     hour 0-23
//   transit-display/commute/end/set       hour 0-23 (exclusive)
//   transit-display/commute/set           JSON {enabled,start,end,days}
// Current config is published retained as JSON on
// transit-display/commute/state. days is a Sun=bit0..Sat=bit6 mask,
// default Mon-Fri (0x3E); it has no HA discovery entity — set it via the
// JSON topic if ever needed.
struct CommuteConfig {
  bool enabled;
  uint8_t startHour;
  uint8_t endHour;
  uint8_t daysMask;
};

void mqttSetup();
void mqttLoop();
void mqttPublishState(const char *routeNorth, const char *routeSouth,
                      const char *northRow, const char *southRow,
                      const char *busWestRow, const char *busEastRow);
CommuteConfig mqttGetCommuteConfig();

// Latest weather pushed from HA on transit-display/weather (retained JSON:
// {"cond":"sunny","hi":81,"lo":63,"rain_in":5,"rain_at":"6pm"}; the rain
// keys are optional and mean "no rain coming" when absent or rain_in < 0).
// Invalid until the first message arrives.
WeatherInfo mqttGetWeather();

// Notices for the bottom of the side column, pushed retained from HA on
// transit-display/notify as {"items":[{"title":"Printing","text":"1h20 left",
// "pct":42}, ...]}. pct is optional. Up to MAX_NOTICES are kept; an empty
// list (or empty payload) clears the zone. Returns the number stored.
static const int MAX_NOTICES = 6;
int mqttGetNotices(Notice *out, int max);

// Hourly outlook for the HOME-key screen, pushed retained from HA on
// transit-display/hourly as {"h":[{"t":"1pm","c":"rainy","f":75,"p":40},
// ...]} (t = local hour label, c = HA condition, f = temp in F, p = precip
// probability %). Invalid until the first message arrives.
HourlyInfo mqttGetHourly();
