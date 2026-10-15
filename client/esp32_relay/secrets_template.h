#pragma once

// Copy this file to secrets.h and replace every value before flashing.
// Keep secrets.h, certificates, and private keys out of version control.
#define WIFI_SSID "your-automation-wifi"
#define WIFI_PASSWORD "replace-with-wifi-password"

// Use only letters, digits, and hyphens; this is also the mDNS hostname.
#define CLIENT_ID "esp32-relay-01"

#define HTTP_BASIC_USER "esp32-relay-01"
#define HTTP_BASIC_PASSWORD "replace-with-a-long-unique-password"

// Replace the text inside each PEM block with this device's certificate and
// matching private key. Keep the raw-string delimiters on their own lines.
// The certificate SAN must match the hostname used by the controller.
// Trust a self-signed certificate explicitly; never disable TLS verification.
static const char TLS_CERTIFICATE_PEM[] = R"PEM(
replace with certificate contents
)PEM";

static const char TLS_PRIVATE_KEY_PEM[] = R"PEM(
replace with private key contents
)PEM";
