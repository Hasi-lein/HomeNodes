#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <esp_https_server.h>
#include <mbedtls/base64.h>

#include <cmath>
#include <ctime>
#include <cstring>

#include "secrets.h"

namespace {

constexpr uint8_t RELAY_PIN = 13;
constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;
constexpr uint8_t DHT20_SDA_PIN = 21;
constexpr uint8_t DHT20_SCL_PIN = 22;
constexpr uint8_t DHT20_I2C_ADDRESS = 0x38;
constexpr char DHT20_SENSOR_ID[] = "room-climate";
constexpr char DHT20_SENSOR_NAME[] = "Temperatur und Luftfeuchtigkeit";
constexpr char HEATER_RELAY_ID[] = "infrared-heater-relay";
constexpr char HEATER_RELAY_NAME[] = "Infrarot Heizung";
constexpr float RELAY_TURN_ON_V = 53.8F;
constexpr float RELAY_TURN_OFF_V = 52.1F;
constexpr float MIN_VALID_BATTERY_V = 30.0F;
constexpr float MAX_VALID_BATTERY_V = 65.0F;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long SENSOR_POLL_INTERVAL_MS = 10000;
constexpr unsigned long MAX_SENSOR_DATA_AGE_MS = 30000;
constexpr unsigned long MAX_VICTRON_DATA_AGE_MS = 120000;
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;
constexpr int FIRST_ALLOWED_HOUR = 6;
constexpr int LAST_ALLOWED_HOUR = 23;
constexpr int INVERTER_ONLY_MODE = 2;
constexpr time_t MIN_VALID_EPOCH = 1704067200;
constexpr size_t MAX_AUTH_HEADER_LENGTH = 160;
constexpr size_t MAX_REQUEST_BODY_LENGTH = 128;
constexpr char TIME_ZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";

struct ControlState {
  bool heaterEnabled;
  bool relayIsOn;
  bool hasVictronData;
  float batteryVoltage;
  int inverterMode;
  unsigned long victronDataReceivedAt;
  bool hasDht20Data;
  float temperatureC;
  float relativeHumidityPercent;
  unsigned long dht20DataReceivedAt;
};

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
ControlState controlState = {false, false, false, 0.0F, 0, 0,
                            false, 0.0F, 0.0F, 0};
httpd_handle_t httpsServer = nullptr;
String basicAuthHeader;
bool wasWiFiConnected = false;
bool hasPolledSinceWiFiConnect = false;
bool mdnsStarted = false;
bool dht20Initialized = false;
unsigned long lastPollAt = 0;
unsigned long lastWiFiRetryAt = 0;
unsigned long lastSensorPollAt = 0;

ControlState getControlState() {
  portENTER_CRITICAL(&stateMux);
  const ControlState snapshot = controlState;
  portEXIT_CRITICAL(&stateMux);
  return snapshot;
}

void setRelay(bool enabled) {
  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (controlState.relayIsOn != enabled) {
    controlState.relayIsOn = enabled;
    digitalWrite(RELAY_PIN, enabled ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    changed = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (changed) {
    Serial.printf("Relay %s\n", enabled ? "ON" : "OFF");
  }
}

void failSafeOff(const char* reason) {
  const ControlState state = getControlState();
  if (state.relayIsOn) {
    Serial.printf("Fail-safe: relay OFF (%s)\n", reason);
  }

  portENTER_CRITICAL(&stateMux);
  controlState.hasVictronData = false;
  portEXIT_CRITICAL(&stateMux);
  setRelay(false);
}

uint8_t dht20Crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) != 0
                ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

bool initializeDht20() {
  Wire.beginTransmission(DHT20_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("DHT20 not detected at I2C address 0x38.");
    return false;
  }

  Wire.beginTransmission(DHT20_I2C_ADDRESS);
  Wire.write(0xBE);
  Wire.write(0x08);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    Serial.println("Could not initialize the DHT20 sensor.");
    return false;
  }

  delay(10);
  Serial.println("DHT20 sensor initialized.");
  return true;
}

bool readDht20(float& temperature, float& humidity) {
  Wire.beginTransmission(DHT20_I2C_ADDRESS);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    Serial.println("DHT20 measurement command failed.");
    return false;
  }

  delay(80);
  if (Wire.requestFrom(DHT20_I2C_ADDRESS, static_cast<uint8_t>(7)) != 7) {
    Serial.println("DHT20 returned an incomplete measurement.");
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  uint8_t data[7];
  for (uint8_t& value : data) {
    value = static_cast<uint8_t>(Wire.read());
  }

  if ((data[0] & 0x80) != 0) {
    Serial.println("DHT20 measurement is still busy.");
    return false;
  }
  if ((data[0] & 0x08) == 0) {
    Serial.println("DHT20 calibration is not enabled.");
    return false;
  }
  if (dht20Crc8(data, 6) != data[6]) {
    Serial.println("DHT20 measurement checksum failed.");
    return false;
  }

  const uint32_t rawHumidity =
      (static_cast<uint32_t>(data[1]) << 12) |
      (static_cast<uint32_t>(data[2]) << 4) | (data[3] >> 4);
  const uint32_t rawTemperature =
      (static_cast<uint32_t>(data[3] & 0x0F) << 16) |
      (static_cast<uint32_t>(data[4]) << 8) | data[5];

  humidity = static_cast<float>(rawHumidity) * 100.0F / 1048576.0F;
  temperature =
      static_cast<float>(rawTemperature) * 200.0F / 1048576.0F - 50.0F;
  if (!std::isfinite(temperature) || !std::isfinite(humidity) ||
      temperature < -40.0F || temperature > 80.0F || humidity < 0.0F ||
      humidity > 100.0F) {
    Serial.println("DHT20 returned out-of-range sensor data.");
    return false;
  }

  return true;
}

void pollDht20() {
  if (!dht20Initialized) {
    dht20Initialized = initializeDht20();
    if (!dht20Initialized) {
      return;
    }
  }

  float temperature = 0.0F;
  float humidity = 0.0F;
  if (readDht20(temperature, humidity)) {
    portENTER_CRITICAL(&stateMux);
    controlState.hasDht20Data = true;
    controlState.temperatureC = temperature;
    controlState.relativeHumidityPercent = humidity;
    controlState.dht20DataReceivedAt = millis();
    portEXIT_CRITICAL(&stateMux);

    Serial.printf("DHT20: temperature %.2f C, humidity %.2f %%\n",
                  temperature, humidity);
  }
}

bool getLocalTime(struct tm& localTime) {
  const time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH) {
    return false;
  }

  return localtime_r(&now, &localTime) != nullptr;
}

const char* getInhibitReason(const ControlState& state, bool clockValid,
                            const struct tm& localTime) {
  if (!state.heaterEnabled) {
    return "disabled_by_api";
  }
  if (!clockValid) {
    return "clock_not_synchronized";
  }
  if (localTime.tm_hour < FIRST_ALLOWED_HOUR ||
      localTime.tm_hour > LAST_ALLOWED_HOUR) {
    return "outside_heating_hours";
  }
  if (!state.hasVictronData ||
      millis() - state.victronDataReceivedAt > MAX_VICTRON_DATA_AGE_MS) {
    return "victron_data_unavailable";
  }
  if (state.inverterMode != INVERTER_ONLY_MODE) {
    return "inverter_not_in_inverter_only_mode";
  }
  return "automatic_control";
}

void applyRelayLogic() {
  const ControlState state = getControlState();
  struct tm localTime = {};
  const bool clockValid = getLocalTime(localTime);
  const char* inhibitReason =
      getInhibitReason(state, clockValid, localTime);

  if (std::strcmp(inhibitReason, "automatic_control") != 0) {
    setRelay(false);
    if (state.heaterEnabled) {
      Serial.printf("Relay inhibited: %s\n", inhibitReason);
    }
    return;
  }

  if (state.batteryVoltage >= RELAY_TURN_ON_V) {
    setRelay(true);
  } else if (state.batteryVoltage <= RELAY_TURN_OFF_V) {
    setRelay(false);
  }
}

void setHeaterEnabled(bool enabled) {
  portENTER_CRITICAL(&stateMux);
  controlState.heaterEnabled = enabled;
  portEXIT_CRITICAL(&stateMux);

  if (!enabled) {
    setRelay(false);
  } else {
    applyRelayLogic();
  }
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
  const DeserializationError error =
      deserializeJson(response, http.getStream());
  http.end();

  if (error) {
    Serial.printf("Invalid JSON from Node-RED: %s\n", error.c_str());
    return false;
  }

  const JsonVariantConst voltageValue =
      response["sensors"]["battery"]["voltage"];
  if (!voltageValue.is<float>() && !voltageValue.is<double>() &&
      !voltageValue.is<int>()) {
    Serial.println(
        "Node-RED response has no numeric sensors.battery.voltage field.");
    return false;
  }

  voltage = voltageValue.as<float>();
  if (!std::isfinite(voltage) || voltage < MIN_VALID_BATTERY_V ||
      voltage > MAX_VALID_BATTERY_V) {
    Serial.printf("Rejected out-of-range battery voltage: %.2f V\n", voltage);
    return false;
  }

  const JsonVariantConst inverterModeValue =
      response["sensors"]["inverter"]["mode"];
  if (!inverterModeValue.is<int>()) {
    Serial.println(
        "Node-RED response has no integer sensors.inverter.mode field.");
    return false;
  }

  inverterMode = inverterModeValue.as<int>();
  if (inverterMode < 1 || inverterMode > 4) {
    Serial.printf("Rejected invalid Victron inverter mode: %d\n",
                  inverterMode);
    return false;
  }

  return true;
}

void pollNodeRed() {
  if (WiFi.status() != WL_CONNECTED) {
    failSafeOff("Wi-Fi disconnected");
    return;
  }

  float voltage = 0.0F;
  int inverterMode = 0;
  if (!readVictronState(voltage, inverterMode)) {
    failSafeOff("HTTPS request or response validation failed");
    return;
  }

  portENTER_CRITICAL(&stateMux);
  controlState.batteryVoltage = voltage;
  controlState.inverterMode = inverterMode;
  controlState.hasVictronData = true;
  controlState.victronDataReceivedAt = millis();
  portEXIT_CRITICAL(&stateMux);

  Serial.printf("Victron poll: battery voltage %.2f V; inverter mode %d\n",
                voltage, inverterMode);
  applyRelayLogic();
}

void sendJson(httpd_req_t* request, const char* status,
              JsonDocument& response) {
  String body;
  serializeJson(response, body);
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_send(request, body.c_str(), body.length());
}

bool isAuthorized(httpd_req_t* request) {
  const size_t headerLength =
      httpd_req_get_hdr_value_len(request, "Authorization");
  if (headerLength == 0 || headerLength >= MAX_AUTH_HEADER_LENGTH) {
    return false;
  }

  char header[MAX_AUTH_HEADER_LENGTH];
  if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                  sizeof(header)) != ESP_OK) {
    return false;
  }

  if (basicAuthHeader.length() != headerLength) {
    return false;
  }

  uint8_t difference = 0;
  for (size_t i = 0; i < headerLength; ++i) {
    difference |= static_cast<uint8_t>(header[i] ^ basicAuthHeader.charAt(i));
  }
  return difference == 0;
}

bool requireAuthorization(httpd_req_t* request) {
  if (isAuthorized(request)) {
    return true;
  }

  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_hdr(request, "WWW-Authenticate",
                     "Basic realm=\"HomeNodes client\"");
  httpd_resp_set_type(request, "application/json");
  httpd_resp_sendstr(request, "{\"error\":\"unauthorized\"}");
  return false;
}

void fillStatusResponse(JsonDocument& response) {
  const ControlState state = getControlState();
  struct tm localTime = {};
  const bool clockValid = getLocalTime(localTime);
  const unsigned long now = millis();
  const bool sensorDataIsFresh =
      state.hasDht20Data &&
      now - state.dht20DataReceivedAt <= MAX_SENSOR_DATA_AGE_MS;

  JsonObject client = response["client"].to<JsonObject>();
  client["clientId"] = CLIENT_ID;
  client["status"] =
      WiFi.status() == WL_CONNECTED && httpsServer != nullptr ? "on" : "off";
  JsonArray methods = client["methods"].to<JsonArray>();
  JsonObject statusMethod = methods.add<JsonObject>();
  statusMethod["method"] = "GET";
  statusMethod["path"] = "/api/v1/status";
  JsonObject relayMethod = methods.add<JsonObject>();
  relayMethod["method"] = "POST";
  relayMethod["path"] = "/api/v1/relay";

  JsonArray sensors = response["sensors"].to<JsonArray>();
  JsonObject climateSensor = sensors.add<JsonObject>();
  climateSensor["id"] = DHT20_SENSOR_ID;
  climateSensor["name"] = DHT20_SENSOR_NAME;
  climateSensor["type"] = "DHT20";
  climateSensor["status"] = sensorDataIsFresh ? "on" : "off";
  if (sensorDataIsFresh) {
    climateSensor["temperatureC"] = state.temperatureC;
    climateSensor["relativeHumidityPercent"] =
        state.relativeHumidityPercent;
  } else {
    climateSensor["temperatureC"] = nullptr;
    climateSensor["relativeHumidityPercent"] = nullptr;
  }

  JsonArray actuators = response["actuators"].to<JsonArray>();
  JsonObject heaterRelay = actuators.add<JsonObject>();
  heaterRelay["id"] = HEATER_RELAY_ID;
  heaterRelay["name"] = HEATER_RELAY_NAME;
  heaterRelay["type"] = "relay";
  heaterRelay["status"] = state.relayIsOn ? "on" : "off";
  heaterRelay["enabled"] = state.heaterEnabled;

  JsonObject clock = response["clock"].to<JsonObject>();
  clock["synchronized"] = clockValid;
  if (clockValid) {
    char formattedTime[24];
    std::strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%dT%H:%M:%S",
                  &localTime);
    clock["localTime"] = formattedTime;
  } else {
    clock["localTime"] = nullptr;
  }
  clock["timeZone"] = "Europe/Berlin";

  response["control"]["reason"] =
      getInhibitReason(state, clockValid, localTime);
}

esp_err_t statusHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }

  JsonDocument response;
  fillStatusResponse(response);
  sendJson(request, "200 OK", response);
  return ESP_OK;
}

bool readRequestedHeaterState(httpd_req_t* request, bool& enabled) {
  if (request->content_len == 0 ||
      request->content_len > MAX_REQUEST_BODY_LENGTH) {
    return false;
  }

  char body[MAX_REQUEST_BODY_LENGTH + 1];
  size_t received = 0;
  while (received < request->content_len) {
    const int result = httpd_req_recv(
        request, body + received, request->content_len - received);
    if (result <= 0) {
      return false;
    }
    received += static_cast<size_t>(result);
  }
  body[received] = '\0';

  JsonDocument requestBody;
  const DeserializationError error = deserializeJson(requestBody, body);
  if (error || !requestBody["state"].is<const char*>()) {
    return false;
  }

  const char* requestedState = requestBody["state"];
  if (std::strcmp(requestedState, "on") == 0) {
    enabled = true;
    return true;
  }
  if (std::strcmp(requestedState, "off") == 0) {
    enabled = false;
    return true;
  }
  return false;
}

esp_err_t relayHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }

  bool enabled = false;
  if (!readRequestedHeaterState(request, enabled)) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(
        request, "{\"error\":\"expected JSON body with state on or off\"}");
    return ESP_OK;
  }

  setHeaterEnabled(enabled);
  JsonDocument response;
  fillStatusResponse(response);
  sendJson(request, "200 OK", response);
  return ESP_OK;
}

bool createBasicAuthHeader() {
  const String credentials =
      String(ESP_API_USER) + ":" + String(ESP_API_PASSWORD);
  unsigned char encoded[2 * MAX_AUTH_HEADER_LENGTH];
  size_t encodedLength = 0;
  const int result = mbedtls_base64_encode(
      encoded, sizeof(encoded), &encodedLength,
      reinterpret_cast<const unsigned char*>(credentials.c_str()),
      credentials.length());
  if (result != 0 || encodedLength + 7 >= MAX_AUTH_HEADER_LENGTH) {
    Serial.println("HomeNodes API credentials are invalid or too long.");
    return false;
  }

  encoded[encodedLength] = '\0';
  basicAuthHeader = "Basic ";
  basicAuthHeader += reinterpret_cast<const char*>(encoded);
  return true;
}

bool startHttpsServer() {
  if (std::strlen(ESP_SERVER_CERTIFICATE_PEM) == 0 ||
      std::strlen(ESP_SERVER_PRIVATE_KEY_PEM) == 0) {
    Serial.println("HTTPS server certificate/key are missing.");
    return false;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.cacert_pem =
      reinterpret_cast<const uint8_t*>(ESP_SERVER_CERTIFICATE_PEM);
  config.cacert_len = std::strlen(ESP_SERVER_CERTIFICATE_PEM) + 1;
  config.prvtkey_pem =
      reinterpret_cast<const uint8_t*>(ESP_SERVER_PRIVATE_KEY_PEM);
  config.prvtkey_len = std::strlen(ESP_SERVER_PRIVATE_KEY_PEM) + 1;
  config.port_secure = 443;

  if (httpd_ssl_start(&httpsServer, &config) != ESP_OK) {
    Serial.println("Could not start ESP32 HTTPS API server.");
    httpsServer = nullptr;
    return false;
  }

  const httpd_uri_t statusUri = {
      "/api/v1/status", HTTP_GET, statusHandler, nullptr};
  const httpd_uri_t relayUri = {
      "/api/v1/relay", HTTP_POST, relayHandler, nullptr};

  if (httpd_register_uri_handler(httpsServer, &statusUri) != ESP_OK ||
      httpd_register_uri_handler(httpsServer, &relayUri) != ESP_OK) {
    Serial.println("Could not register ESP32 HTTPS API routes.");
    httpd_ssl_stop(httpsServer);
    httpsServer = nullptr;
    return false;
  }

  Serial.println("ESP32 HTTPS API listening on port 443.");
  return true;
}

void stopHttpsServer() {
  if (httpsServer != nullptr) {
    httpd_ssl_stop(httpsServer);
    httpsServer = nullptr;
  }
}

void startNetworkServices() {
  configTzTime(TIME_ZONE, NTP_SERVER);
  Serial.printf("Started time synchronization with %s.\n", NTP_SERVER);

  if (!mdnsStarted) {
    if (MDNS.begin(CLIENT_ID)) {
      mdnsStarted = true;
      MDNS.addService("https", "tcp", 443);
      Serial.printf("mDNS hostname: %s.local\n", CLIENT_ID);
    } else {
      Serial.println("Could not start mDNS discovery.");
    }
  }

  if (!createBasicAuthHeader()) {
    return;
  }
  startHttpsServer();
}

}  // namespace

void setup() {
  Serial.begin(115200);

  digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  controlState.relayIsOn = false;
  controlState.heaterEnabled = false;

  Wire.begin(DHT20_SDA_PIN, DHT20_SCL_PIN);
  Serial.printf("DHT20 I2C pins: SDA GPIO %u, SCL GPIO %u.\n",
                DHT20_SDA_PIN, DHT20_SCL_PIN);
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CLIENT_ID);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf(
      "Starting %s; heater disabled until API enable; polling Node-RED every "
      "%lu ms; NTP server %s.\n",
      CLIENT_ID, static_cast<unsigned long>(POLL_INTERVAL_MS), NTP_SERVER);
}

void loop() {
  const unsigned long now = millis();
  if (lastSensorPollAt == 0 ||
      now - lastSensorPollAt >= SENSOR_POLL_INTERVAL_MS) {
    lastSensorPollAt = now;
    pollDht20();
  }

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (wasWiFiConnected) {
      failSafeOff("Wi-Fi disconnected");
      stopHttpsServer();
      if (mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
      }
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
    startNetworkServices();
  }

  if (wifiConnected &&
      (!hasPolledSinceWiFiConnect || now - lastPollAt >= POLL_INTERVAL_MS)) {
    lastPollAt = now;
    hasPolledSinceWiFiConnect = true;
    pollNodeRed();
  }

  delay(20);
}
