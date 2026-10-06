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

The first client prototype is based on the existing ESP32-CAM sketch. Camera
and PIR functionality are not used in this test. The ESP32 exposes HTTPS
endpoints and initially reports its identity, firmware version and a
controllable relay:

- `GET /api/v1/capabilities` — client ID, firmware version and capabilities
- `GET /api/v1/status` — current relay state
- `POST /api/v1/relay` — switch the relay using `{"state":"on"}` or
  `{"state":"off"}`

All endpoints use HTTPS and HTTP Basic Authentication. During development,
Wi-Fi credentials, device-specific authentication credentials and the TLS
certificate are configured locally in `client/esp32_relay/secrets.h`. That
file and the private key must not be committed. The client is reachable via
its mDNS address `<CLIENT_ID>.local` and advertises the HTTPS service on the
local network.

The sketch retains GPIO 13 and active-HIGH relay logic from the previous
prototype. Verify this against the actual board and relay module. On an
ESP32-CAM, GPIO 13 is also associated with the microSD interface.

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
- TLS certificates and device-specific HTTPS Basic Auth credentials

## 6. Next steps

1. Check components and connections against the original Miro sketch.
2. Reconstruct unreadable or cropped areas as separate diagrams.
3. Document electrical power flows separately from digital communication
   flows.
4. Define a responsibility and API for each external module.
5. Record open assumptions and device specifications still to be collected.
