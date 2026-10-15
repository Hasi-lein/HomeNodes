# ESP32 relay client

This PlatformIO project turns the existing relay prototype into a small HTTPS
API server. It keeps the relay on GPIO 13 (active HIGH, as in the previous
sketch) and starts with the relay off.

## Configure and flash

1. Install the PlatformIO IDE extension in VS Code.
2. Open this `esp32_relay` directory as the PlatformIO project. The
   `esp32dev` environment targets a generic ESP32-WROOM-32 development board
   using the Arduino framework.
3. Copy `secrets_template.h` to `secrets.h`.
4. Set the automation WLAN, unique client ID, and a unique HTTP Basic Auth
   username/password.
5. The repository includes the non-secret OpenSSL configuration
   `esp32-relay-cert.cnf`. Its Subject Alternative Name (SAN) must match the
   exact hostname the controller will use. If you change `CLIENT_ID` or the
   hostname, update the `CN` and `DNS.1` entries in that file to match.
   Generate a self-signed TLS server certificate and private key:

   ```sh
   openssl req -x509 -newkey rsa:2048 -sha256 -days 365 -nodes \
     -keyout esp32-relay.key \
     -out esp32-relay-cert.pem \
     -config esp32-relay-cert.cnf \
     -extensions server_cert
   ```

   `esp32-relay-cert.pem` is the public certificate; `esp32-relay.key` is
   private. Replace the placeholder text inside the `TLS_CERTIFICATE_PEM` and
   `TLS_PRIVATE_KEY_PEM` raw-string blocks in `secrets.h` with the complete
   PEM contents. Keep each `R"PEM(` and `)PEM"` delimiter on its own line.
   Never commit the private key or `secrets.h`.
6. Configure the controller's HTTPS client to explicitly trust this
   self-signed certificate (for example, as its CA/trusted certificate file).
   Keep peer and hostname verification enabled; do not use an insecure TLS
   option. If the controller runs in Docker, make the public `.pem` certificate
   file available inside the container and configure the HTTP client to use it.
7. Connect the board by USB, select the `esp32dev` environment, and use
   PlatformIO's Build and Upload tasks. Use Monitor to view the serial output
   at 115200 baud.

The project source is in `src/main.cpp`. Add other ESP32 firmware as separate
client directories, each with its own `platformio.ini`.

The client advertises `_https._tcp` over mDNS and can be reached at
`https://<CLIENT_ID>.local`. For TLS validation, the hostname used by the
controller must match a DNS name in the certificate SAN. A self-signed
certificate is not trusted automatically: the controller must trust it
explicitly. Connecting by IP address requires that exact IP address as an IP
SAN in the certificate. Prefer a stable hostname; mDNS names ending in `.local`
may not resolve automatically from inside Docker, depending on the host and
container network configuration.

The certificate expires after the configured validity period (365 days in the
example). Renew it before expiry, update the certificate in `secrets.h`, and
update the controller's trusted certificate at the same time. Replacing the
certificate without updating the controller trust configuration will cause
TLS validation to fail, as intended.

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
