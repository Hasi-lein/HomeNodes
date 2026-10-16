# Client API tests

The `esp32_relay.http` file contains Visual Studio Code REST Client requests
for the ESP32 relay client's HTTPS API.

## Run the requests

1. Install the VS Code extension **REST Client**
   (`humao.rest-client`) if needed.
2. Open `esp32_relay.http`.
3. Use **Send Request** above a request.
4. Enter the relay's HTTP Basic Auth password when prompted.

The target hostname and username are defined at the top of the request file.
Change them if the ESP32's `CLIENT_ID` or HTTP Basic Auth username differs.
The `on` and `off` requests switch the relay; leave the heater and mains loads
disconnected during initial testing.

## TLS verification warning

The current REST Client extension implementation sends HTTPS requests without
validating the server certificate (`rejectUnauthorized` is disabled in its
HTTP client). Therefore, these requests are useful for local API/relay
functional testing, but they do **not** verify the ESP32's self-signed
certificate or hostname. A successful request does not establish that TLS
certificate validation is working.

To test certificate trust and hostname validation, use a client that supports
a custom CA/certificate while keeping verification enabled, for example:

```sh
curl --cacert client/esp32_relay/esp32-relay-cert.pem \
  -u esp32-relay-01 \
  https://esp32-relay-01.local/api/v1/status
```

`curl` prompts for the password if it is not supplied after `-u`.
