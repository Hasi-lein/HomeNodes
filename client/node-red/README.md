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
Node-RED's settings. Use dedicated credentials for the ESP32 and do not
put passwords into the flow JSON. Node-RED's built-in HTTP-node authentication
configuration applies to all HTTP In endpoints in that runtime.

### Venus OS Node-RED 4.x configuration

On the Venus OS installation used for this flow, Node-RED is started with
`/usr/lib/node_modules/node-red/venus-settings.js`. That file loads optional
user overrides from:

```text
/data/home/nodered/.node-red/settings-user.js
```

Do not edit `venus-settings.js` or the generated `.config.users.json` for this
purpose. Create `settings-user.js` with the following structure, replacing the
placeholder with a bcrypt password hash generated locally with
`/usr/bin/node-red admin hash-pw`:

```js
module.exports = {
    httpNodeAuth: {
        user: "HomeNodes",
        pass: "<bcrypt-hash>"
    }
};
```

The password hash belongs only on Venus OS. Configure the corresponding
**plaintext Node-RED API password** in each authorized client's ignored local
`secrets.h`; the client uses it to authenticate each HTTPS request. Do not
share or commit either value. The Node-RED HTTP-In setting has one shared
credential for every HTTP-In route in this runtime; keep this password
dedicated to Node-RED and separate from the ESP32's own API password and the
Venus/editor login. If you change the username to `HomeNodes`, update
`NODERED_HTTP_USER` on each ESP32 client and redeploy/restart Node-RED.

After validating the file with `node --check`, restart the managed process so
it reloads the settings:

```sh
/usr/bin/svc -t /service/node-red-venus
```

On this Venus OS image, `sv` is not installed; `/usr/bin/svc -t` signals the
supervised Node-RED process, which is then started again by `supervise`.
Verify that a request without credentials returns `401 Unauthorized`, and
that a request with the dedicated API credentials returns `200 OK`. This
authentication setting protects every HTTP In route in this Node-RED runtime.

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
