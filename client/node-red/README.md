# Node-RED flows

## Venus OS Victron-state endpoint

Import [`ir_heater_voltage_relay.json`](ir_heater_voltage_relay.json) into the
Node-RED editor on Venus OS. The flow only publishes the latest Victron battery voltage and VE.Bus mode;
the ESP32 owns the relay hysteresis and switching logic. It does not
replace or modify the existing Tibber charging flow.

The flow reads `com.victronenergy.system/0` at `/Dc/Battery/Voltage`, caches
valid readings and serves:

```text
GET /homenodes/v1/battery-voltage
```

A successful response is JSON:

```json
{
  "voltage": 53.42,
  "updatedAt": 1792173477458
}
```

`updatedAt` is the response time as Unix milliseconds. `inverterMode` is the
VE.Bus `/Mode` integer. Mode `2` is Inverter Only and is the sole mode that
permits heating; mode `3` (`On`), Charger Only, and Off inhibit it. The endpoint
returns `503 Service Unavailable` if either Victron value is missing or older
than 120 seconds. The flow ignores non-numeric battery readings, voltages
outside 30–65 V, and invalid mode values.

The ESP32 polls this endpoint and applies both the 06:00–23:59 Europe/Berlin
time window and the mode-2 requirement locally, together with its voltage
hysteresis. The Venus OS Raspberry Pi remains a client/data provider; it is
not the project controller.

### Secure the endpoint

The flow export does not configure transport security or authentication.
Before using it, configure the Node-RED HTTP In service for HTTPS (directly
using Node-RED's HTTPS settings or through a trusted HTTPS reverse proxy) and
enable HTTP Basic Authentication for HTTP In routes with `httpNodeAuth` in
Node-RED's `settings.js`. Use dedicated credentials for the ESP32 and do not
put passwords into the flow JSON. Node-RED's built-in HTTP-node authentication
configuration applies to all HTTP In endpoints in that runtime.

Restrict access to the automation LAN/firewall as appropriate. The ESP32
validates the HTTPS server certificate and hostname. Put the trusted
certificate/CA, endpoint URL and Basic Auth credentials in the ignored local
file `client/esp32_relay/secrets.h`; do not disable certificate verification.
The certificate SAN must match the hostname in that URL.

For example, from a machine that trusts the certificate:

```sh
curl --cacert venus-node-red-ca.pem \
  -u 'ESP32_USER:ESP32_PASSWORD' \
  https://venus.local:1881/homenodes/v1/battery-voltage
```

Configure `NODERED_BATTERY_VOLTAGE_URL` to the actual Venus OS hostname or
address and HTTPS port. If connecting by IP address, the server certificate
needs that exact IP address as an IP SAN. mDNS names ending in `.local` may not
resolve on all devices.

## Credential handling

Never commit live tokens, passwords, `secrets.h`, private keys or other local
credentials. The earlier shared Tibber token and ESP Basic Auth password
should be rotated before continued use.
