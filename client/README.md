# ESP32 client projects

Each ESP32 firmware lives in its own directory and is an independent
PlatformIO project. Open the specific client directory in VS Code, then use
the PlatformIO tasks to build, upload and monitor that device.

Current projects:

- [`esp32_relay/`](esp32_relay/) — HTTPS relay client for the AI-Thinker
  ESP32-CAM board.

Create future clients as sibling directories with their own `platformio.ini`,
`src/main.cpp` and project-specific README. This keeps board settings,
dependencies and build outputs separate between devices.
