# ESP32 relay client

This sketch turns the existing ESP32-CAM relay prototype into a small HTTPS
API server. It keeps the relay on GPIO 13 (active HIGH, as in the previous
sketch), starts with the relay off, and does not initialize the camera or PIR
sensor.

## Configure and flash

1. Use an ESP32 Arduino core that includes `esp_https_server` and mDNS support.
2. Copy `secrets.example.h` to `secrets.h`.
3. Set the automation WLAN, unique client ID, and a unique HTTP Basic Auth
   username/password.
4. Create a TLS certificate whose Subject Alternative Name matches
   `<CLIENT_ID>.local`, plus its private key. Paste the PEM contents into
   `TLS_CERTIFICATE_PEM` and `TLS_PRIVATE_KEY_PEM`. Keep `secrets.h` and the
   private key out of version control.
5. Trust the device certificate (or its issuing CA) in the controller. Do not
   disable certificate validation in the controller.
6. Select the correct ESP32-CAM board and serial port in Arduino IDE, connect
   by USB, and upload the sketch.

The client advertises `_https._tcp` over mDNS and can be reached at
`https://<CLIENT_ID>.local`. For TLS validation, the hostname used by the
controller must match a name in the certificate. To connect by IP address,
that IP address must also be included in the certificate's Subject Alternative
Name. The ESP32 itself serves HTTPS on port 443.

## API

All endpoints require HTTP Basic Authentication over HTTPS.

### `GET /api/v1/capabilities`

Returns the client ID, firmware version, and the available relay command.

### `GET /api/v1/status`

Returns the current relay state.

### `POST /api/v1/relay`

Accepts a JSON body:

```json
{"state":"on"}
```

Use `{"state":"off"}` to turn the relay off. Successful responses use the
same shape as the status endpoint. Invalid or oversized request bodies are
rejected with `400 Bad Request`.

## Hardware note

GPIO 13 is retained from the old sketch. On an ESP32-CAM board it is also
associated with the microSD interface; do not use the SD card while using this
relay pin. Verify the relay module's input polarity and its electrical
isolation before connecting any load. The USB connection is suitable for
development and low-power operation, not necessarily for powering a relay
module and its load-side equipment.
