# Raspberry Pi room-climate client

This Dockerized Python client polls the ESP32 relay's authenticated HTTPS
status endpoint once per minute, reads its fresh DHT20 temperature,
relative-humidity and battery-voltage values, and appends timestamped
measurements to a JSON Lines file. It serves an HTTPS browser dashboard with a
hoverable graph for temperature, humidity and battery voltage. The Pi does not
connect to the sensor over I²C.

The target is a Raspberry Pi 3B running 32-bit Raspberry Pi OS. The dashboard
is available on the local network at `https://junge.local`.

## Install Docker on the Pi

For Raspberry Pi OS 32-bit (`armv7l`), install Docker Engine using Docker's
official convenience script:

```sh
curl -fsSL https://get.docker.com -o /tmp/get-docker.sh
sudo sh /tmp/get-docker.sh
rm /tmp/get-docker.sh
sudo usermod -aG docker "$USER"
```

Log out of SSH and reconnect so the new group membership applies. Verify the
installation:

```sh
docker run --rm hello-world
docker compose version
```

## Configure the ESP32 API and HTTPS certificates

The Pi polls `https://esp32-relay-01.local/api/v1/status` using the ESP32's
HTTP Basic Authentication credentials. It verifies the ESP32 certificate;
certificate validation is not disabled.

The ESP32 advertises its `.local` hostname with multicast DNS. Docker
containers do not reliably resolve mDNS names, so Compose maps
`esp32-relay-01.local` to `ESP32_HOST_IP` in `.env`. Reserve the ESP32's
current DHCP address in the router and set that address in `.env`; update it
there if the ESP32's address changes.

Copy the ESP32's **public server certificate** from the Mac to the Pi. Run
this on the Mac from the HomeNodes repository:

```sh
scp client/esp32_relay/esp32-relay-cert.pem \
  junge@junge:/home/junge/HomeNodes/client/raspberry_pi_climate/certs/esp32-relay-cert.pem
```

The certificate file is local and ignored by Git; it must already exist in
`client/esp32_relay/`. Do not copy the ESP32 private key.

On the Pi, create the application configuration from the template:

```sh
cd /home/junge/HomeNodes/client/raspberry_pi_climate
cp .env.example .env
chmod 600 .env
nano .env
```

Set `ESP32_API_PASSWORD` to the password configured as `ESP_API_PASSWORD` in
the ESP32 firmware's local `secrets.h`. Do not paste the password into chat or
commit `.env`. Set `ESP32_HOST_IP` to the relay's current address; its current
lease can be checked on the Pi with `getent ahostsv4 esp32-relay-01.local`.

Create a local certificate authority and an HTTPS server certificate for the
Pi's dashboard hostname:

```sh
./create-certificates.sh junge
```

The script creates server and CA private keys in the ignored `certs/`
directory. Keep `certs/ca.key` private; only copy `certs/ca.crt` to browser
devices you control. Install that public CA as a trusted root certificate on
each device that will visit the dashboard, then browse to `https://junge.local`.
The dashboard certificate is for `junge` and `junge.local`, not the Pi's IP
address.

To copy the public dashboard CA certificate to a Mac, run:

```sh
scp junge@junge:HomeNodes/client/raspberry_pi_climate/certs/ca.crt \
  ~/Downloads/homenodes-ca.crt
```

On macOS, import it into the **System** keychain with **Keychain Access**, open
the **HomeNodes Local CA** certificate, expand **Trust**, set **When using this
certificate** to **Always Trust**, and confirm.

## Start and operate the app

On the Pi:

```sh
cd /home/junge/HomeNodes/client/raspberry_pi_climate
docker compose up --build -d
docker compose logs -f
```

The Python service polls the ESP32 once per minute. It logs API, TLS, stale
sensor and JSON errors explicitly and does not record a failed sample. The
chart's hover tooltip shows the Pi's UTC request time and the returned
temperature and humidity. The dashboard also shows the latest fresh battery
voltage reported by the ESP32. The heater panel shows the physical relay state,
automatic-control permission and ESP32 control reason. Its target-temperature
form changes the ESP32's persistent setpoint; accepted values are 5–35 °C.
The API credentials stay on the Pi and are never sent to the browser.
The battery tile remains `—` until the ESP32 is running firmware that includes
the battery-voltage sensor in its status response.
The chart plots battery voltage on its own scale and includes it in the hover
tooltip; historic records without a voltage value simply omit that series.

The JSON Lines archive is stored at `data/readings.jsonl`. Each line is one
measurement with an ISO-8601 UTC sample time, temperature, humidity and (when
available) battery voltage. Up to
the latest **10,000** records are retained for the API and graph. To reduce SD
card writes, the file is compacted atomically in batches: it can grow by up to
1,000 entries past the retention target before it is rewritten with the newest
10,000. At a one-minute interval, the archive holds about seven days of
history.

Useful commands:

```sh
docker compose ps
docker compose logs --tail=100
docker compose restart
docker compose down
```

`docker compose down` preserves `data/readings.jsonl`; do not delete the
`data/` directory if you want to retain measurements.

## Dashboard and API

- `/` — current room temperature/humidity and graphs for 1 hour, 6 hours,
  24 hours, 7 days or 30 days.
- `/api/v1/status` — app health and latest saved reading.
- `/api/v1/readings?hours=24` — timestamped readings for a supported period.
- `POST /api/v1/heater/target` — update the persistent ESP32 setpoint with
  `{"targetTemperatureC":22.5}`.

The browser HTTPS certificate encrypts traffic and verifies the dashboard
server after its CA is trusted. It does not authenticate users. Keep the
dashboard on a trusted home network and do not expose it to the internet.
The setpoint endpoint is available to clients on that network; ESP32 API
credentials remain server-side, and only the target-temperature setting is
forwarded.

## Local checks

Run tests without Pi hardware:

```sh
python3 -m unittest discover -s tests
```

Tests cover ESP32 response parsing, stale/invalid sensor data, JSON Lines
retention and API output.
