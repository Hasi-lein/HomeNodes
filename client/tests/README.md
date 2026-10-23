# ESP32 relay API tests

The `esp32_relay.http` file contains Visual Studio Code REST Client requests
for the ESP32 relay client's HTTPS API.

1. Install the **REST Client** extension (`humao.rest-client`) if needed.
2. Open `esp32_relay.http` and set `@host` to the ESP32's mDNS name or IP.
3. Send `GET /api/v1/status` to inspect client connectivity, named sensor and
   actuator state, and local time. Inverter voltage and mode are not exposed
   through this API; they are available through USB serial diagnostics.
4. Send `POST /api/v1/relay` with `{"state":"on"}` to enable automatic
   control, or `{"state":"off"}` to force the heater off.

An `on` request only enables the automatic control; time, voltage, and
Victron-mode requirements still apply. Heater permission resets to disabled
after reboot. Keep the heater and mains loads disconnected during initial
testing.

The REST Client extension currently does not validate server certificates.
A successful request is not proof of TLS certificate or hostname validation.
For a verified test, use a client configured to trust the ESP32's certificate
while keeping certificate and hostname verification enabled.
