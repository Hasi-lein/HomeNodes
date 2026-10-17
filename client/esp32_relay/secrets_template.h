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
#define NODERED_HTTP_USER "esp32-relay-01"
#define NODERED_HTTP_PASSWORD "replace-with-a-long-unique-password"

// Paste the complete PEM certificate, including the BEGIN/END lines.
// Do not paste URL-encoded/base64-only text. Its SAN must match the hostname
// in NODERED_BATTERY_VOLTAGE_URL. Keep TLS certificate verification enabled.
static const char NODERED_CA_CERTIFICATE_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
replace with the certificate's base64-encoded PEM body
-----END CERTIFICATE-----
)PEM";

// Poll interval, in milliseconds. Default: 60 seconds.
constexpr unsigned long POLL_INTERVAL_MS = 60000;
