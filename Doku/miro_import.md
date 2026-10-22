# HomeNodes — Architecture Draft

> **Status:** Preliminary transcription and evolving project notes  
> **Source:** Miro board exported to PDF on 2026-09-24  
> **Note:** This editable draft does not replace an electrical installation
> plan. Some details may be incomplete or unreadable in the source export.

## 1. Energy and system overview

### Meter cabinet and grid connection

- Electricity supplier / house connection
- Query current electricity prices every 15 minutes
- Meter / SLS / smart meter
  - E.DIS eHZ, if available
- 6 A RCD/MCB charger (as labelled in the source sketch; verify exact device)
- Grid transfer switch, 3 phases + neutral
- Feed to the meter cabinet

### Kitchen — technical equipment

- Raspberry Pi running Venus OS
- Victron MultiPlus-II 48/15000/200-100 inverter/charger
- Victron MPPT 250/60 MC4 solar charge controller
- 48 V distribution and protection
- Battery bank, 32 kWh nominal
  - 2 × 16 kWh LiFePO4 batteries
  - Connected in parallel
  - One Daly BMS per battery
  - Main switch and overcurrent protection for each battery
  - Batteries are interconnected and connected to the inverter using flexible
    busbars
  - Maximum charge/discharge current per battery: 200 A
  - Each BMS supports CAN, RS-232 and RS-485

The Raspberry Pi running Venus OS is intended as a specialized Victron client,
not as the central controller:

- Venus OS provides the Victron system environment.
- A Node-RED script controls charging based on Tibber electricity prices.
- The MultiPlus-II connects to the Raspberry Pi through a Victron MK3-USB-C
  interface.
- The MPPT charge controller connects to the Raspberry Pi via USB.
- The central controller runs on a separate computer in the automation
  network.

#### Victron MultiPlus-II 48/15000/200-100

The model designation indicates these headline ratings:

- 48 V battery system
- 15,000 VA inverter rating
- 200 A battery charger
- 100 A transfer switch

Verify continuous and peak output, connection details, charger settings and
transfer-switch configuration against the device datasheet and actual
installation.

#### Victron MPPT 250/60 MC4

- MC4 connector version
- Maximum PV open-circuit voltage: 250 V
- Maximum charge current: 60 A

Permitted PV power and string design depend on battery voltage, temperature and
the actual panel configuration.

#### Battery bank and Daly BMS

The battery bank consists of two LiFePO4 batteries, each with a nominal
capacity of 16 kWh, for 32 kWh nominal total capacity.

Each battery has a Daly BMS with the following stated limits and interfaces:

- Maximum charge current: 200 A
- Maximum discharge current: 200 A
- CAN
- RS-232
- RS-485

The batteries are connected in parallel. BMS addressing and the interface to
be used with Venus OS or Node-RED are not yet known. At present, state of charge
(SOC) is estimated from the voltage reported by the inverter; direct SOC
telemetry from the Daly BMS units is not configured. Continuous and peak
currents, protection coordination and protective-device sizing must be checked
against the actual installation.

#### Overall power limit

The two parallel batteries can be charged and discharged together. AC output
is limited by the Victron MultiPlus-II's 15 kVA inverter rating. Additional DC
loads could add to the total installation load, but are currently considered
negligible compared with the inverter rating. Actual limits also depend on
operating mode, batteries, protective devices and connected DC loads.

### Loads and circuits

The sketch shows the following loads and distributions:

- 230 V sockets
- Lighting circuit, including switches
- 48 V USB-C outlets, 73 mm installation depth

## 2. PV system and roof sketch

The draft specifies **5 kWp** total solar capacity:

- Two PV strings, each rated at 2.5 kWp
- 2.5 kWp on each roof side

The PDF also contains a floor plan with dimensions and symbols for switches,
lights and sockets. Exact placement needs to be checked against a more legible
source.

## 3. Preliminary energy flows

```text
Electricity supplier / house connection
                |
                v
       Meter / smart meter ------> Electricity-price query
                |
                v
       Grid transfer switch (3 phases + N)
                |
                v
           Meter cabinet
                |
                v
     MultiPlus-II <------ Victron MK3-USB-C <------ Raspberry Pi
                |                                      |
                v                                      |
       48 V distribution and protection                |
                |                                      |
                v                                      |
       Battery bank: 2 x 16 kWh, parallel              |
                                                       |
                         USB <--- Victron MPPT 250/60 MC4
```

The Raspberry Pi receives Tibber electricity prices and uses them in the
Node-RED charging-control script. The separate controller may monitor and
configure the Pi as a Victron client. Tibber credentials, polling intervals,
price thresholds and safe fallback behavior still need to be specified.

The exported sketch does not make every connection between the PV modules,
inverter/charger and meter cabinet clear. Before implementation, an
electrician must verify the protection scheme, transfer logic, earthing system,
phase allocation and permitted operating modes.

## 4. Decentralized client architecture

A separate computer on the automation network acts as the **controller**. The
controller application is planned to run in Docker with a web frontend
(PHP/Symfony is the current preference). It handles client discovery,
commissioning, configuration, overview and visualization. The Raspberry Pi
running Venus OS is a specialized Victron client. ESP32 clients and the
Victron client should remain as independent as possible during normal
operation. A client handles the local control of its attached sensors and
actuators; required data can be exchanged directly between linked clients.

### Automation network and initial authentication

- Automation devices use a dedicated Wi-Fi network.
- The controller computer also connects to this Wi-Fi network. Its Docker
  application provides the web frontend on the network.
- Clients and the controller initially communicate over HTTPS/REST with HTTP
  Basic Authentication.
- Do not use one shared, permanent default password on every device; use
  device-specific credentials.
- HTTPS/TLS protects credentials and data in transit. The dedicated Wi-Fi
  network provides an additional network boundary; it does not replace
  transport encryption.
- During initial development, ESP32 clients may use locally generated
  self-signed server certificates. The controller must explicitly trust each
  certificate (or a future local CA) while keeping TLS peer and hostname
  verification enabled. The URL hostname must match the certificate SAN.
- OAuth 2.0 is deferred from the first implementation and may be considered
  later.

The controller searches the local network for clients. Once discovered, it can
configure them over HTTP/REST and create relationships between their sensors
and actuators. The controller stores these relationships and visualization
properties such as diagram position, appearance and connections. It is used
for commissioning and monitoring but does not need to be in every linked
client's runtime data path.

The discovery mechanism is still undecided. Scanning every IP address is
possible, but depends on the local network and is complicated by changing DHCP
addresses. LAN service discovery or device announcements and stable client IDs
should be evaluated. A discovered IP address is a current network location,
not a permanent device identity.

### Client-to-client links

For example, the controller can link a temperature-sensor client to a relay
client controlling an infrared heater:

1. The controller discovers both clients and reads their advertised
   capabilities (for example, temperature measurement and relay switching).
2. The user creates a link in the controller's diagram and configures the
   rule, setpoints and local limits.
3. The controller sends the required configuration to both clients over
   HTTP/REST; clients save it for autonomous operation.
4. During normal operation, the sensor client sends measurements directly to
   the relay client, which runs its local control logic.
5. The controller can also display status and measurements. If the controller
   goes offline, the configured link should remain in place and continue its
   basic operation.

```text
Commissioning / configuration:
[Controller] -- HTTPS/REST --> [Temperature client]
      `-------- HTTPS/REST --> [Relay client]

Runtime:
[Temperature client] -- measurement --> [Relay client] --> Infrared heater
          `---------------- status / measurements ------> [Controller]
```

### Example: boiler client

- An ESP32 reads the water temperature and locally controls the boiler heater.
- Local temperature control and permitted operating limits reside on the
  client.
- The controller can configure or send an extra request such as "run
  legionella protection" with a target temperature and time window.
- The client checks the request against local limits, acknowledges or rejects
  it, and reports temperature, operating state and result.
- If the network connection is lost, the client continues using its local
  control and last valid safe settings.

Controller commands should express desired behavior, not bypass client-side
protections. Safety cut-offs, temperature limits and suitable scald protection
must work independently of the controller and network.

### Communication approach

ESP32 clients are intended to use C++. HTTPS/REST with Basic Authentication is
the initial approach for discovery, querying and client configuration. A
transport and message format for direct runtime links between clients still
needs to be selected. Depending on the requirements, options include direct
HTTP requests, WebSocket or MQTT. Decide whether each client should expose a
service directly or whether some clients should relay messages.

Direct client communication must be authenticated and restricted to permitted
commands. Clients should store controller-configured links locally. Safety
functions and safe states remain the responsibility of each client.

### First prototype: ESP32 relay client

The first client prototype targets a generic ESP32-WROOM-32 development
board. Node-RED on Venus OS reads `com.victronenergy.system/0` at
`/Dc/Battery/Voltage` and exposes the latest fresh sample through
`GET /homenodes/v1/battery-voltage`. The ESP32 polls this endpoint over HTTPS
every 60 seconds by default and makes the relay decision locally; MQTT is not
used in this initial design.

The Node-RED endpoint returns a JSON object with numeric `voltage`, integer
`inverterMode` and `updatedAt` (Unix milliseconds), or `503 Service
Unavailable` if either cached Victron value is missing or older than 120
seconds. Node-RED must provide HTTPS and HTTP Basic Authentication for its
HTTP In endpoint. The ESP32 trusts the configured server certificate/CA and
verifies the hostname; do not disable TLS verification. Configure the endpoint
URL, credentials and trusted PEM locally in `client/esp32_relay/secrets.h`;
never commit that file.

The ESP32 relay starts OFF and uses local hysteresis: ON at or above 53.8 V,
OFF at or below 52.1 V, retaining its previous state between thresholds.
Heater permission is disabled at startup and must be enabled through the
ESP32's authenticated HTTPS API; an `on` command only enables automatic
control and an `off` command always forces the relay off. The API reports the
relay state, battery voltage, inverter mode and local time. Heating is
permitted only from 06:00 through 23:59 local time (Europe/Berlin)
and only when the VE.Bus mode is `2` (Inverter Only). Any other mode, an
unsynchronized clock, Wi-Fi/HTTPS/authentication failure, stale data or an
invalid response switches the relay OFF. The ESP32 uses the Fritz!Box at
`192.168.178.1` as its only NTP source; no public NTP pool fallback is used.
The Fritz!Box must provide NTP service on the LAN. Voltage is an approximate
indicator rather than a direct SOC reading; validate the thresholds and
fail-safe behavior before enabling the heater.
The sketch retains GPIO 13 and active-HIGH relay logic. Verify this against
the exact board pinout and relay module before use.

The hardware photo shows an ESP32-WROOM-32 development board and a single
relay module with `VCC`, `IN1` and `GND` control labels. The first relay test
uses only these two boards; no sensor is needed. Confirm the relay module's
exact model, voltage requirements and trigger polarity before wiring or
applying a load.

#### ESP32-WROOM-32 development board pinout

Pin labels below are transcribed from the board photo. Orientation: hold the
board with the antenna at the top and the USB connector at the bottom. The
left and right columns are the pin rows as seen from the component side.
`VP` and `VN` are also known as GPIO36 and GPIO39. The board's `D0`–`D3`,
`CMD` and `CLK` pins are connected to the module's flash interface; do not use
them as general-purpose GPIO.

```text
                 Antenna
          ESP32-WROOM-32 module

       LEFT PIN ROW       RIGHT PIN ROW
       3V3                GND
       EN                 GPIO23
       VP / GPIO36        GPIO22
       VN / GPIO39        TX / GPIO1
       GPIO34             RX / GPIO3
       GPIO35             GPIO21
       GPIO32             GND
       GPIO33             GPIO19
       GPIO25             GPIO18
       GPIO26             GPIO5
       GPIO27             GPIO17
       GPIO14             GPIO16
       GPIO12             GPIO4
       GND                GPIO0
       GPIO13             GPIO2
       D2 / GPIO9*        GPIO15
       D3 / GPIO10*       D1 / GPIO8*
       CMD / GPIO11*      D0 / GPIO7*
       5V                 CLK / GPIO6*
              USB connector

       * Flash-interface pins: do not use for peripherals.
```

For the relay blink hardware test, the control-side wiring is:

```text
ESP32 3V3     -> relay VCC
ESP32 GND     -> relay GND
ESP32 GPIO13  -> relay IN1
```

This wiring assumes the relay board can be powered from 3.3 V and its input
accepts 3.3 V logic. Verify the actual relay module markings and switching
polarity. The pinout is specific to the photographed board variant; check the
silkscreen on the actual board before wiring.

## 5. Open questions

The following items are not yet specified by the source PDF or project notes:

- Exact hardware and Docker deployment for the controller computer
- Complete list of ESP32 models and their responsibilities
- Final API/transport protocol and message format
- Measurement points and sensors
- V2H / wallbox integration
- Boiler hardware, sensors and actuators
- Control of other loads
- Monitoring camera
- Daly BMS addressing and parallel-battery communication
- Exact Daly BMS protocols and interface pinouts
- Direct SOC reporting from the Daly BMS units
- Other charger models
- Electrical protection and shutdown functions
- Automation Wi-Fi segmentation and security details
- TLS certificate renewal/trust distribution and device-specific HTTPS Basic
  Auth credentials

## 6. Next steps

1. Check components and connections against the original Miro sketch.
2. Reconstruct unreadable or cropped areas as separate diagrams.
3. Document electrical power flows separately from digital communication
   flows.
4. Define a responsibility and API for each external module.
5. Record open assumptions and device specifications still to be collected.
