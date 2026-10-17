#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cmath>
#include <ctime>

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
constexpr int FIRST_ALLOWED_HOUR = 6;
constexpr int LAST_ALLOWED_HOUR = 23;
constexpr int INVERTER_ONLY_MODE = 2;
constexpr time_t MIN_VALID_EPOCH = 1704067200;
constexpr char TIME_ZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";

bool relayIsOn = false;
bool wasWiFiConnected = false;
bool hasPolledSinceWiFiConnect = false;
unsigned long lastPollAt = 0;
unsigned long lastWiFiRetryAt = 0;

void setRelay(bool enabled) {
  if (relayIsOn == enabled) {
    return;
  }

  relayIsOn = enabled;
  digitalWrite(RELAY_PIN, relayIsOn ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  Serial.printf("Relay %s\n", relayIsOn ? "ON" : "OFF");
}

void failSafeOff(const char* reason) {
  if (relayIsOn) {
    Serial.printf("Fail-safe: relay OFF (%s)\n", reason);
  }
  setRelay(false);
}

bool getLocalTime(struct tm& localTime) {
  const time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH) {
    return false;
  }

  return localtime_r(&now, &localTime) != nullptr;
}

bool isWithinHeatingWindow() {
  struct tm localTime;
  if (!getLocalTime(localTime)) {
    return false;
  }

  return localTime.tm_hour >= FIRST_ALLOWED_HOUR &&
         localTime.tm_hour <= LAST_ALLOWED_HOUR;
}

bool readVictronState(float& voltage, int& inverterMode) {
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

  if (!response["inverterMode"].is<int>()) {
    Serial.println("Node-RED response has no integer inverterMode field.");
    return false;
  }

  inverterMode = response["inverterMode"].as<int>();
  if (inverterMode < 1 || inverterMode > 4) {
    Serial.printf("Rejected invalid Victron inverter mode: %d\n",
                  inverterMode);
    return false;
  }

  return true;
}

void applyRelayLogic(float voltage, int inverterMode) {
  struct tm localTime;
  if (!getLocalTime(localTime)) {
    setRelay(false);
    Serial.println("Relay inhibited: NTP clock is not synchronized.");
    return;
  }

  if (localTime.tm_hour < FIRST_ALLOWED_HOUR ||
      localTime.tm_hour > LAST_ALLOWED_HOUR) {
    setRelay(false);
    Serial.printf("Relay inhibited: local time %02d:%02d is outside 06:00-23:59.\n",
                  localTime.tm_hour, localTime.tm_min);
    return;
  }

  if (inverterMode != INVERTER_ONLY_MODE) {
    setRelay(false);
    Serial.printf("Relay inhibited: Victron mode %d is not Inverter Only.\n",
                  inverterMode);
    return;
  }

  if (voltage >= RELAY_TURN_ON_V) {
    setRelay(true);
  } else if (voltage <= RELAY_TURN_OFF_V) {
    setRelay(false);
  }

  Serial.printf("Battery voltage: %.2f V; Victron mode: %d; relay %s\n",
                voltage, inverterMode, relayIsOn ? "ON" : "OFF");
}

void pollNodeRed() {
  if (WiFi.status() != WL_CONNECTED) {
    if (relayIsOn) {
      failSafeOff("Wi-Fi disconnected");
    }
    return;
  }

  float voltage = 0.0F;
  int inverterMode = 0;
  if (!readVictronState(voltage, inverterMode)) {
    failSafeOff("HTTPS request or response validation failed");
    return;
  }

  applyRelayLogic(voltage, inverterMode);
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

  Serial.printf(
      "Starting %s; polling Node-RED every %lu ms; NTP server: %s; "
      "heating window: 06:00-23:59 Europe/Berlin.\n",
      CLIENT_ID, static_cast<unsigned long>(POLL_INTERVAL_MS), NTP_SERVER);
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
    configTzTime(TIME_ZONE, NTP_SERVER);
    Serial.printf("Started time synchronization with %s.\n", NTP_SERVER);
  }

  if (relayIsOn && !isWithinHeatingWindow()) {
    failSafeOff("outside heating hours or clock is not synchronized");
  }

  if (wifiConnected &&
      (!hasPolledSinceWiFiConnect || now - lastPollAt >= POLL_INTERVAL_MS)) {
    lastPollAt = now;
    hasPolledSinceWiFiConnect = true;
    pollNodeRed();
  }

  delay(20);
}
