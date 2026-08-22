#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
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

static WiFiClient mqttNet;
static PubSubClient mqtt(mqttNet);
static IPAddress brokerIp;

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
    snprintf(payload + pos, sizeof(payload) - pos,
             "\"dev\":{\"ids\":[\"transit-display\"],"
             "\"name\":\"Transit Display\",\"mf\":\"DIY\","
             "\"mdl\":\"CrowPanel 5.79 e-ink\"}}");
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
  publishDiscovery();
  LOGB("mqtt: connected");
  return true;
}

void mqttSetup() {
  // Discovery payloads exceed PubSubClient's 256-byte default
  mqtt.setBufferSize(1024);
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
