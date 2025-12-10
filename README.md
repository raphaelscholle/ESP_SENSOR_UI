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
   - If you previously saw `UnknownPackageError` for `ESP Async WebServer` on Windows, pull the latest code: dependencies now use
     the ESPHome-maintained forks fetched directly from GitHub so they install cleanly across platforms.

### Windows setup tip (Python/UV error)
If PlatformIO on Windows shows `Could not find UV-managed Python` or similar while installing dependencies, point PlatformIO to your installed Python 3.13 (or newer):

1. Install the official **Python 3.13.x** from [python.org](https://www.python.org/downloads/windows/) and check **"Add python.exe to PATH"**.
2. Open a new terminal **before launching VS Code** and set PlatformIO to use your native Python instead of UV:
   - PowerShell: `setx PIO_USE_NATIVE_PYTHON 1`
   - Command Prompt: `set PIO_USE_NATIVE_PYTHON=1` (for the current session)
3. In the same terminal, set PlatformIO's Python executable to your installation path (replace the example path with yours):
   - PowerShell: `setx PLATFORMIO_PYTHON_EXE "C:\\Users\\<you>\\AppData\\Local\\Programs\\Python\\Python313\\python.exe"`
   - Command Prompt: `setx PLATFORMIO_PYTHON_EXE "C:\\Users\\<you>\\AppData\\Local\\Programs\\Python\\Python313\\python.exe"`
4. Restart VS Code, open the project, and let PlatformIO reuse the installed Python. Confirm via `PlatformIO > Core CLI > pio system info` (the `Python` entry should point to your installation).
   - Optional: run `scripts/set_platformio_python.ps1` in PowerShell to set these environment variables automatically.

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
