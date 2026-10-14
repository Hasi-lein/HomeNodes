# ESP32 relay blink test

This standalone PlatformIO project tests the ESP32 and relay module only. It
does not use Wi-Fi, HTTPS, an API or any sensor. The relay starts OFF, then
alternates ON and OFF every 500 ms.

## Hardware

Connect only the relay module's logic side:

| ESP32-WROOM-32 | Relay module |
| --- | --- |
| 3V3 | VCC |
| GND | GND |
| GPIO 13 | IN1 |

This assumes the relay module accepts 3.3 V logic and is active HIGH. Check
the board/module markings and verify the actual behavior. If the relay is
active LOW, change `RELAY_ON_LEVEL` to `LOW` and `RELAY_OFF_LEVEL` to `HIGH`
in `src/main.cpp`.

See the [documented ESP32-WROOM-32 board pinout](../../Doku/miro_import.md#esp32-wroom-32-development-board-pinout)
for the photographed board's pin-row layout and flash-pin warning.

Do not connect the infrared heater or any mains load during this test. Observe
the relay indicator and listen for switching while the ESP32 is powered over
USB. Disconnect USB immediately if the board or relay module heats up, smells
or behaves unexpectedly.

## Build and upload

Install the VS Code **PlatformIO IDE** extension (`platformio.platformio-ide`)
if it is not already installed. Open this directory as a PlatformIO project.
Select the `esp32dev` environment, then use PlatformIO **Build**, **Upload**
and **Monitor**. PlatformIO downloads the board toolchain on the first build.
The serial monitor runs at 115200 baud and prints each relay state.
