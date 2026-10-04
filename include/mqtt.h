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
// Call once when WiFi comes back after a drop: clears the cached broker IP
// and the reconnect backoff so the next mqttLoop() re-resolves and dials.
void mqttOnWifiReconnect();
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

// Pause, to save panel refresh cycles (GDEY0579T93 is rated for 1M) when
// nobody's looking: HA publishes "ON"/"OFF" retained on
// transit-display/pause/set (an automation drives it: nights + away); the
// device echoes it retained on transit-display/pause/state and exposes a
// "Pause updates" switch via discovery. While paused the panel stops fetching
// and is left blank (white, no message); "BLANK" is accepted as a synonym for
// "ON". The HOME key wakes it for a few minutes.
bool mqttGetPaused();

// Flight mode: the panel ranks routes to JFK instead of showing arrivals
// (flight.h). HA (or anyone) publishes "ON"/"OFF" retained on
// transit-display/flight/set; echoed on transit-display/flight/state and
// exposed as a "Flight mode" switch. The EXIT button toggles it via
// mqttSetFlight(), which republishes the retained set topic so the new state
// survives reconnects. Flight mode overrides the pause.
bool mqttGetFlight();
void mqttSetFlight(bool on);
// Waze driving minutes to JFK, pushed (retained) by HA on
// transit-display/flight/uber; -1 until one arrives.
int mqttGetUberMin();
