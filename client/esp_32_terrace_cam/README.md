# ESP32 terrace camera client

This PlatformIO project targets the AI-Thinker ESP32-CAM. It follows the
HomeNodes ESP32 client pattern: local control remains on the device, its HTTPS
API uses Basic Authentication and a device certificate, and image uploads
validate the remote server certificate. It reports DHT20 temperature and
humidity and sends JPEG images to the configured upload endpoint.

## Hardware and pins

The firmware uses the AI-Thinker camera pinout from the board's camera
schematic. It does not use the SD-card interface.

| AI-Thinker ESP32-CAM | Device | Connection |
| --- | --- | --- |
| 3V3 | DHT20 | VCC |
| GND | DHT20, PIR and relay module | GND |
| GPIO 13 | DHT20 | SDA |
| GPIO 14 | DHT20 | SCL |
| GPIO 15 | PIR motion sensor | Digital output |
| GPIO 4 | Light relay module | IN |

GPIO 13/14 are the shared HomeNodes I²C pins. GPIO 4 is also connected to the
AI-Thinker white flash LED, so the on-board LED will light whenever the light
relay is energized. GPIO 15 is a boot-strapping pin; keep attached PIR
hardware from forcing an invalid level while the ESP32 resets. Verify the
actual PIR output and relay input levels before connecting the light.

The relay output starts LOW (OFF), matching the supplied hardware sketch.
Confirm that the relay module is active HIGH and compatible with 3.3 V GPIO
logic. Keep mains wiring disconnected while testing.

The existing climate controller now uses the same SDA 13/SCL 14 I²C pins.
Its heater-relay signal has moved from GPIO 13 to GPIO 27. The heater relay
wire must be moved to GPIO 27 before that firmware is flashed.

## Local configuration and certificates

Copy `secrets_template.h` to `secrets.h`; the latter is ignored by Git.
Configure the Wi-Fi network, a unique API password, upload endpoint credentials,
and trusted CA certificate for `normal.bio`. Do not put real credentials or
private keys in the template or commit them.

Create an HTTPS server certificate with `homenodes-terrace-cam-01.local` in
its subject alternative name, matching `CLIENT_ID` in `secrets.h`. For example,
from this directory:

```sh
openssl req -x509 -nodes -days 825 -newkey rsa:2048 \
  -keyout homenodes-terrace-cam-01.key \
  -out homenodes-terrace-cam-01-cert.pem \
  -config homenodes-terrace-cam-cert.cnf
```

The certificate and private key are generated locally in the ignored
`secrets.h` for this device. Keep the private key private. Copy only the public
server certificate to any API client that needs to verify this ESP32.

For outbound uploads, place the trusted root CA for the HTTPS certificate used
by `normal.bio` in `IMAGE_UPLOAD_CA_CERTIFICATE_PEM`. Do not paste the web
server's CRT/leaf certificate there: it is not a CA certificate and cannot
validate the chain. The current `normal.bio` chain is Let's Encrypt `YE1`,
issued by `Root YE`; the matching public root is available from
<https://letsencrypt.org/certs/gen-y/root-ye.pem>. Recheck the live chain if the
provider renews or changes its issuer. Certificate validation must remain
enabled; the firmware deliberately does not use `setInsecure()`. The upload
endpoint must accept `multipart/form-data` with a JPEG file field named `image`
and HTTP Basic Authentication.

## Build and flash

1. Open this directory as a PlatformIO project in VS Code.
2. Create and fill in the ignored local `secrets.h`.
3. Connect the AI-Thinker board's serial adapter, hold GPIO 0 low for flashing,
   then build and upload the `esp32cam` environment.
4. Disconnect GPIO 0 from ground and reset the board for normal operation.
5. Open the serial monitor at 115200 baud. The device advertises
   `https://homenodes-terrace-cam-01.local` on the local network.

PSRAM is enabled and used for the camera frame buffer. The SD card is not
initialized; images are uploaded directly and are not queued on flash when the
network is unavailable.

## Behavior and API

The PIR input is sampled locally. Light mode and camera mode are independent:

- Light defaults to `auto`; detected motion turns the relay on while the PIR
  output is high. The relay turns off when motion clears.
- A timed manual light request turns the relay on for **1–60 minutes**, then
  switches the light mode to `off`.
- Camera mode defaults to `off` for privacy. In `auto`, each PIR rising edge
  schedules three images, uploaded about five seconds apart.
- A manual capture schedules three images regardless of automatic camera
  mode. It returns immediately with HTTP `202`; progress is available through
  status. Only one three-image batch can run at once; motion while it is busy
  does not queue another batch.
- Light and camera modes are stored in ESP32 NVS. A timed light request saves
  `off` before turning the light on, so it is not restored after reboot.
  Camera mode defaults to `off` on first boot.
- Image upload failures are reported in `camera.captureState`; the device
  does not save failed uploads for later retry.

Every API route requires Basic Authentication over HTTPS:

| Method and path | Request | Purpose |
| --- | --- | --- |
| `GET /api/v1/status` | — | Health, DHT20, light, PIR and camera state |
| `POST /api/v1/light` | `{"mode":"auto"}` | Enable motion-controlled light |
| `POST /api/v1/light` | `{"mode":"off"}` | Turn light off |
| `POST /api/v1/light` | `{"mode":"on","durationMinutes":15}` | Turn light on temporarily |
| `POST /api/v1/camera` | `{"mode":"auto"}` | Enable camera capture on motion |
| `POST /api/v1/camera` | `{"mode":"off"}` | Disable automatic capture |
| `POST /api/v1/camera/capture` | Empty body | Queue three manual images |

Example requests:

```sh
curl --cacert esp32-terrace-cam-cert.pem \
  -u 'HomeNodes:YOUR_LOCAL_API_PASSWORD' \
  https://homenodes-terrace-cam-01.local/api/v1/status

curl --cacert esp32-terrace-cam-cert.pem \
  -u 'HomeNodes:YOUR_LOCAL_API_PASSWORD' \
  -H 'Content-Type: application/json' \
  -d '{"mode":"on","durationMinutes":15}' \
  https://homenodes-terrace-cam-01.local/api/v1/light

curl --cacert esp32-terrace-cam-cert.pem \
  -u 'HomeNodes:YOUR_LOCAL_API_PASSWORD' -X POST \
  https://homenodes-terrace-cam-01.local/api/v1/camera/capture
```

Keep the device on the trusted home network. HTTPS protects API credentials
and uploaded images in transit; it does not replace authorization on the
receiving web endpoint or network access controls.
