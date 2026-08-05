#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("CrowPanel 5.79\" e-paper — hello");
}

void loop() {
  Serial.println("alive");
  delay(1000);
}
