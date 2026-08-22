#pragma once

// MQTT bridge to Home Assistant (Mosquitto add-on on the HAOS Pi).
// Publishes availability (LWT) + arrival rows; entities are created in HA
// via MQTT discovery. Display behavior is unaffected.
void mqttSetup();
void mqttLoop();
void mqttPublishState(const char *route, const char *northRow,
                      const char *southRow, const char *busWestRow,
                      const char *busEastRow);
