# ESP32 IR heater relay client

This PlatformIO project runs the IR-heater relay logic locally on an
ESP32-WROOM-32. It polls the Venus OS Node-RED client for fresh battery voltage
and Victron VE.Bus mode, synchronizes its clock from the Fritz!Box, persists
settings in NVS, and exposes an authenticated HTTPS API for status and control.

## Heater behavior

The physical relay always starts OFF at boot. If no setting has been stored,
heater permission defaults to disabled; an API `on` command enables automatic
control and persists that permission in NVS, so it remains enabled after a
reboot. It does not directly force the relay on. The relay can energize only
when all of these conditions are true:

- Heater permission is enabled in persistent settings.
- Local time is synchronized and between **06:00 and 23:59 Europe/Berlin**.
- Recent, valid voltage and inverter-mode data is available from Node-RED.
- Victron VE.Bus mode is `2` (**Inverter Only**).
- Battery voltage reaches **53.8 V** to turn ON. At or below **52.1 V**, it
  turns OFF; between those thresholds it retains the previous relay state.
- A valid DHT20 measurement is available. The initial room-temperature target
  is **21.0 °C** with a **0.5 °C total hysteresis band**: heating can turn ON
  below **20.5 °C** and turns OFF at or above **21.0 °C**. Within that band the
  current relay state is retained.

An API `off` command always disables heater permission and immediately turns
the relay OFF. Wi-Fi, HTTPS, authentication, stale-data, invalid-response,
clock-sync, time-window, inverter-mode, or DHT20 failures also keep the relay
OFF.

These voltage thresholds are the initial chosen values, not guaranteed SOC
limits. Validate them and all fail-safe behavior before connecting or enabling
the infrared heater.

## HTTPS API

The ESP32 advertises `_https._tcp` using mDNS at
`https://<CLIENT_ID>.local` (default `https://esp32-relay-01.local`).
Every endpoint requires HTTP Basic Authentication over HTTPS using
`ESP_API_USER` and `ESP_API_PASSWORD`.

### `GET /api/v1/status`

Returns client identity and API methods, clock and control reason, named
sensors, and actuators. Fresh battery voltage is exposed as a battery sensor;
inverter mode remains internal to relay control and is printed to USB serial
for diagnostics.

```json
{
  "client": {
    "clientId": "esp32-relay-01",
    "status": "active",
    "statusReason": "active",
    "heaterEnabled": false,
    "logEnabled": false,
    "logStorageStatus": "disabled",
    "methods": [
      {"method": "GET", "path": "/api/v1/status"},
      {"method": "POST", "path": "/api/v1/relay"},
      {"method": "POST", "path": "/api/v1/settings"}
    ],
    "clock": {
      "synchronized": true,
      "localTime": "2026-10-21T20:09:23",
      "timeZone": "Europe/Berlin"
    },
    "controlReason": "disabled_by_api",
    "temperatureControl": {
      "targetTemperatureC": 21,
      "hysteresisC": 0.5,
      "turnOnBelowC": 20.5,
      "turnOffAtOrAboveC": 21
    },
    "log": []
  },
  "sensors": [
    {
      "id": "room-climate",
      "name": "Temperatur und Luftfeuchtigkeit",
      "type": "DHT20",
      "status": "on",
      "temperatureC": 22.45,
      "relativeHumidityPercent": 48.2
    },
    {
      "id": "battery-voltage",
      "name": "Akku",
      "type": "battery",
      "status": "on",
      "voltageV": 53.42
    }
  ],
  "actuators": [
    {
      "id": "infrared-heater-relay",
      "name": "Infrarot Heizung",
      "type": "relay",
      "status": "off"
    }
  ]
}
```

Sensor readings are `null` and the sensor status is `off` until a valid DHT20
sample less than 30 seconds old is available. Battery voltage is `null` and
its status is `off` until valid Node-RED data less than 120 seconds old is
available. The actuator's single `status`
field is the physical relay state. Automatic-control permission can be inferred
from `client.controlReason`: `disabled_by_api` means it is disabled; any other
reason means permission is enabled but may be inhibited. The reason reports the
local control decision and is not itself the mechanism that switches the relay.
`client.status` is a client health state: it is `active` only while Wi-Fi and
the HTTPS API are available, the clock is synchronized, fresh Victron/Node-RED
data and a fresh DHT20 sample are available.
Otherwise it is `error`; `client.statusReason` identifies the first failed
condition (`wifi_disconnected`, `https_api_unavailable`,
`clock_not_synchronized`, `settings_storage_unavailable`,
`victron_data_unavailable`, or `sensor_data_unavailable`). A deliberate heater
disable (`client.controlReason: disabled_by_api`) does not make the client
unhealthy. Historical entries in `client.log` do not by themselves change the
current health status. The actuator's `status` remains `on` or `off` and
describes only the physical relay.
`client.controlReason` can be `disabled_by_api`,
`clock_not_synchronized`, `outside_heating_hours`,
`victron_data_unavailable`, `sensor_data_unavailable`,
`inverter_not_in_inverter_only_mode`, or `automatic_control`. `client.clock` contains the
local time and synchronization state. `client.temperatureControl` reports the
active thermostat setpoint and switching thresholds.

Settings are stored in ESP32 NVS. If no stored value exists, defaults are
heater permission `false`, target `21.0 °C`, and API logging `false`. The
heater permission changed through `POST /api/v1/relay` survives reboot and is
restored at startup; use `{"state":"off"}` to persistently disable it. Since
this can resume automatic heating after power is restored, the heater still
must satisfy every local safety condition before the relay can turn on.

Change the temperature target with `POST /api/v1/settings`:

```json
{"targetTemperatureC":22.5}
```

Persistently enable or disable automatic heater control through the same
endpoint:

```json
{"heaterEnabled":true}
```

The equivalent `POST /api/v1/relay` request remains available.

Change persistent API logging through the same endpoint:

```json
{"apiLoggingEnabled":true}
```

Send one setting per request. `heaterEnabled` and `apiLoggingEnabled` accept
booleans; temperature targets must be from **5 to 35 °C**. Invalid values are
rejected. Settings are changed with one authenticated `POST /api/v1/settings`
request per value. A successful change is written to NVS before it is applied,
and unchanged values are not rewritten. NVS stores these small, infrequently
changed settings persistently; routine control and sensor polling do not write
them. The status API reports the current values as
`client.heaterEnabled`, `client.temperatureControl.targetTemperatureC`, and
`client.logEnabled`.

Persistent API logging is **disabled by default**. While disabled, the firmware
does not mount SPIFFS for logs or write API calls to flash; `client.log` is
empty and `client.logStorageStatus` is `disabled`. Enable it with the settings
API above. When enabled, requests, responses and any resulting errors are
appended to `/api.log`; the status response contains at most the latest 50
entries under `client.log`. Log compaction is batched to reduce flash writes,
retaining 50 transactions after compaction; it runs when a new entry brings the
file to 62 entries. Authorization headers and unrecognized request bodies are
never written; relay request bodies are logged only in normalized `state`
form. The current status response is omitted from its own persisted response
snapshot to avoid recursively duplicating the previous log.
`client.logStorageStatus` is `ready` when enabled storage works and `error`
when SPIFFS is unavailable or a write fails. A first start with logging enabled
initializes the dedicated SPIFFS partition if it has not been formatted yet.

### `POST /api/v1/settings`

Persistently update one setting per request. `targetTemperatureC` accepts a
number from 5 to 35; `heaterEnabled` and `apiLoggingEnabled` accept booleans.
Invalid or out-of-range values return `400 Bad Request`; storage failures
return `500 Internal Server Error`.

### `POST /api/v1/relay`

Enable automatic heating permission (still subject to time, voltage and
inverter conditions):

```json
{"state":"on"}
```

Disable the heater unconditionally:

```json
{"state":"off"}
```

The response has the same shape as `GET /api/v1/status`. Invalid JSON or state
values return `400 Bad Request`; missing or incorrect credentials return
`401 Unauthorized`.

## Configure and flash

1. Install the VS Code PlatformIO IDE extension.
2. Open this directory as a PlatformIO project. The `esp32dev` environment
   targets a generic ESP32-WROOM-32 development board.
3. Copy `secrets_template.h` to `secrets.h`.
4. Set Wi-Fi credentials, `NTP_SERVER` (Fritz!Box default
   `192.168.178.1`), Node-RED URL and credentials, and a unique ESP32 API
   password. The username default is `HomeNodes`; use matching credentials in
   the Node-RED `httpNodeAuth` configuration. Do not reuse passwords between
   devices.
5. Paste the complete Node-RED certificate/CA into
   `NODERED_CA_CERTIFICATE_PEM`. Paste this ESP32's complete server certificate
   and private key into `ESP_SERVER_CERTIFICATE_PEM` and
   `ESP_SERVER_PRIVATE_KEY_PEM`. The ESP certificate SAN must match
   `<CLIENT_ID>.local`. Keep the private key and all of `secrets.h` out of
   version control.
6. Connect the board by USB, use PlatformIO **Build** and **Upload**, then
   open **Monitor** at 115200 baud.

The repo's ignored local `esp32-relay-cert.pem` and `esp32-relay.key` are the
server identity for this relay. The public certificate is also needed by API
clients to validate the ESP32 HTTPS endpoint. Never disable TLS validation.

Node-RED returns `503 Service Unavailable` when the voltage or inverter-mode
measurement is missing or stale. Its internal response groups those readings
under `sensors.battery.voltage` and `sensors.inverter.mode`; mode `2` is the
only mode that permits heating.

## Clock synchronization

The ESP32 uses `NTP_SERVER` (default `192.168.178.1`) as its only NTP source;
there is no public-pool fallback. The router must provide NTP over the LAN.
The local timezone is Europe/Berlin with daylight-saving transitions. Until
the clock is valid, heater enable cannot energize the relay.

## Hardware

The DHT20 temperature/humidity sensor is read over I²C every 10 seconds and
its values are printed to the serial monitor at 115200 baud. These readings are
currently diagnostic only: they are not included in the HTTPS API and do not
affect heater control.

Connect the DHT20 module to 3.3 V (not 5 V) and use the default I²C pins below.
For the initial relay test, use a single-channel relay module:

| ESP32-WROOM-32 | Device | Connection |
| --- | --- | --- |
| 3V3 | DHT20 | `+` / VCC |
| GND | DHT20 and relay module | `-` / GND |
| GPIO 21 | DHT20 | SDA |
| GPIO 22 | DHT20 | SCL |
| 3V3 | Relay module | VCC |
| GPIO 13 | Relay module | IN1 |

This assumes the module accepts 3.3 V power/logic and is active HIGH. Verify
the exact relay module and board. Keep the heater and all mains loads
disconnected while testing the firmware and API. The serial monitor reports
sensor connection, measurement or checksum errors explicitly.
