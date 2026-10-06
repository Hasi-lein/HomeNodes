#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_https_server.h>
#include <mbedtls/base64.h>

#include <cstring>
#include <cstdio>

#include "secrets.h"

namespace {

constexpr uint8_t RELAY_PIN = 13;
constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;
constexpr char FIRMWARE_VERSION[] = "0.1.0";
constexpr size_t MAX_AUTH_HEADER_LENGTH = 160;
constexpr size_t MAX_REQUEST_BODY_LENGTH = 128;

httpd_handle_t httpsServer = nullptr;
bool relayIsOn = false;
String basicAuthHeader;

void sendJson(httpd_req_t* request, const char* status, const char* body) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
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
    difference |= static_cast<uint8_t>(
        header[i] ^ basicAuthHeader.charAt(i));
  }
  return difference == 0;
}

bool requireAuthorization(httpd_req_t* request) {
  if (isAuthorized(request)) {
    return true;
  }

  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"ESP32 relay\"");
  httpd_resp_set_type(request, "application/json");
  httpd_resp_sendstr(request, "{\"error\":\"unauthorized\"}");
  return false;
}

esp_err_t capabilitiesHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }

  char response[256];
  std::snprintf(response, sizeof(response),
                "{\"clientId\":\"%s\",\"firmwareVersion\":\"%s\","
                "\"capabilities\":[{\"id\":\"relay-1\",\"type\":\"relay\","
                "\"commands\":[\"on\",\"off\"]}]}",
                CLIENT_ID, FIRMWARE_VERSION);
  sendJson(request, "200 OK", response);
  return ESP_OK;
}

esp_err_t statusHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }

  char response[160];
  std::snprintf(response, sizeof(response),
                "{\"clientId\":\"%s\",\"firmwareVersion\":\"%s\","
                "\"relay\":{\"id\":\"relay-1\",\"state\":\"%s\"}}",
                CLIENT_ID, FIRMWARE_VERSION, relayIsOn ? "on" : "off");
  sendJson(request, "200 OK", response);
  return ESP_OK;
}

bool readRequestedRelayState(httpd_req_t* request, bool& requestedState) {
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

  const char* cursor = body;
  const auto skipWhitespace = [&cursor]() {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') {
      ++cursor;
    }
  };

  skipWhitespace();
  if (*cursor++ != '{') {
    return false;
  }
  skipWhitespace();
  if (std::strncmp(cursor, "\"state\"", 7) != 0) {
    return false;
  }
  cursor += 7;
  skipWhitespace();
  if (*cursor++ != ':') {
    return false;
  }
  skipWhitespace();
  if (*cursor++ != '"') {
    return false;
  }

  if (std::strncmp(cursor, "on\"", 3) == 0) {
    requestedState = true;
    cursor += 3;
  } else if (std::strncmp(cursor, "off\"", 4) == 0) {
    requestedState = false;
    cursor += 4;
  } else {
    return false;
  }

  skipWhitespace();
  if (*cursor++ != '}') {
    return false;
  }
  skipWhitespace();
  return *cursor == '\0';
}

esp_err_t relayHandler(httpd_req_t* request) {
  if (!requireAuthorization(request)) {
    return ESP_OK;
  }

  bool requestedState = false;
  if (!readRequestedRelayState(request, requestedState)) {
    sendJson(request, "400 Bad Request",
             "{\"error\":\"expected JSON body with state on or off\"}");
    return ESP_OK;
  }

  relayIsOn = requestedState;
  digitalWrite(RELAY_PIN, relayIsOn ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  statusHandler(request);
  return ESP_OK;
}

bool createBasicAuthHeader() {
  const String credentials = String(HTTP_BASIC_USER) + ":" + HTTP_BASIC_PASSWORD;
  unsigned char encoded[2 * MAX_AUTH_HEADER_LENGTH];
  size_t encodedLength = 0;
  const int result = mbedtls_base64_encode(
      encoded, sizeof(encoded), &encodedLength,
      reinterpret_cast<const unsigned char*>(credentials.c_str()),
      credentials.length());
  if (result != 0 || encodedLength + 7 >= MAX_AUTH_HEADER_LENGTH) {
    Serial.println("HTTP Basic Auth credentials are invalid or too long.");
    return false;
  }

  encoded[encodedLength] = '\0';
  basicAuthHeader = "Basic ";
  basicAuthHeader += reinterpret_cast<const char*>(encoded);
  return true;
}

bool startHttpsServer() {
  if (std::strlen(TLS_CERTIFICATE_PEM) == 0 ||
      std::strlen(TLS_PRIVATE_KEY_PEM) == 0) {
    Serial.println("HTTPS certificate/key are missing; server not started.");
    return false;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.servercert =
      reinterpret_cast<const uint8_t*>(TLS_CERTIFICATE_PEM);
  config.servercert_len = std::strlen(TLS_CERTIFICATE_PEM) + 1;
  config.prvtkey = reinterpret_cast<const uint8_t*>(TLS_PRIVATE_KEY_PEM);
  config.prvtkey_len = std::strlen(TLS_PRIVATE_KEY_PEM) + 1;
  config.port_secure = 443;

  if (httpd_ssl_start(&httpsServer, &config) != ESP_OK) {
    Serial.println("Could not start HTTPS server.");
    httpsServer = nullptr;
    return false;
  }

  const httpd_uri_t capabilitiesUri = {
      "/api/v1/capabilities", HTTP_GET, capabilitiesHandler, nullptr};
  const httpd_uri_t statusUri = {
      "/api/v1/status", HTTP_GET, statusHandler, nullptr};
  const httpd_uri_t relayUri = {
      "/api/v1/relay", HTTP_POST, relayHandler, nullptr};

  const bool routesRegistered =
      httpd_register_uri_handler(httpsServer, &capabilitiesUri) == ESP_OK &&
      httpd_register_uri_handler(httpsServer, &statusUri) == ESP_OK &&
      httpd_register_uri_handler(httpsServer, &relayUri) == ESP_OK;
  if (!routesRegistered) {
    Serial.println("Could not register HTTPS API routes.");
    httpd_ssl_stop(httpsServer);
    httpsServer = nullptr;
    return false;
  }

  Serial.println("HTTPS API listening on port 443.");
  return true;
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CLIENT_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to Wi-Fi %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.printf("\nConnected. IP address: %s\n",
                WiFi.localIP().toString().c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
  relayIsOn = false;

  if (!createBasicAuthHeader()) {
    return;
  }

  connectToWiFi();
  if (MDNS.begin(CLIENT_ID)) {
    MDNS.addService("https", "tcp", 443);
    Serial.printf("mDNS hostname: %s.local\n", CLIENT_ID);
  } else {
    Serial.println("Could not start mDNS discovery.");
  }

  startHttpsServer();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt >= 10000) {
      lastReconnectAttempt = millis();
      WiFi.reconnect();
    }
    delay(100);
    return;
  }
  delay(1000);
}
