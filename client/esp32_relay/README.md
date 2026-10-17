# ESP32 relay client

This PlatformIO project runs the battery-voltage hysteresis and relay control
locally on an ESP32-WROOM-32. The ESP32 polls a HTTPS endpoint exposed by
Node-RED on Venus OS for the latest Victron battery voltage. It does not use
MQTT.

## Behavior

- Poll Node-RED every 60 seconds by default; configure `POLL_INTERVAL_MS` in
  the local `secrets.h`.
- Turn the relay **ON** at or above 53.8 V.
- Turn the relay **OFF** at or below 52.1 V.
- Retain the previous relay state between the two thresholds.
- Start with the relay OFF. If Wi-Fi, TLS, HTTP, or response validation fails,
  switch OFF (fail-safe).
- Reject voltage values outside the broad sanity range of 30–65 V.

These are the voltage thresholds selected for the initial test. Battery
voltage is an approximate indicator, not a direct SOC measurement. Validate
the thresholds for the battery and operating conditions before enabling the
infrared heater.

## Configure and flash

1. Install the VS Code PlatformIO IDE extension.
2. Open this `esp32_relay` directory as a PlatformIO project. The `esp32dev`
   environment targets a generic ESP32-WROOM-32 development board.
3. Copy `secrets_template.h` to `secrets.h`.
4. Set the Wi-Fi SSID/password, Node-RED HTTPS URL, Node-RED HTTP Basic Auth
   credentials, and polling interval.
5. Paste the **complete PEM** for the Node-RED server certificate or issuing
   CA into `NODERED_CA_CERTIFICATE_PEM`, including the
   `-----BEGIN CERTIFICATE-----` and `-----END CERTIFICATE-----` lines.
   Do not paste URL-encoded certificate text or a base64 body without the PEM
   markers. The certificate SAN must match the hostname in
   `NODERED_BATTERY_VOLTAGE_URL`. Do not disable TLS verification.
6. Connect the ESP32 by USB, select the `esp32dev` environment, then use
   PlatformIO **Build** and **Upload**. Use **Monitor** at 115200 baud.

`secrets.h` is ignored by Git. Never commit it or real credentials. The
PlatformIO project downloads ArduinoJson on its first build.

## Node-RED endpoint

The Venus OS Node-RED flow should expose:

```text
GET /homenodes/v1/battery-voltage
```

A successful JSON response is:

```json
{
  "voltage": 53.42,
  "updatedAt": 1792173477458
}
```

`updatedAt` is Unix time in milliseconds. Node-RED should return `503 Service
Unavailable` when it has no recent Victron voltage sample (the example flow
uses a 120-second freshness limit). Configure the HTTP In endpoint to use
HTTPS and HTTP Basic Authentication. The ESP32 trusts the configured
certificate/CA and checks the hostname; do not use insecure TLS options.

The ESP32 sends its Basic Auth credentials on every poll. Use dedicated
credentials for this endpoint and keep them private.

## Hardware

For the initial relay test, use only the ESP32 and the single-channel relay
module:

| ESP32-WROOM-32 | Relay module |
| --- | --- |
| 3V3 | VCC |
| GND | GND |
| GPIO 13 | IN1 |

This assumes the module accepts 3.3 V power/logic and is active HIGH. Verify
the exact module rating and behavior. Do not connect the infrared heater or
any mains load while testing.
