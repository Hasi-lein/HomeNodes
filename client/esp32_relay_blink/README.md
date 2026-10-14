# ESP32 relay blink test

This standalone PlatformIO project tests the ESP32 and relay module only. It
does not use Wi-Fi, HTTPS, an API or any sensor. The relay starts OFF, then
alternates ON and OFF every 500 ms. The onboard LED on GPIO 2 follows the
same state.

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

The sketch assumes the onboard LED is connected to GPIO 2 and is active HIGH.
If the LED does not blink, verify the board pinout; some boards use a
different LED pin or active-LOW logic. Adjust `LED_PIN`, `LED_ON_LEVEL` and
`LED_OFF_LEVEL` in `src/main.cpp` as needed.

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
