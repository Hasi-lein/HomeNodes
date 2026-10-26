# Client projects

Each device client lives in its own directory with an independent runtime and
setup. The ESP32 firmware uses PlatformIO. The Raspberry Pi controller app
lives separately in [`../controller/raspberry_pi_climate/`](../controller/raspberry_pi_climate/)
and runs in Docker.

Install the VS Code extension **PlatformIO IDE**
(`platformio.platformio-ide`) to build and upload ESP32 firmware from VS Code.
The repository also recommends this extension through
`.vscode/extensions.json`.

Current projects:

- [`esp_32_climate_controll/`](esp_32_climate_controll/) — ESP32-WROOM-32
  client that polls Node-RED, controls the relay locally and exposes an
  authenticated HTTPS status/control API.
- [`node-red/`](node-red/) — Venus OS Node-RED flow serving current Victron
  battery voltage and inverter mode to clients.
- [`tests/`](tests/) — REST Client `.http` requests for testing the ESP32 relay
  API.

Create future clients as sibling directories with their own runtime
configuration and project-specific README. This keeps board settings,
dependencies and build outputs separate between devices.
