#pragma once

// Copy this file to secrets.h and replace every value before flashing.
// Keep secrets.h and all private credentials out of version control.
#define WIFI_SSID "your-automation-wifi"
#define WIFI_PASSWORD "replace-with-wifi-password"

// Use only letters, digits, and hyphens.
#define CLIENT_ID "esp32-relay-01"

// The ESP32 uses the Fritz!Box as its only NTP source (UDP port 123).
#define NTP_SERVER "192.168.178.1"

#define NODERED_BATTERY_VOLTAGE_URL \
  "https://venus.local:1881/homenodes/v1/battery-voltage"
#define NODERED_HTTP_USER "HomeNodes"
#define NODERED_HTTP_PASSWORD "replace-with-a-long-unique-password"

// Credentials for the ESP32 HTTPS API. Use a unique password for this client.
#define ESP_API_USER "HomeNodes"
#define ESP_API_PASSWORD "replace-with-a-different-long-unique-password"

// Paste the complete PEM certificate, including the BEGIN/END lines.
// Do not paste URL-encoded/base64-only text. Its SAN must match the hostname
// in NODERED_BATTERY_VOLTAGE_URL. Keep TLS certificate verification enabled.
static const char NODERED_CA_CERTIFICATE_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
replace with the certificate's base64-encoded PEM body
-----END CERTIFICATE-----
)PEM";

// ESP32 HTTPS server identity. The certificate SAN must include
// <CLIENT_ID>.local. Keep the private key secret and never commit secrets.h.
static const char ESP_SERVER_CERTIFICATE_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
replace with this ESP32's server certificate
-----END CERTIFICATE-----
)PEM";

static const char ESP_SERVER_PRIVATE_KEY_PEM[] = R"PEM(
-----BEGIN PRIVATE KEY-----
replace with this ESP32's private key
-----END PRIVATE KEY-----
)PEM";

// Poll interval, in milliseconds. Default: 60 seconds.
constexpr unsigned long POLL_INTERVAL_MS = 60000;
