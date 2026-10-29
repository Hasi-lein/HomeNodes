#pragma once

// Copy this file to secrets.h, fill in local settings, and keep it out of Git.
#define WIFI_SSID "your-automation-wifi"
#define WIFI_PASSWORD "replace-with-wifi-password"

// Use only letters, digits, and hyphens.
#define CLIENT_ID "homenodes-terrace-cam-01"
#define NTP_SERVER "replace-with-ntp-server-hostname-or-ip"

// Credentials for this ESP32's authenticated HTTPS API.
#define ESP_API_USER "replace-with-username"
#define ESP_API_PASSWORD "replace-with-a-long-unique-password"

// The endpoint must accept multipart/form-data with a JPEG field named image.
#define IMAGE_UPLOAD_HOST "replace-with-upload-server-hostname"
#define IMAGE_UPLOAD_PATH "replace-with-upload-server-path"
#define IMAGE_UPLOAD_HTTP_USER "replace-with-upload-username"
#define IMAGE_UPLOAD_HTTP_PASSWORD "replace-with-upload-password"

// Trusted root CA for IMAGE_UPLOAD_HOST, not the server's CRT/leaf certificate.
// Check the endpoint's current chain before choosing a root CA.
static const char IMAGE_UPLOAD_CA_CERTIFICATE_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
replace with the trusted root CA certificate for normal.bio
-----END CERTIFICATE-----
)PEM";

// The certificate SAN must include <CLIENT_ID>.local.
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
