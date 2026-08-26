#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "mqtt.h"
#include "log.h"
#include "secrets.h"

// The HAOS Pi's DHCP address can move; resolve its mDNS name instead.
// ArduinoOTA.begin() has already started the mDNS responder by mqttSetup().
static const char *BROKER_MDNS_HOST = "homeassistant";
static const uint16_t BROKER_PORT = 1883;

static const char *CLIENT_ID = "transit-display";
static const char *TOPIC_AVAIL = "transit-display/availability";
static const char *TOPIC_STATE = "transit-display/state";

// Commute-mode config topics (see mqtt.h for the contract)
static const char *TOPIC_COMMUTE_STATE = "transit-display/commute/state";
static const char *TOPIC_COMMUTE_SET_ENABLED = "transit-display/commute/enabled/set";
static const char *TOPIC_COMMUTE_SET_START = "transit-display/commute/start/set";
static const char *TOPIC_COMMUTE_SET_END = "transit-display/commute/end/set";
static const char *TOPIC_COMMUTE_SET_JSON = "transit-display/commute/set";

// Weather pushed from HA (retained); see mqtt.h for the payload shape
static const char *TOPIC_WEATHER = "transit-display/weather";

static WiFiClient mqttNet;
static PubSubClient mqtt(mqttNet);
static IPAddress brokerIp;

// Defaults: weekday mornings 7-10am (Sun=bit0..Sat=bit6, Mon-Fri = 0x3E)
static CommuteConfig commuteCfg = {true, 7, 10, 0x3E};
static Preferences commutePrefs; // NVS namespace kept open for saves

CommuteConfig mqttGetCommuteConfig() { return commuteCfg; }

static WeatherInfo weather = {};

WeatherInfo mqttGetWeather() { return weather; }

static void saveCommuteCfg() {
  commutePrefs.putBool("en", commuteCfg.enabled);
  commutePrefs.putUChar("start", commuteCfg.startHour);
  commutePrefs.putUChar("end", commuteCfg.endHour);
  commutePrefs.putUChar("days", commuteCfg.daysMask);
}

static void loadCommuteCfg() {
  commutePrefs.begin("commute", false);
  commuteCfg.enabled = commutePrefs.getBool("en", commuteCfg.enabled);
  commuteCfg.startHour = commutePrefs.getUChar("start", commuteCfg.startHour);
  commuteCfg.endHour = commutePrefs.getUChar("end", commuteCfg.endHour);
  commuteCfg.daysMask = commutePrefs.getUChar("days", commuteCfg.daysMask);
}

static void publishCommuteState() {
  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"enabled\":%s,\"start\":%u,\"end\":%u,\"days\":%u}",
           commuteCfg.enabled ? "true" : "false", commuteCfg.startHour,
           commuteCfg.endHour, commuteCfg.daysMask);
  mqtt.publish(TOPIC_COMMUTE_STATE, payload, true);
}

static uint8_t clampHour(long v) {
  if (v < 0) return 0;
  if (v > 23) return 23;
  return (uint8_t)v;
}

static void mqttCallback(char *topic, byte *payload, unsigned int len) {
  char buf[160];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  if (strcmp(topic, TOPIC_WEATHER) == 0) {
    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
      LOGB("weather: bad json ignored");
      return;
    }
    const char *c = doc["cond"] | "";
    snprintf(weather.cond, sizeof(weather.cond), "%s", c);
    weather.hi = doc["hi"] | 0;
    weather.lo = doc["lo"] | 0;
    weather.valid = weather.cond[0] != 0;
    LOGB("weather: %s %d-%d", weather.cond, weather.lo, weather.hi);
    return;
  }

  if (strcmp(topic, TOPIC_COMMUTE_SET_ENABLED) == 0) {
    commuteCfg.enabled = strcasecmp(buf, "ON") == 0 || strcmp(buf, "1") == 0;
  } else if (strcmp(topic, TOPIC_COMMUTE_SET_START) == 0) {
    commuteCfg.startHour = clampHour(strtol(buf, nullptr, 10));
  } else if (strcmp(topic, TOPIC_COMMUTE_SET_END) == 0) {
    commuteCfg.endHour = clampHour(strtol(buf, nullptr, 10));
  } else if (strcmp(topic, TOPIC_COMMUTE_SET_JSON) == 0) {
    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
      LOGB("commute: bad json ignored");
      return;
    }
    if (doc["enabled"].is<bool>()) commuteCfg.enabled = doc["enabled"];
    if (doc["start"].is<long>()) commuteCfg.startHour = clampHour(doc["start"]);
    if (doc["end"].is<long>()) commuteCfg.endHour = clampHour(doc["end"]);
    if (doc["days"].is<long>()) commuteCfg.daysMask = (uint8_t)(doc["days"].as<long>() & 0x7F);
  } else {
    return;
  }
  saveCommuteCfg();
  publishCommuteState();
  LOGB("commute: enabled=%d window=%u-%u days=0x%02x", commuteCfg.enabled,
       commuteCfg.startHour, commuteCfg.endHour, commuteCfg.daysMask);
}

// One retained discovery config per sensor, published on every (re)connect.
// Keys are HA MQTT-discovery abbreviations to keep payloads small.
struct SensorDef {
  const char *key;   // unique_id suffix + discovery topic leaf
  const char *name;
  const char *tpl;   // value_template against TOPIC_STATE json
  const char *unit;  // nullptr = no unit
};

static const SensorDef SENSORS[] = {
    {"train_north", "Train Manhattan-bound",
     "{{ value_json.north.split(',')[0] if value_json.north else '' }}", "min"},
    {"train_south", "Train Euclid-bound",
     "{{ value_json.south.split(',')[0] if value_json.south else '' }}", "min"},
    {"bus_west", "Bus downtown-bound",
     "{{ value_json.bus_west.split(',')[0] if value_json.bus_west else '' }}",
     "min"},
    {"bus_east", "Bus eastbound",
     "{{ value_json.bus_east.split(',')[0] if value_json.bus_east else '' }}",
     "min"},
    {"route", "Active route", "{{ value_json.route }}", nullptr},
};

// Shared device block so all entities group under one HA device
static const char *DEV_BLOCK =
    "\"dev\":{\"ids\":[\"transit-display\"],"
    "\"name\":\"Transit Display\",\"mf\":\"DIY\","
    "\"mdl\":\"CrowPanel 5.79 e-ink\"}";

static void publishDiscovery() {
  for (const SensorDef &s : SENSORS) {
    char topic[96];
    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/transit_display/%s/config", s.key);
    char payload[640];
    size_t pos = snprintf(
        payload, sizeof(payload),
        "{\"uniq_id\":\"transit_display_%s\",\"name\":\"%s\","
        "\"stat_t\":\"%s\",\"val_tpl\":\"%s\","
        "\"avty_t\":\"%s\",",
        s.key, s.name, TOPIC_STATE, s.tpl, TOPIC_AVAIL);
    if (s.unit)
      pos += snprintf(payload + pos, sizeof(payload) - pos,
                      "\"unit_of_meas\":\"%s\",", s.unit);
    snprintf(payload + pos, sizeof(payload) - pos, "%s}", DEV_BLOCK);
    mqtt.publish(topic, payload, true);
  }

  // Commute mode controls: one switch + two hour numbers
  char payload[640];
  snprintf(payload, sizeof(payload),
           "{\"uniq_id\":\"transit_display_commute_enabled\","
           "\"name\":\"Commute mode\",\"icon\":\"mdi:train\","
           "\"stat_t\":\"%s\","
           "\"val_tpl\":\"{{ 'ON' if value_json.enabled else 'OFF' }}\","
           "\"cmd_t\":\"%s\",\"avty_t\":\"%s\",%s}",
           TOPIC_COMMUTE_STATE, TOPIC_COMMUTE_SET_ENABLED, TOPIC_AVAIL,
           DEV_BLOCK);
  mqtt.publish("homeassistant/switch/transit_display/commute_enabled/config",
               payload, true);

  struct { const char *key, *name, *tpl, *cmd; } nums[] = {
      {"commute_start", "Commute start hour", "{{ value_json.start }}",
       TOPIC_COMMUTE_SET_START},
      {"commute_end", "Commute end hour", "{{ value_json.end }}",
       TOPIC_COMMUTE_SET_END},
  };
  for (auto &n : nums) {
    char topic[96];
    snprintf(topic, sizeof(topic),
             "homeassistant/number/transit_display/%s/config", n.key);
    snprintf(payload, sizeof(payload),
             "{\"uniq_id\":\"transit_display_%s\",\"name\":\"%s\","
             "\"stat_t\":\"%s\",\"val_tpl\":\"%s\",\"cmd_t\":\"%s\","
             "\"min\":0,\"max\":23,\"step\":1,\"icon\":\"mdi:clock-outline\","
             "\"avty_t\":\"%s\",%s}",
             n.key, n.name, TOPIC_COMMUTE_STATE, n.tpl, n.cmd, TOPIC_AVAIL,
             DEV_BLOCK);
    mqtt.publish(topic, payload, true);
  }
}

static bool resolveBroker() {
  IPAddress ip = MDNS.queryHost(BROKER_MDNS_HOST, 2000);
  if (ip == IPAddress()) {
    LOGB("mqtt: mDNS lookup for %s.local failed", BROKER_MDNS_HOST);
    return false;
  }
  if (ip != brokerIp)
    LOGB("mqtt: broker %s.local -> %s", BROKER_MDNS_HOST,
         ip.toString().c_str());
  brokerIp = ip;
  return true;
}

static bool mqttConnect() {
  if (brokerIp == IPAddress() && !resolveBroker()) return false;
  mqtt.setServer(brokerIp, BROKER_PORT);
  if (!mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS, TOPIC_AVAIL, 0, true,
                    "offline")) {
    // Stale cached IP (Pi moved networks?) — re-resolve for the next attempt
    brokerIp = IPAddress();
    return false;
  }
  mqtt.publish(TOPIC_AVAIL, "online", true);
  mqtt.subscribe(TOPIC_COMMUTE_SET_ENABLED);
  mqtt.subscribe(TOPIC_COMMUTE_SET_START);
  mqtt.subscribe(TOPIC_COMMUTE_SET_END);
  mqtt.subscribe(TOPIC_COMMUTE_SET_JSON);
  mqtt.subscribe(TOPIC_WEATHER);
  publishDiscovery();
  publishCommuteState();
  LOGB("mqtt: connected");
  return true;
}

void mqttSetup() {
  // Discovery payloads exceed PubSubClient's 256-byte default
  mqtt.setBufferSize(1024);
  mqtt.setCallback(mqttCallback);
  loadCommuteCfg();
}

void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  static uint32_t lastAttempt = 0;
  if (lastAttempt != 0 && millis() - lastAttempt < 5000) return;
  lastAttempt = millis();
  if (!mqttConnect()) LOGB("mqtt: connect failed (rc=%d)", mqtt.state());
}

void mqttPublishState(const char *route, const char *northRow,
                      const char *southRow, const char *busWestRow,
                      const char *busEastRow) {
  if (!mqtt.connected()) return;
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"route\":\"%s\",\"north\":\"%s\",\"south\":\"%s\","
           "\"bus_west\":\"%s\",\"bus_east\":\"%s\"}",
           route, northRow, southRow, busWestRow, busEastRow);
  mqtt.publish(TOPIC_STATE, payload, true);
}
