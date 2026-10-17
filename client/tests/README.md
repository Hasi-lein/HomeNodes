# Node-RED endpoint test

The `esp32_relay.http` file contains a Visual Studio Code REST Client request
for the Node-RED battery-voltage endpoint on Venus OS.

## Run the request

1. Install the VS Code extension **REST Client**
   (`humao.rest-client`) if needed.
2. Open `esp32_relay.http`.
3. Change the hostname if needed, then use **Send Request**.
4. Enter the Node-RED HTTP Basic Auth username and password when prompted.

The endpoint should return `200 OK` and a JSON object containing `voltage` and
`updatedAt` when a recent Victron measurement is available. A missing or stale
sample returns `503 Service Unavailable`.

## TLS verification

The current REST Client extension implementation sends HTTPS requests without
validating the server certificate (`rejectUnauthorized` is disabled in its
HTTP client). A successful request therefore does **not** verify TLS trust or
hostname validation.

To verify the Node-RED certificate and hostname, use a client configured with
the correct trusted certificate/CA, for example:

```sh
curl --cacert venus-node-red-ca.pem \
  -u ESP32_USER \
  https://venus.local:1881/homenodes/v1/battery-voltage
```

`curl` prompts for the password if it is not supplied after `-u`.
