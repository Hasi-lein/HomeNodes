# HomeNodes

Decentralized home automation with locally autonomous clients, a central
controller, and a focus on energy management.

## Project status

This is an early-stage personal project. The architecture and hardware notes
are evolving, and the ESP32 relay client is a development prototype. It has
not yet been built or tested on hardware.

## Current direction

- A computer on a dedicated automation Wi-Fi network runs the controller
  application in Docker, with a web frontend. PHP/Symfony is the current
  preference.
- The controller searches the network for clients and provides a GUI to inspect
  and configure them. The Venus OS Raspberry Pi is a client, not the controller.
- ESP32 clients use C++ firmware and perform their local control
  autonomously.
- The controller discovers and configures clients over HTTPS/REST. Initial
  authentication is HTTP Basic Authentication over TLS, using
  device-specific credentials.
- Client-to-client links should continue operating without the controller
  after configuration.
- A Raspberry Pi running Venus OS and Node-RED acts as a specialized Victron
  client for the inverter, MPPT charge controller and Tibber-based charging
  control.

## Repository contents

- [`Doku/miro_import.md`](Doku/miro_import.md) — editable English architecture
  draft and hardware notes.
- [`Doku/miro_export.pdf`](Doku/miro_export.pdf) — original Miro PDF export,
  retained as a source reference. The original export is in German and includes
  diagrams of the home's electrical installation.
- [`client/esp32_relay/`](client/esp32_relay/) — ESP32-WROOM-32 relay
  prototype; polls Venus OS Node-RED, runs relay logic locally, and exposes
  authenticated HTTPS status/control endpoints.
- [`client/raspberry_pi_climate/`](client/raspberry_pi_climate/) — Dockerized
  Python client for a Raspberry Pi 3B; logs DHT20 room temperature and
  humidity to SQLite and provides a local browser dashboard.
- [`client/tests/`](client/tests/) — VS Code REST Client requests for exercising
  client APIs locally.
- [`client/node-red/`](client/node-red/) — Node-RED client flow on Venus OS;
  serves fresh Victron battery voltage and inverter mode to the ESP32.

## ESP32 development in VS Code

Install the **PlatformIO IDE** extension (`platformio.platformio-ide`) from
the VS Code Extensions view. This repository recommends it automatically when
opened in VS Code; accept the workspace recommendation if prompted. No
additional VS Code extension is required for the current Arduino-framework
PlatformIO projects.

PlatformIO installs the selected ESP32 platform, compiler and framework
packages when a project is built for the first time. Open an individual
firmware project directory, such as `client/esp32_relay_blink`, to build,
upload and monitor that board.

## ESP32 relay prototype

The ESP32 polls the Node-RED endpoint
`GET /homenodes/v1/battery-voltage` over verified HTTPS. It applies local
hysteresis (ON at 53.8 V, OFF at 52.1 V) and a 21 °C thermostat target with a
0.5 °C total hysteresis band, permits heating only 06:00–23:59
Europe/Berlin and only in Victron VE.Bus mode 2 (Inverter Only), and fails OFF
when time or fresh Victron data is unavailable. Its HTTPS API reports status
in client, sensor and actuator groups and lets an authorized caller enable
automatic heating or force it off. Inverter voltage and mode are kept out of
the API response and remain available in USB serial diagnostics. Heater
permission and thermostat target are stored persistently; the default heater
permission is disabled until first enabled through the API. Heater enable,
target temperature and optional API logging can be changed through its
authenticated API. See the relay client README for details.

See [`client/esp32_relay/README.md`](client/esp32_relay/README.md) for setup
details. Do not commit `secrets.h`, private keys or real credentials.

## Safety

This repository is not an electrical installation plan. Have mains wiring,
protection, battery installation and switching arrangements reviewed by a
qualified electrician. Keep hardware safety limits local to each client;
network commands must not bypass them.
