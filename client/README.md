# Client projects

Each hardware client lives in its own directory with an independent runtime
and setup. The ESP32 firmware projects use PlatformIO; the Raspberry Pi
climate monitor runs in Docker.

Install the VS Code extension **PlatformIO IDE**
(`platformio.platformio-ide`) to build and upload ESP32 firmware from VS Code.
The repository also recommends this extension through
`.vscode/extensions.json`.

Current projects:

- [`esp32_relay/`](esp32_relay/) — ESP32-WROOM-32 client that polls Node-RED,
  controls the relay locally and exposes an authenticated HTTPS status/control
  API.
- [`esp32_relay_blink/`](esp32_relay_blink/) — standalone relay hardware test
  that switches every 500 ms without network or sensor functionality.
- [`raspberry_pi_climate/`](raspberry_pi_climate/) — Dockerized Python client
  for DHT20 room-temperature and humidity logging with a browser graph.
- [`node-red/`](node-red/) — Venus OS Node-RED flow serving current Victron
  battery voltage and inverter mode to clients.
- [`tests/`](tests/) — REST Client `.http` requests for testing the ESP32 relay
  API.

Create future clients as sibling directories with their own runtime
configuration and project-specific README. This keeps board settings,
dependencies and build outputs separate between devices.
