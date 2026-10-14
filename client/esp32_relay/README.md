# ESP32 relay client

This PlatformIO project turns the existing relay prototype into a small HTTPS
API server. It keeps the relay on GPIO 13 (active HIGH, as in the previous
sketch) and starts with the relay off.

## Configure and flash

1. Install the PlatformIO IDE extension in VS Code.
2. Open this `esp32_relay` directory as the PlatformIO project. The
   `esp32dev` environment targets a generic ESP32-WROOM-32 development board
   using the Arduino framework.
3. Copy `secrets.example.h` to `secrets.h`.
4. Set the automation WLAN, unique client ID, and a unique HTTP Basic Auth
   username/password.
5. Create a TLS certificate whose Subject Alternative Name matches
   `<CLIENT_ID>.local`, plus its private key. Paste the PEM contents into
   `TLS_CERTIFICATE_PEM` and `TLS_PRIVATE_KEY_PEM`. Keep `secrets.h` and the
   private key out of version control.
6. Trust the device certificate (or its issuing CA) in the controller. Do not
   disable certificate validation in the controller.
7. Connect the board by USB, select the `esp32dev` environment, and use
   PlatformIO's Build and Upload tasks. Use Monitor to view the serial output
   at 115200 baud.

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

GPIO 13 is retained from the old sketch and is available on many ESP32-WROOM-32
development boards. Verify the exact board pinout, relay module input polarity
and electrical isolation before connecting a load. The USB connection is
suitable for development and low-power operation, not necessarily for powering
a relay module and its load-side equipment.

This first test uses only the ESP32 board and single-channel relay module
(`VCC`, `IN1`, `GND`); no sensor is required. Confirm the relay module's exact
model, supply voltage and trigger polarity before wiring. Do not connect a
mains or heater load while testing GPIO behavior.
