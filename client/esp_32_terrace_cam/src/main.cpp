#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <esp_camera.h>
#include <esp_https_server.h>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <memory>

#include "secrets.h"

namespace {

constexpr uint8_t PIR_PIN = 15;
constexpr uint8_t LIGHT_RELAY_PIN = 4;
constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;
constexpr uint8_t DHT20_SDA_PIN = 13;
constexpr uint8_t DHT20_SCL_PIN = 14;
constexpr uint8_t DHT20_I2C_ADDRESS = 0x38;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long SENSOR_POLL_INTERVAL_MS = 10000;
constexpr unsigned long MAX_SENSOR_DATA_AGE_MS = 30000;
constexpr unsigned long PIR_SAMPLE_INTERVAL_MS = 50;
constexpr unsigned long IMAGE_GAP_MS = 5000;
constexpr unsigned long IMAGE_UPLOAD_TIMEOUT_MS = 15000;
constexpr uint8_t IMAGES_PER_BATCH = 3;
constexpr uint8_t MIN_LIGHT_DURATION_MINUTES = 1;
constexpr uint8_t MAX_LIGHT_DURATION_MINUTES = 60;
constexpr size_t MAX_REQUEST_BODY_LENGTH = 256;
constexpr size_t MAX_AUTH_HEADER_LENGTH = 256;
constexpr time_t MIN_VALID_EPOCH = 1704067200;
constexpr char TIME_ZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char SETTINGS_NAMESPACE[] = "terracecam";

enum class LightMode : uint8_t { Off = 0, Auto = 1, Timed = 2 };
enum class CameraMode : uint8_t { Off = 0, Auto = 1 };

struct RuntimeState {
  LightMode lightMode;
  CameraMode cameraMode;
  bool relayIsOn;
  bool pirMotion;
  uint32_t lightDeadline;
  bool hasDht20Data;
  float temperatureC;
  float relativeHumidityPercent;
  uint32_t dht20DataReceivedAt;
  bool captureBusy;
  bool captureAutomatic;
  bool captureFailed;
  uint8_t imagesRemaining;
  uint8_t imagesUploadedInBatch;
  uint32_t totalImagesUploaded;
  uint32_t nextCaptureAt;
  uint32_t captureBatch;
};

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
RuntimeState runtimeState = {
    LightMode::Auto, CameraMode::Off, false, false, 0, false, 0.0F, 0.0F, 0,
    false, false, false, 0, 0, 0, 0, 0};
Preferences settingsStore;
SemaphoreHandle_t settingsMutex = nullptr;
bool settingsStorageReady = false;
httpd_handle_t httpsServer = nullptr;
String basicAuthHeader;
bool cameraReady = false;
bool captureTaskReady = false;
char lastCaptureError[96] = {};
bool dht20Initialized = false;
bool mdnsStarted = false;
bool wasWiFiConnected = false;
bool previousPirMotion = false;
uint32_t lastWiFiRetryAt = 0;
uint32_t lastSensorPollAt = 0;
uint32_t lastPirSampleAt = 0;

RuntimeState getRuntimeState() {
  portENTER_CRITICAL(&stateMux);
  const RuntimeState snapshot = runtimeState;
  portEXIT_CRITICAL(&stateMux);
  return snapshot;
}

bool persistSetting(const char* key, uint8_t value) {
  if (!settingsStorageReady || settingsMutex == nullptr ||
      xSemaphoreTake(settingsMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }
  const size_t written = settingsStore.putUChar(key, value);
  xSemaphoreGive(settingsMutex);
  return written == sizeof(value);
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
    while (Wire.available()) {
      Wire.read();
    }
    Serial.println("DHT20 returned an incomplete measurement.");
    return false;
  }

  uint8_t data[7];
  for (uint8_t& value : data) {
    value = static_cast<uint8_t>(Wire.read());
  }
  if ((data[0] & 0x80) != 0 || (data[0] & 0x08) == 0 ||
      dht20Crc8(data, 6) != data[6]) {
    Serial.println("DHT20 returned a busy, uncalibrated, or invalid sample.");
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
  return std::isfinite(temperature) && std::isfinite(humidity) &&
         temperature >= -40.0F && temperature <= 80.0F && humidity >= 0.0F &&
         humidity <= 100.0F;
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
  if (!readDht20(temperature, humidity)) {
    dht20Initialized = false;
    return;
  }

  portENTER_CRITICAL(&stateMux);
  runtimeState.hasDht20Data = true;
  runtimeState.temperatureC = temperature;
  runtimeState.relativeHumidityPercent = humidity;
  runtimeState.dht20DataReceivedAt = millis();
  portEXIT_CRITICAL(&stateMux);
}

void setRelayOutput(bool enabled) {
  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (runtimeState.relayIsOn != enabled) {
    runtimeState.relayIsOn = enabled;
    digitalWrite(LIGHT_RELAY_PIN, enabled ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    changed = true;
  }
  portEXIT_CRITICAL(&stateMux);
  if (changed) {
    Serial.printf("Terrace light relay %s\n", enabled ? "ON" : "OFF");
  }
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void updateLightFromMode(uint32_t now) {
  RuntimeState state = getRuntimeState();
  bool shouldBeOn = false;

  if (state.lightMode == LightMode::Timed) {
    if (deadlineReached(now, state.lightDeadline)) {
      portENTER_CRITICAL(&stateMux);
      if (runtimeState.lightMode == LightMode::Timed &&
          runtimeState.lightDeadline == state.lightDeadline) {
        runtimeState.lightMode = LightMode::Off;
        runtimeState.lightDeadline = 0;
      }
      state = runtimeState;
      portEXIT_CRITICAL(&stateMux);
      if (state.lightMode == LightMode::Off) {
        Serial.println("Timed terrace light expired.");
      }
    }
  }

  shouldBeOn = state.lightMode == LightMode::Timed ||
               (state.lightMode == LightMode::Auto && state.pirMotion);
  setRelayOutput(shouldBeOn);
}

String lightModeName(LightMode mode) {
  switch (mode) {
    case LightMode::Off:
      return "off";
    case LightMode::Auto:
      return "auto";
    case LightMode::Timed:
      return "timed";
  }
  return "off";
}

String cameraModeName(CameraMode mode) {
  return mode == CameraMode::Auto ? "auto" : "off";
}

bool startCaptureBatch(bool automatic) {
  if (!cameraReady || !captureTaskReady ||
      WiFi.status() != WL_CONNECTED) {
    return false;
  }

  portENTER_CRITICAL(&stateMux);
  if (runtimeState.captureBusy) {
    portEXIT_CRITICAL(&stateMux);
    return false;
  }
  runtimeState.captureBusy = true;
  runtimeState.captureAutomatic = automatic;
  runtimeState.captureFailed = false;
  runtimeState.imagesRemaining = IMAGES_PER_BATCH;
  runtimeState.imagesUploadedInBatch = 0;
  lastCaptureError[0] = '\0';
  runtimeState.nextCaptureAt = millis();
  runtimeState.captureBatch += 1;
  portEXIT_CRITICAL(&stateMux);
  Serial.printf("Started %s capture batch.\n",
                automatic ? "automatic" : "manual");
  return true;
}

bool base64Encode(const String& input, String& output) {
  const size_t capacity = 4 * ((input.length() + 2) / 3) + 1;
  std::unique_ptr<unsigned char[]> buffer(new unsigned char[capacity]);
  size_t encodedLength = 0;
  const int result = mbedtls_base64_encode(
      buffer.get(), capacity, &encodedLength,
      reinterpret_cast<const unsigned char*>(input.c_str()), input.length());
  if (result != 0) {
    return false;
  }
  buffer[encodedLength] = '\0';
  output = reinterpret_cast<const char*>(buffer.get());
  return true;
}

bool writeAll(WiFiClientSecure& client, const uint8_t* data, size_t length) {
  size_t written = 0;
  while (written < length) {
    const size_t count = client.write(data + written, length - written);
    if (count == 0) {
      return false;
    }
    written += count;
    delay(0);
  }
  return true;
}

bool uploadJpeg(const camera_fb_t* frame, uint32_t batch, uint8_t imageIndex,
                String& filename, String& error) {
  if (frame == nullptr || frame->buf == nullptr || frame->len == 0) {
    error = "Camera returned an empty frame";
    return false;
  }
  if (std::strstr(IMAGE_UPLOAD_CA_CERTIFICATE_PEM, "replace with") != nullptr) {
    error = "Image upload CA certificate is not configured";
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(IMAGE_UPLOAD_CA_CERTIFICATE_PEM);
  client.setTimeout(IMAGE_UPLOAD_TIMEOUT_MS);
  client.setHandshakeTimeout(15);
  if (!client.connect(IMAGE_UPLOAD_HOST, 443)) {
    error = "Could not establish verified HTTPS connection to image endpoint";
    return false;
  }

  String authorization;
  const String credentials =
      String(IMAGE_UPLOAD_HTTP_USER) + ":" + IMAGE_UPLOAD_HTTP_PASSWORD;
  if (!base64Encode(credentials, authorization)) {
    client.stop();
    error = "Could not encode image endpoint credentials";
    return false;
  }

  time_t now = time(nullptr);
  const uint64_t timestamp =
      now >= MIN_VALID_EPOCH ? static_cast<uint64_t>(now) : millis();
  filename = "terrace_cam_" + String(batch) + "_" + String(timestamp) + "_" +
             String(imageIndex) + ".jpg";
  const String boundary = "----HomeNodesTerraceCamBoundary7MA4YWxk";
  const String bodyStart =
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"image\"; filename=\"" +
      filename +
      "\"\r\n"
      "Content-Type: image/jpeg\r\n\r\n";
  const String bodyEnd = "\r\n--" + boundary + "--\r\n";
  const size_t contentLength = bodyStart.length() + frame->len + bodyEnd.length();

  client.printf("POST %s HTTP/1.1\r\n", IMAGE_UPLOAD_PATH);
  client.printf("Host: %s\r\n", IMAGE_UPLOAD_HOST);
  client.printf("Authorization: Basic %s\r\n", authorization.c_str());
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n",
                boundary.c_str());
  client.printf("Content-Length: %u\r\n",
                static_cast<unsigned int>(contentLength));
  client.print("Connection: close\r\n\r\n");

  const bool sent =
      writeAll(client, reinterpret_cast<const uint8_t*>(bodyStart.c_str()),
               bodyStart.length()) &&
      writeAll(client, frame->buf, frame->len) &&
      writeAll(client, reinterpret_cast<const uint8_t*>(bodyEnd.c_str()),
               bodyEnd.length());
  if (!sent) {
    client.stop();
    error = "Image upload connection closed while sending";
    return false;
  }

  const uint32_t responseStartedAt = millis();
  while (!client.available() && client.connected() &&
         millis() - responseStartedAt < IMAGE_UPLOAD_TIMEOUT_MS) {
    delay(10);
  }
  if (!client.available()) {
    client.stop();
    error = "Image endpoint did not return an HTTP response";
    return false;
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  client.stop();
  const int firstSpace = statusLine.indexOf(' ');
  const int statusCode =
      firstSpace >= 0 ? statusLine.substring(firstSpace + 1).toInt() : 0;
  if (statusCode < 200 || statusCode >= 300) {
    error = "Image endpoint returned " + statusLine;
    return false;
  }

  return true;
}

void finishCapture(uint32_t batch, bool failed, const char* error) {
  portENTER_CRITICAL(&stateMux);
  if (runtimeState.captureBatch == batch) {
    runtimeState.captureBusy = false;
    runtimeState.captureFailed = failed;
    runtimeState.imagesRemaining = 0;
    if (error != nullptr) {
      std::snprintf(lastCaptureError, sizeof(lastCaptureError), "%s", error);
    }
  }
  portEXIT_CRITICAL(&stateMux);
}

void serviceCapture() {
  RuntimeState state = getRuntimeState();
  const uint32_t now = millis();
  if (!state.captureBusy || !deadlineReached(now, state.nextCaptureAt)) {
    return;
  }
  if (state.captureAutomatic && state.cameraMode != CameraMode::Auto) {
    finishCapture(state.captureBatch, false, nullptr);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Capture cancelled because Wi-Fi is disconnected.");
    finishCapture(state.captureBatch, true, "Wi-Fi disconnected");
    return;
  }
  if (time(nullptr) < MIN_VALID_EPOCH) {
    portENTER_CRITICAL(&stateMux);
    if (runtimeState.captureBatch == state.captureBatch &&
        runtimeState.captureBusy) {
      runtimeState.nextCaptureAt = millis() + 1000;
    }
    portEXIT_CRITICAL(&stateMux);
    return;
  }

  camera_fb_t* frame = esp_camera_fb_get();
  if (frame == nullptr) {
    Serial.println("Camera failed to capture an image.");
    finishCapture(state.captureBatch, true, "Camera failed to capture an image");
    return;
  }

  const uint8_t imageIndex =
      static_cast<uint8_t>(state.imagesUploadedInBatch + 1);
  String filename;
  String error;
  const bool uploaded =
      uploadJpeg(frame, state.captureBatch, imageIndex, filename, error);
  esp_camera_fb_return(frame);
  if (!uploaded) {
    Serial.printf("Image upload failed: %s\n", error.c_str());
    finishCapture(state.captureBatch, true, error.c_str());
    return;
  }

  Serial.printf("Uploaded %s\n", filename.c_str());
  portENTER_CRITICAL(&stateMux);
  runtimeState.totalImagesUploaded += 1;
  if (runtimeState.captureBatch == state.captureBatch &&
      runtimeState.captureBusy) {
    runtimeState.imagesUploadedInBatch += 1;
    if (runtimeState.imagesRemaining > 0) {
      runtimeState.imagesRemaining -= 1;
    }
    if (runtimeState.imagesRemaining == 0) {
      runtimeState.captureBusy = false;
    } else {
      runtimeState.nextCaptureAt = millis() + IMAGE_GAP_MS;
    }
  }
  portEXIT_CRITICAL(&stateMux);
}

void captureTask(void*) {
  for (;;) {
    serviceCapture();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void pollPir(uint32_t now) {
  if (now - lastPirSampleAt < PIR_SAMPLE_INTERVAL_MS) {
    return;
  }
  lastPirSampleAt = now;
  const bool motion = digitalRead(PIR_PIN) == HIGH;
  portENTER_CRITICAL(&stateMux);
  runtimeState.pirMotion = motion;
  const CameraMode cameraMode = runtimeState.cameraMode;
  portEXIT_CRITICAL(&stateMux);

  if (motion && !previousPirMotion && cameraMode == CameraMode::Auto &&
      !startCaptureBatch(true)) {
    Serial.println("Motion capture was skipped because the camera is busy or unavailable.");
  }
  previousPirMotion = motion;
  updateLightFromMode(now);
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

void sendError(httpd_req_t* request, const char* status, const char* message) {
  JsonDocument response;
  response["error"] = message;
  sendJson(request, status, response);
}

bool isAuthorized(httpd_req_t* request) {
  const size_t headerLength =
      httpd_req_get_hdr_value_len(request, "Authorization");
  if (headerLength == 0 || headerLength >= MAX_AUTH_HEADER_LENGTH ||
      headerLength != basicAuthHeader.length()) {
    return false;
  }

  char header[MAX_AUTH_HEADER_LENGTH];
  if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                  sizeof(header)) != ESP_OK) {
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
                     "Basic realm=\"HomeNodes terrace camera\"");
  httpd_resp_set_type(request, "application/json");
  httpd_resp_sendstr(request, "{\"error\":\"unauthorized\"}");
  return false;
}

bool readJsonBody(httpd_req_t* request, JsonDocument& document) {
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
  return deserializeJson(document, body, received) == DeserializationError::Ok &&
         document.is<JsonObject>();
}

void fillStatusResponse(JsonDocument& response) {
  const RuntimeState state = getRuntimeState();
  const uint32_t now = millis();
  const bool sensorFresh =
      state.hasDht20Data &&
      now - state.dht20DataReceivedAt <= MAX_SENSOR_DATA_AGE_MS;

  JsonObject client = response["client"].to<JsonObject>();
  client["clientId"] = CLIENT_ID;
  client["status"] =
      WiFi.status() == WL_CONNECTED && cameraReady && captureTaskReady &&
              settingsStorageReady && sensorFresh
          ? "active"
          : "error";
  client["statusReason"] =
      WiFi.status() != WL_CONNECTED
          ? "wifi_disconnected"
          : !cameraReady ? "camera_unavailable"
          : !captureTaskReady ? "capture_service_unavailable"
          : !settingsStorageReady ? "settings_storage_unavailable"
          : !sensorFresh ? "sensor_data_unavailable"
                         : "active";
  JsonArray methods = client["methods"].to<JsonArray>();
  JsonObject statusMethod = methods.add<JsonObject>();
  statusMethod["method"] = "GET";
  statusMethod["path"] = "/api/v1/status";
  JsonObject lightMethod = methods.add<JsonObject>();
  lightMethod["method"] = "POST";
  lightMethod["path"] = "/api/v1/light";
  JsonObject cameraMethod = methods.add<JsonObject>();
  cameraMethod["method"] = "POST";
  cameraMethod["path"] = "/api/v1/camera";
  JsonObject captureMethod = methods.add<JsonObject>();
  captureMethod["method"] = "POST";
  captureMethod["path"] = "/api/v1/camera/capture";

  JsonArray sensors = response["sensors"].to<JsonArray>();
  JsonObject climateSensor = sensors.add<JsonObject>();
  climateSensor["id"] = "terrace-climate";
  climateSensor["name"] = "Terrasse Temperatur und Luftfeuchtigkeit";
  climateSensor["type"] = "DHT20";
  climateSensor["status"] = sensorFresh ? "on" : "off";
  if (sensorFresh) {
    climateSensor["temperatureC"] = state.temperatureC;
    climateSensor["relativeHumidityPercent"] = state.relativeHumidityPercent;
  } else {
    climateSensor["temperatureC"] = nullptr;
    climateSensor["relativeHumidityPercent"] = nullptr;
  }

  JsonArray actuators = response["actuators"].to<JsonArray>();
  JsonObject lightRelay = actuators.add<JsonObject>();
  lightRelay["id"] = "terrace-light-relay";
  lightRelay["name"] = "Terrassenlicht";
  lightRelay["type"] = "relay";
  lightRelay["status"] = state.relayIsOn ? "on" : "off";

  JsonObject light = response["light"].to<JsonObject>();
  light["mode"] = lightModeName(state.lightMode);
  light["motionDetected"] = state.pirMotion;
  if (state.lightMode == LightMode::Timed &&
      !deadlineReached(now, state.lightDeadline)) {
    const uint32_t remainingMs = state.lightDeadline - now;
    light["remainingMinutes"] =
        (remainingMs + 60000 - 1) / 60000;
  } else {
    light["remainingMinutes"] = 0;
  }

  JsonObject camera = response["camera"].to<JsonObject>();
  camera["mode"] = cameraModeName(state.cameraMode);
  camera["captureState"] =
      state.captureBusy ? "capturing"
                        : state.captureFailed ? "error" : "idle";
  camera["imagesPerBatch"] = IMAGES_PER_BATCH;
  camera["imagesUploadedInBatch"] = state.imagesUploadedInBatch;
  camera["totalImagesUploaded"] = state.totalImagesUploaded;
  camera["cameraReady"] = cameraReady;
  camera["captureServiceReady"] = captureTaskReady;
  char captureError[sizeof(lastCaptureError)];
  portENTER_CRITICAL(&stateMux);
  std::memcpy(captureError, lastCaptureError, sizeof(captureError));
  portEXIT_CRITICAL(&stateMux);
  camera["lastError"] = captureError;
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

esp_err_t lightHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }
  JsonDocument document;
  if (!readJsonBody(request, document)) {
    sendError(request, "400 Bad Request", "Expected a JSON object");
    return ESP_OK;
  }
  JsonObjectConst body = document.as<JsonObjectConst>();
  const char* mode = body["mode"] | "";
  if (std::strcmp(mode, "off") == 0 && body.size() == 1) {
    if (!persistSetting("lightMode", static_cast<uint8_t>(LightMode::Off))) {
      sendError(request, "503 Service Unavailable",
                "Could not persist light mode");
      return ESP_OK;
    }
    portENTER_CRITICAL(&stateMux);
    runtimeState.lightMode = LightMode::Off;
    runtimeState.lightDeadline = 0;
    portEXIT_CRITICAL(&stateMux);
  } else if (std::strcmp(mode, "auto") == 0 && body.size() == 1) {
    if (!persistSetting("lightMode", static_cast<uint8_t>(LightMode::Auto))) {
      sendError(request, "503 Service Unavailable",
                "Could not persist light mode");
      return ESP_OK;
    }
    portENTER_CRITICAL(&stateMux);
    runtimeState.lightMode = LightMode::Auto;
    runtimeState.lightDeadline = 0;
    portEXIT_CRITICAL(&stateMux);
  } else if (std::strcmp(mode, "on") == 0 && body.size() == 2 &&
             body["durationMinutes"].is<uint8_t>()) {
    const uint8_t duration = body["durationMinutes"].as<uint8_t>();
    if (duration < MIN_LIGHT_DURATION_MINUTES ||
        duration > MAX_LIGHT_DURATION_MINUTES) {
      sendError(request, "400 Bad Request",
                "durationMinutes must be from 1 to 60");
      return ESP_OK;
    }
    if (!persistSetting("lightMode", static_cast<uint8_t>(LightMode::Off))) {
      sendError(request, "503 Service Unavailable",
                "Could not persist light mode");
      return ESP_OK;
    }
    portENTER_CRITICAL(&stateMux);
    runtimeState.lightMode = LightMode::Timed;
    runtimeState.lightDeadline = millis() +
                                 static_cast<uint32_t>(duration) * 60UL * 1000UL;
    portEXIT_CRITICAL(&stateMux);
  } else {
    sendError(request, "400 Bad Request",
              "Use mode off, auto, or on with durationMinutes (1-60)");
    return ESP_OK;
  }

  updateLightFromMode(millis());
  JsonDocument response;
  fillStatusResponse(response);
  sendJson(request, "200 OK", response);
  return ESP_OK;
}

esp_err_t cameraModeHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }
  JsonDocument document;
  if (!readJsonBody(request, document)) {
    sendError(request, "400 Bad Request", "Expected a JSON object");
    return ESP_OK;
  }
  JsonObjectConst body = document.as<JsonObjectConst>();
  const char* mode = body["mode"] | "";
  if (body.size() != 1 ||
      (std::strcmp(mode, "off") != 0 && std::strcmp(mode, "auto") != 0)) {
    sendError(request, "400 Bad Request", "Use camera mode off or auto");
    return ESP_OK;
  }

  const CameraMode cameraMode =
      std::strcmp(mode, "auto") == 0 ? CameraMode::Auto : CameraMode::Off;
  if (!persistSetting("cameraMode", static_cast<uint8_t>(cameraMode))) {
    sendError(request, "503 Service Unavailable",
              "Could not persist camera mode");
    return ESP_OK;
  }
  portENTER_CRITICAL(&stateMux);
  runtimeState.cameraMode = cameraMode;
  if (cameraMode == CameraMode::Off && runtimeState.captureAutomatic) {
    runtimeState.captureBusy = false;
    runtimeState.imagesRemaining = 0;
  }
  portEXIT_CRITICAL(&stateMux);

  JsonDocument response;
  fillStatusResponse(response);
  sendJson(request, "200 OK", response);
  return ESP_OK;
}

esp_err_t captureHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }
  if (request->content_len != 0) {
    sendError(request, "400 Bad Request", "Capture request must have an empty body");
    return ESP_OK;
  }
  if (!cameraReady || !captureTaskReady) {
    sendError(request, "503 Service Unavailable", "Camera is unavailable");
    return ESP_OK;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(request, "503 Service Unavailable", "Wi-Fi is disconnected");
    return ESP_OK;
  }
  if (!startCaptureBatch(false)) {
    sendError(request, "409 Conflict", "A capture batch is already in progress");
    return ESP_OK;
  }

  JsonDocument response;
  response["status"] = "accepted";
  response["imagesPerBatch"] = IMAGES_PER_BATCH;
  response["captureBatch"] = getRuntimeState().captureBatch;
  sendJson(request, "202 Accepted", response);
  return ESP_OK;
}

bool createBasicAuthHeader() {
  const String credentials = String(ESP_API_USER) + ":" + ESP_API_PASSWORD;
  String encoded;
  if (!base64Encode(credentials, encoded) ||
      encoded.length() + 7 >= MAX_AUTH_HEADER_LENGTH) {
    Serial.println("ESP32 API credentials are invalid or too long.");
    return false;
  }
  basicAuthHeader = "Basic " + encoded;
  return true;
}

bool startHttpsServer() {
  if (std::strstr(ESP_SERVER_CERTIFICATE_PEM, "replace with") != nullptr ||
      std::strstr(ESP_SERVER_PRIVATE_KEY_PEM, "replace with") != nullptr) {
    Serial.println("ESP32 HTTPS server certificate/key are not configured.");
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
  config.httpd.max_uri_handlers = 8;
  if (httpd_ssl_start(&httpsServer, &config) != ESP_OK) {
    Serial.println("Could not start ESP32 HTTPS API server.");
    httpsServer = nullptr;
    return false;
  }

  const httpd_uri_t statusUri = {
      "/api/v1/status", HTTP_GET, statusHandler, nullptr};
  const httpd_uri_t lightUri = {
      "/api/v1/light", HTTP_POST, lightHandler, nullptr};
  const httpd_uri_t cameraUri = {
      "/api/v1/camera", HTTP_POST, cameraModeHandler, nullptr};
  const httpd_uri_t captureUri = {
      "/api/v1/camera/capture", HTTP_POST, captureHandler, nullptr};
  if (httpd_register_uri_handler(httpsServer, &statusUri) != ESP_OK ||
      httpd_register_uri_handler(httpsServer, &lightUri) != ESP_OK ||
      httpd_register_uri_handler(httpsServer, &cameraUri) != ESP_OK ||
      httpd_register_uri_handler(httpsServer, &captureUri) != ESP_OK) {
    Serial.println("Could not register ESP32 HTTPS API routes.");
    httpd_ssl_stop(httpsServer);
    httpsServer = nullptr;
    return false;
  }
  Serial.println("ESP32 terrace camera HTTPS API listening on port 443.");
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
  if (!mdnsStarted) {
    if (MDNS.begin(CLIENT_ID)) {
      mdnsStarted = true;
      MDNS.addService("https", "tcp", 443);
      Serial.printf("mDNS hostname: %s.local\n", CLIENT_ID);
    } else {
      Serial.println("Could not start mDNS discovery.");
    }
  }
  if (createBasicAuthHeader()) {
    startHttpsServer();
  }
}

void setupCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 15;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t result = esp_camera_init(&config);
  cameraReady = result == ESP_OK;
  if (!cameraReady) {
    Serial.printf("Camera initialization failed: 0x%x\n", result);
  } else {
    Serial.println("AI-Thinker camera initialized.");
  }
}

void loadSettings() {
  settingsStorageReady = settingsStore.begin(SETTINGS_NAMESPACE, false);
  if (!settingsStorageReady) {
    Serial.println(
        "Could not open persistent settings; API mode changes are unavailable.");
    return;
  }
  const uint8_t storedLightMode =
      settingsStore.getUChar("lightMode", static_cast<uint8_t>(LightMode::Auto));
  const uint8_t storedCameraMode =
      settingsStore.getUChar("cameraMode", static_cast<uint8_t>(CameraMode::Off));

  portENTER_CRITICAL(&stateMux);
  runtimeState.lightMode =
      storedLightMode == static_cast<uint8_t>(LightMode::Off)
          ? LightMode::Off
          : LightMode::Auto;
  runtimeState.cameraMode =
      storedCameraMode == static_cast<uint8_t>(CameraMode::Auto)
          ? CameraMode::Auto
          : CameraMode::Off;
  portEXIT_CRITICAL(&stateMux);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  digitalWrite(LIGHT_RELAY_PIN, RELAY_OFF_LEVEL);
  pinMode(LIGHT_RELAY_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  Wire.begin(DHT20_SDA_PIN, DHT20_SCL_PIN);
  Serial.printf("DHT20 I2C pins: SDA GPIO %u, SCL GPIO %u.\n", DHT20_SDA_PIN,
                DHT20_SCL_PIN);
  loadSettings();
  settingsMutex = xSemaphoreCreateMutex();
  if (settingsMutex == nullptr) {
    settingsStorageReady = false;
    Serial.println("Could not create persistent-settings lock.");
  }
  setupCamera();
  if (cameraReady) {
    captureTaskReady =
        xTaskCreatePinnedToCore(captureTask, "camera-upload", 8192, nullptr, 1,
                                nullptr, 0) == pdPASS;
    if (!captureTaskReady) {
      Serial.println("Could not start camera upload task.");
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CLIENT_ID);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Starting %s; light mode %s; camera mode %s.\n", CLIENT_ID,
                lightModeName(runtimeState.lightMode).c_str(),
                cameraModeName(runtimeState.cameraMode).c_str());
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSensorPollAt >= SENSOR_POLL_INTERVAL_MS ||
      lastSensorPollAt == 0) {
    lastSensorPollAt = now;
    pollDht20();
  }
  pollPir(now);

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (wasWiFiConnected) {
      stopHttpsServer();
      if (mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
      }
    }
    wasWiFiConnected = false;
    if (now - lastWiFiRetryAt >= WIFI_RETRY_INTERVAL_MS) {
      lastWiFiRetryAt = now;
      WiFi.reconnect();
    }
  } else if (!wasWiFiConnected) {
    wasWiFiConnected = true;
    startNetworkServices();
  }

  delay(10);
}
