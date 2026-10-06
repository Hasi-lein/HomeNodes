#pragma once

// Copy this file to secrets.h and replace every value before flashing.
#define WIFI_SSID "your-automation-wifi"
#define WIFI_PASSWORD "replace-with-wifi-password"

// Use only letters, digits, and hyphens; this is also the mDNS hostname.
#define CLIENT_ID "esp32-relay-01"

#define HTTP_BASIC_USER "esp32-relay-01"
#define HTTP_BASIC_PASSWORD "replace-with-a-long-unique-password"

// Paste the PEM certificate and matching private key for this device here.
// The controller must trust this certificate when connecting to the ESP32.
#define TLS_CERTIFICATE_PEM R"PEM()PEM"
#define TLS_PRIVATE_KEY_PEM R"PEM()PEM"
