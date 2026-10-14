# ESP32 client projects

Each ESP32 firmware lives in its own directory and is an independent
PlatformIO project. Open the specific client directory in VS Code, then use
the PlatformIO tasks to build, upload and monitor that device.

Install the VS Code extension **PlatformIO IDE**
(`platformio.platformio-ide`) to use these projects from VS Code. The
repository also recommends this extension through `.vscode/extensions.json`.

Current projects:

- [`esp32_relay/`](esp32_relay/) — HTTPS relay client for a generic
  ESP32-WROOM-32 development board.
- [`esp32_relay_blink/`](esp32_relay_blink/) — standalone relay hardware test
  that switches every 500 ms without network or sensor functionality.

Create future clients as sibling directories with their own `platformio.ini`,
`src/main.cpp` and project-specific README. This keeps board settings,
dependencies and build outputs separate between devices.
