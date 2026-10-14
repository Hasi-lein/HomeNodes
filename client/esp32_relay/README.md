# ESP32 relay client

This PlatformIO project turns the existing ESP32-CAM relay prototype into a
small HTTPS API server. It keeps the relay on GPIO 13 (active HIGH, as in the
previous sketch), starts with the relay off, and does not initialize the
camera or PIR sensor.

## Configure and flash

1. Install the PlatformIO IDE extension in VS Code.
2. Open this `esp32_relay` directory as the PlatformIO project. The
   `esp32cam` environment targets the AI-Thinker ESP32-CAM using the Arduino
   framework.
3. Copy `secrets.example.h` to `secrets.h`.
4. Set the automation WLAN, unique client ID, and a unique HTTP Basic Auth
   username/password.
5. Create a TLS certificate whose Subject Alternative Name matches
   `<CLIENT_ID>.local`, plus its private key. Paste the PEM contents into
   `TLS_CERTIFICATE_PEM` and `TLS_PRIVATE_KEY_PEM`. Keep `secrets.h` and the
   private key out of version control.
6. Trust the device certificate (or its issuing CA) in the controller. Do not
   disable certificate validation in the controller.
7. Connect the board by USB (or a suitable USB-to-serial adapter), select the
   `esp32cam` environment, and use PlatformIO's Build and Upload tasks. Use
   Monitor to view the serial output at 115200 baud.

The project source is in `src/main.cpp`. Add other ESP32 firmware as separate
client directories, each with its own `platformio.ini`.

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
