#include <Arduino.h>

namespace {

constexpr uint8_t RELAY_PIN = 13;
constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;
constexpr uint8_t LED_PIN = 2;
constexpr uint8_t LED_ON_LEVEL = HIGH;
constexpr uint8_t LED_OFF_LEVEL = LOW;
constexpr unsigned long SWITCH_INTERVAL_MS = 500;

bool relayIsOn = false;

void setRelay(bool enabled) {
  relayIsOn = enabled;
  digitalWrite(RELAY_PIN, relayIsOn ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  digitalWrite(LED_PIN, relayIsOn ? LED_ON_LEVEL : LED_OFF_LEVEL);
  Serial.printf("Relay %s\n", relayIsOn ? "ON" : "OFF");
}

}  // namespace

void setup() {
  Serial.begin(115200);

  digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF_LEVEL);
  pinMode(LED_PIN, OUTPUT);
  setRelay(false);
}

void loop() {
  delay(SWITCH_INTERVAL_MS);
  setRelay(!relayIsOn);
}
