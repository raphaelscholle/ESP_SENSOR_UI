# ESP Sensor UI

Arduino/PlatformIO firmware for the Waveshare ESP32-C3-Zero that exposes a rich web UI for configuring MQTT endpoints, the onboard WS2812B LED, and a Grove Sunlight sensor without writing code.

## Features
- Captive-style configuration: if Wi-Fi fails, the device creates an `ESP-Sensor-UI` access point for setup.
- Web UI served from the device for Wi-Fi, MQTT, LED, and I2C pin assignments.
- MQTT control for WS2812B with solid, rainbow, and breathe animations via topic `<base>/<wsTopic>/set`.
- Sunlight metrics (visible, IR, UV) published periodically to `<base>/<sunlightTopic>/*`.
- Preferences-backed configuration so settings persist across reboots.

## Building and Flashing
1. Install [PlatformIO](https://platformio.org/install) or use the VS Code extension.
2. Connect the ESP32-C3-Zero.
3. Run `pio run --target upload` to flash and `pio device monitor` to view logs.

## MQTT Control Examples
- `color:#FF00FF` sets a solid magenta.
- `rainbow` enables a flowing rainbow.
- `breathe:#4488FF` applies a pulsing blue.
- `off` turns the LED off.

## Pin Defaults
- WS2812B: pin 8, single pixel on-board.
- Grove Sunlight: SDA=6, SCL=7 (adjustable in UI).

## Topics
- Status: `<base>/status` (online message).
- LED control: `<base>/<wsTopic>/set`.
- Sunlight readings: `<base>/<sunlightTopic>/visible`, `/ir`, `/uv`.
