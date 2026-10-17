#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cmath>

#include "secrets.h"

namespace {

constexpr uint8_t RELAY_PIN = 13;
constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;
constexpr float RELAY_TURN_ON_V = 53.8F;
constexpr float RELAY_TURN_OFF_V = 52.1F;
constexpr float MIN_VALID_BATTERY_V = 30.0F;
constexpr float MAX_VALID_BATTERY_V = 65.0F;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;

bool relayIsOn = false;
bool wasWiFiConnected = false;
bool hasPolledSinceWiFiConnect = false;
unsigned long lastPollAt = 0;
unsigned long lastWiFiRetryAt = 0;

void setRelay(bool enabled) {
  relayIsOn = enabled;
  digitalWrite(RELAY_PIN, relayIsOn ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  Serial.printf("Relay %s\n", relayIsOn ? "ON" : "OFF");
}

void failSafeOff(const char* reason) {
  Serial.printf("Battery voltage unavailable; fail-safe: relay OFF (%s)\n",
                reason);
  setRelay(false);
}

bool readBatteryVoltage(float& voltage) {
  WiFiClientSecure tlsClient;
  tlsClient.setCACert(NODERED_CA_CERTIFICATE_PEM);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(tlsClient, NODERED_BATTERY_VOLTAGE_URL)) {
    Serial.println("Could not initialize HTTPS request to Node-RED.");
    return false;
  }

  http.setAuthorization(NODERED_HTTP_USER, NODERED_HTTP_PASSWORD);
  http.addHeader("Accept", "application/json");

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    Serial.printf("Node-RED HTTPS request failed with status %d.\n",
                  statusCode);
    http.end();
    return false;
  }

  JsonDocument response;
  const DeserializationError error = deserializeJson(response, http.getStream());
  http.end();

  if (error) {
    Serial.printf("Invalid JSON from Node-RED: %s\n", error.c_str());
    return false;
  }

  if (!response["voltage"].is<float>() &&
      !response["voltage"].is<double>() &&
      !response["voltage"].is<int>()) {
    Serial.println("Node-RED response has no numeric voltage field.");
    return false;
  }

  voltage = response["voltage"].as<float>();
  if (!std::isfinite(voltage) || voltage < MIN_VALID_BATTERY_V ||
      voltage > MAX_VALID_BATTERY_V) {
    Serial.printf("Rejected out-of-range battery voltage: %.2f V\n", voltage);
    return false;
  }

  return true;
}

void applyVoltageHysteresis(float voltage) {
  if (voltage >= RELAY_TURN_ON_V) {
    setRelay(true);
  } else if (voltage <= RELAY_TURN_OFF_V) {
    setRelay(false);
  }

  Serial.printf("Battery voltage: %.2f V; relay %s\n", voltage,
                relayIsOn ? "ON" : "OFF");
}

void pollNodeRed() {
  if (WiFi.status() != WL_CONNECTED) {
    if (relayIsOn) {
      failSafeOff("Wi-Fi disconnected");
    }
    return;
  }

  float voltage = 0.0F;
  if (!readBatteryVoltage(voltage)) {
    failSafeOff("HTTPS request or response validation failed");
    return;
  }

  applyVoltageHysteresis(voltage);
}

}  // namespace

void setup() {
  Serial.begin(115200);

  digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  relayIsOn = false;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CLIENT_ID);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Starting %s; polling Node-RED every %lu ms.\n", CLIENT_ID,
                static_cast<unsigned long>(POLL_INTERVAL_MS));
}

void loop() {
  const unsigned long now = millis();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (relayIsOn) {
      failSafeOff("Wi-Fi disconnected");
    }
    wasWiFiConnected = false;
    hasPolledSinceWiFiConnect = false;
    if (now - lastWiFiRetryAt >= WIFI_RETRY_INTERVAL_MS) {
      lastWiFiRetryAt = now;
      WiFi.reconnect();
    }
  } else if (!wasWiFiConnected) {
    wasWiFiConnected = true;
    hasPolledSinceWiFiConnect = false;
  }

  if (wifiConnected &&
      (!hasPolledSinceWiFiConnect || now - lastPollAt >= POLL_INTERVAL_MS)) {
    lastPollAt = now;
    hasPolledSinceWiFiConnect = true;
    pollNodeRed();
  }

  delay(20);
}
