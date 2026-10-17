# ESP32 client projects

Each ESP32 firmware lives in its own directory and is an independent
PlatformIO project. Open the specific client directory in VS Code, then use
the PlatformIO tasks to build, upload and monitor that device.

Install the VS Code extension **PlatformIO IDE**
(`platformio.platformio-ide`) to use these projects from VS Code. The
repository also recommends this extension through `.vscode/extensions.json`.

Current projects:

- [`esp32_relay/`](esp32_relay/) — ESP32-WROOM-32 client that polls the
  Node-RED battery-voltage endpoint over verified HTTPS and controls a relay
  locally.
- [`esp32_relay_blink/`](esp32_relay_blink/) — standalone relay hardware test
  that switches every 500 ms without network or sensor functionality.
- [`tests/`](tests/) — REST Client `.http` requests for testing the Node-RED
  voltage endpoint.

Create future clients as sibling directories with their own `platformio.ini`,
`src/main.cpp` and project-specific README. This keeps board settings,
dependencies and build outputs separate between devices.
