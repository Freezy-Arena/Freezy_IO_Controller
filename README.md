# Freezy IO Controller

> [!WARNING]
> **This is a test branch of Freezy IO Controller for ESP32 arena devices.** `FMS_TABLE` requires the Freezy Arena WebSocket input/coil and HTTP start extensions documented below. Do not assume upstream [Team 254's Cheesy Arena](https://github.com/Team254/cheesy-arena) supports those endpoints or the `setRegisters`/`setInput` extensions.

**Freezy IO Controller** replaces the separate **Freezy EStops application** with a single ESP32-S3 firmware application for Freezy Arena hub, FMS table, and alliance stop controllers. Use the built-in web interface to assign each device its role and responsibilities, configure its network and arena connection, and save its settings. The same firmware supports all five roles; changing a device's role does not require flashing a different application.

Depending on the selected role, the controller reads hub counters, reports PLC registers and inputs, handles field or alliance stop buttons, requests match starts, controls relays, and drives LEDs. Hardware wiring and active inputs/outputs depend on the assigned role; see [Device roles](#device-roles) and the linked wiring guides.

## Highlights

- Replaces Freezy EStops and combines hub, FMS table, and alliance stop responsibilities in one application
- Assign each device's role through its web interface: Red Hub, Blue Hub, FMS Table, Red Alliance, or Blue Alliance
- Migrates legacy REST API calls and polling to a persistent WebSocket connection for stop inputs and stack-light updates; match start still uses HTTP
- Four overflow-safe ESP32 PCNT counter channels with 64-bit accumulation
- Red Hub and Blue Hub roles with separate PLC mappings
- FMS_TABLE role with dedicated stop sampling/delivery and legacy settings migration; see [protocol comparison and bench tests](docs/FMS_TABLE.md)
- RED_ALLIANCE and BLUE_ALLIANCE roles for three E-stops and three A-stops each, with retained transitions and acknowledged delivery; see [alliance stop wiring and behavior](docs/ALLIANCE_STOPS.md)
- W5500 wired Ethernet with DHCP or static addressing
- Arena WebSocket client with automatic reconnect
- Form login and `session_token` cookie authentication for protected arena pages
- Persistent arena username, password, network, role, message, and LED settings
- Coil, direct DMX, and WebSocket LED control
- Locally rendered solid, pulse, startup, advantage, rainbow, and side-test animations
- Browser-based configuration stored in ESP32 Preferences/NVS

## Hardware

| Component | Configuration |
|---|---|
| MCU | ESP32-S3 DevKitM-1 |
| Ethernet | W5500 SPI module |
| LED strip | WS2812B, GPIO 38, up to 300 LEDs |
| Default LED setup | 125 LEDs, BRG color order |
| Relay outputs | GPIO 34 and GPIO 35 |
| Counter inputs | GPIO 15, 1, 2, and 3 |

### W5500 wiring

| Signal | GPIO |
|---|---:|
| SCK | 13 |
| MISO | 12 |
| MOSI | 11 |
| CS | 14 |
| IRQ | 10 |
| RST | 9 |

## Web configuration

After boot, the serial console prints the assigned IP address. Open `http://<device-ip>` in a browser.

![Main device configuration dashboard](docs/images/config-dashboard.svg)

The main page configures:

- DHCP or a static IP address and gateway
- Arena server IP and port, shared by WebSocket and FMS Table HTTP
- Device role: `redHub`, `blueHub`, `FMS_TABLE`, `RED_ALLIANCE`, or `BLUE_ALLIANCE`
- Links to the WebSocket and LED settings pages

Saving the main page restarts the controller.

### Firmware updates over Ethernet

Open **Firmware Update** on the main page, or `http://<device-ip>/update`.
The page provides a `.bin` file picker, **Upload and Update** button, progress,
and a validation result. Updates use the existing W5500 Ethernet connection;
Wi-Fi and a separate OTA service are not required.

1. Install this OTA-capable firmware once over USB: `pio run --target upload`.
   The explicit `default_8MB.csv` layout matches this project's board default:
   two `0x330000`-byte (3,342,336-byte) application slots plus OTA metadata.
   A device with a different partition layout needs USB flashing first.
2. For subsequent releases, run `pio run` and locate
   `.pio/build/esp32-s3-devkitm-1/firmware.bin`.
3. Take the arena out of service before updating. Flash writes and reboot can
   interrupt input sampling, stop delivery, and outputs. Keep power and Ethernet
   connected throughout the update.
4. Open **Firmware Update**, select that application's `firmware.bin`, and
   click **Upload and Update**. Do not upload `bootloader.bin`, `partitions.bin`,
   filesystem images, or merged flash images.
5. Wait for the installed/rebooting message. After about 15 seconds, return to
   Device Configuration and check the controller's role, connection, and inputs
   before returning the arena to service.

The server streams into the inactive application slot, checks size and ESP image
validity, and selects it for boot only after the complete upload validates.
Disconnected uploads are aborted; validation failures keep the current boot
selection. Preferences/NVS settings are retained. This does not provide automatic
rollback if a valid image boots but has application bugs; keep USB recovery available.
Future firmware must retain the updater to allow further browser updates.

The device web interface, including this updater, has no login or HTTPS. Anyone
who can reach it can install firmware; use a trusted, isolated maintenance network.
The page token prevents ordinary cross-origin browser submissions but is not user
authentication or a firmware signature. ESP image validation cannot establish
that a binary is a trusted Freezy IO Controller release.

Choose the role that matches the device's wiring and job:

| Role | Device responsibility |
|---|---|
| `redHub` | Red hub counters, sensor inputs, relays, and LEDs |
| `blueHub` | Blue hub counters, sensor inputs, relays, and LEDs |
| `FMS_TABLE` | Field E-stop, match-start button, and field stack lights |
| `RED_ALLIANCE` | Red alliance station E-stop and A-stop inputs, plus status LEDs |
| `BLUE_ALLIANCE` | Blue alliance station E-stop and A-stop inputs, plus status LEDs |

### Live input status

For `FMS_TABLE`, `RED_ALLIANCE`, and `BLUE_ALLIANCE`, select **View Input Status**
on the configuration page, or open `http://<device-ip>/inputs` after the role has
been saved and the controller rebooted.

- FMS Table shows **Field E-stop** and **Start button**.
- Alliance roles show each station's **E-stop** and **A-stop** (six inputs).
- Red means a stop is pressed; green means start is pressed; gray means released.
  Each indicator also has a text label. Unavailable readings use a dashed indicator.

The page polls local sampled inputs every 500 ms and reports stale readings or
connection loss. It also shows a latched controller fault. This is a read-only
view of physical inputs, not confirmation of arena delivery or match state.
The JSON endpoint is `/api/inputs`; both routes are unavailable for hub roles.

### WebSocket settings

Open **Configure WebSocket Messages** from the main page.

![WebSocket credentials and message controls](docs/images/websocket-settings.svg)

| Option | Default | Purpose |
|---|---|---|
| Arena username | `admin` | Username posted to the arena `/login` endpoint |
| Arena password | `password` | Password posted to the arena `/login` endpoint |
| Send Registers | Enabled | Sends `setRegisters` counter updates |
| Send Inputs | Enabled | Sends hub `setInput` sensor updates; stop-role delivery always stays enabled |

When the protected field-testing WebSocket responds with HTTP 307 or 401, the controller:

1. Posts the saved username and password to `/login`.
2. Extracts the returned `session_token` cookie.
3. Adds the cookie to the next WebSocket upgrade request.
4. Reconnects using the normal retry cycle.

Credentials are stored as plain strings in ESP32 Preferences. Saving this page clears the cached session so updated credentials are used the next time authentication is required.

### LED settings

Open **Configure LED Settings** from the main page.

![LED mode, count, and color-order settings](docs/images/led-settings.svg)

| Option | Choices |
|---|---|
| Control Mode | Coil, DMX Direct, or DMX WebSocket |
| Number of LEDs | 1–300; default 125 |
| RGB Color Order | RGB, RBG, GRB, GBR, BRG, or BGR; default BRG |

FastLED selects the RGB color order during initialization. The LED page therefore warns the user and performs a **Save & Reboot** so all changes take effect.

## LED control modes

| Mode | Behavior |
|---|---|
| Coil | PLC light coils select the role color; field reset displays green |
| DMX Direct | The ESP32 renders incoming DMX LED data directly |
| DMX WebSocket | `setLedMode.RedMode` and `setLedMode.BlueMode` select locally rendered animations |

### Arena animation modes

The integer values mirror `led/mode.go` in the arena project.

| Value | Mode | ESP32 behavior |
|---:|---|---|
| 0 | Off | All LEDs off |
| 1 | Red | Solid red |
| 2 | Blue | Solid blue |
| 3 | Green | Solid green / field safe |
| 4 | Purple | Solid purple / field cleanup |
| 5 | White | Solid white / scoring assessment |
| 6 | Red Pulse | Pulsing red |
| 7 | Blue Pulse | Pulsing blue |
| 8 | Red Startup | Red startup fill sequence |
| 9 | Blue Startup | Blue startup fill sequence |
| 10 | Red Advantage | Red base with white fixture sweeps |
| 11 | Blue Advantage | Blue base with white fixture sweeps |
| 12 | Rainbow | Rotating 32-position arena rainbow, scaled to the configured strip |
| 13 | Side 1 Test | First quarter in the hub alliance color |
| 14 | Side 2 Test | Second quarter in the hub alliance color |
| 15 | Side 3 Test | Third quarter in the hub alliance color |
| 16 | Side 4 Test | Fourth quarter in the hub alliance color |

For side-test modes, the configured LED count is divided into four contiguous sections. Any remainder is safely included through proportional section boundaries.

## Device roles

| Role | Channel registers | Total | Motor coil | Light coil |
|---|---|---|---|---|
| `redHub` | 3, 4, 5, 6 | 1 | `COIL_RED_HUB_MOTOR` | `COIL_RED_HUB_LIGHT` |
| `blueHub` | 7, 8, 9, 10 | 2 | `COIL_BLUE_HUB_MOTOR` | `COIL_BLUE_HUB_LIGHT` |

Role-specific registers, coils, inputs, counters, and relay pins are defined in `src/role_config.h`.

`FMS_TABLE` uses GPIO 33 for field stop (acknowledged WebSocket `setInput`, channel 0), GPIO 34 for start,
and GPIO 47 for its original stack-light layout, driven by WebSocket PLC coil changes. It does not initialize hub
relays/counters or send hub telemetry. See [FMS_TABLE migration](docs/FMS_TABLE.md)
for polarity, retained transitions, retries, overflow, settings, and bench tests.
It requires the inspected Freezy Arena WebSocket and HTTP extensions; upstream compatibility
must not be inferred from the hub LED support.

`RED_ALLIANCE` and `BLUE_ALLIANCE` use GPIOs 1, 2, 3, 15, 18, and 16 for
station 1 E/A, station 2 E/A, and station 3 E/A stops. Red writes channels 1–6;
blue writes channels 7–12. Both use the original GPIO 47 status LEDs and
dedicated input sampling, with independent transition queues and acknowledged
WebSocket delivery. See [alliance stop roles](docs/ALLIANCE_STOPS.md) for polarity,
retry/fault behavior, settings migration, and outstanding hardware validation.

## Migration from REST APIs to WebSockets

Freezy IO Controller replaces many of the older Freezy EStops methods that used REST APIs with messages over the arena WebSocket connection. Stop inputs now use acknowledged WebSocket writes, and stack lights follow incoming PLC coil updates instead of repeatedly polling an HTTP endpoint.

| Function | Current communication with Freezy Arena |
|---|---|
| Field and alliance stop inputs | WebSocket `setInput` with acknowledgment; replaces HTTP POST `/api/freezy/eStopState` |
| FMS table stack lights | WebSocket `plcIoChange.Coils`; replaces HTTP GET `/api/freezy/field_stack_light` polling |
| Hub counters and sensor inputs | WebSocket `setRegisters` and `setInput` |
| PLC coil updates for relays and lights | WebSocket `plcIoChange` |
| Arena LED animation modes | WebSocket `setLedMode` |
| FMS table match-start request | Still HTTP POST `/api/freezy/startMatch` |

The migration is not complete: match start still requires the Freezy Arena HTTP API, and an HTTP POST to `/login` remains available for session authentication when a non-stop role encounters a protected WebSocket endpoint. Stop roles use the public PLC WebSocket endpoint without this login fallback. HTTP and WebSocket connections share the saved arena host and port.

The controller also serves its own web configuration pages and local `/api/inputs` status endpoint over HTTP. These are device-local interfaces, separate from the Freezy Arena REST APIs being replaced. See [FMS table protocol and migration details](docs/FMS_TABLE.md) for the arena endpoint requirements.

## WebSocket protocol

The default endpoint is:

```text
ws://10.0.100.5:8080/api/plc/websocket
```

### Outbound register update

Counter values are sent every 500 ms when register sending is enabled.

```json
{
  "type": "setRegisters",
  "data": [
    { "register": 3, "cValue": 42 },
    { "register": 1, "cValue": 42 }
  ]
}
```

### Outbound input update

```json
{
  "type": "setInput",
  "data": [
    { "channel": 0, "state": true }
  ]
}
```

### Inbound coil change

```json
{
  "type": "plcIoChange",
  "data": {
    "Coils": [false, true, false, false]
  }
}
```

### Inbound LED mode

`RedMode` and `BlueMode` are the only data fields. Both are required and must be in the range 0–16.

```json
{
  "type": "setLedMode",
  "data": {
    "RedMode": 12,
    "BlueMode": 3
  }
}
```

## Persistent settings

| Preferences namespace | Stored settings |
|---|---|
| `network` | DHCP/static configuration and LED control mode |
| `role` | Hub, FMS Table, or alliance stop role |
| `websocket` | Arena host/port, username/password, and message toggles |
| `leds` | LED count and RGB color order |

## Project structure

```text
src/
├── main.cpp                       Setup, loop, and callbacks
├── role_config.h                  Role-specific mappings and pins
├── counter/                       PCNT hardware counters
├── relay/                         GPIO relay outputs
├── network/                       W5500 Ethernet and network preferences
├── led/                           FastLED strip manager and preferences
├── led_animator/                  Arena-compatible local animations
├── dmx_led/                       Direct DMX LED rendering
├── webserver/                     Device configuration pages
└── websocket/
    ├── ws_manager.cpp             WebSocket, login, cookie, and JSON handling
    ├── coil_map.h                 Seasonal PLC coil assignments
    ├── register_map.h             Seasonal PLC register assignments
    └── input_map.h                Seasonal PLC input assignments
```

## Web templates and static assets

Web UI source files live at the repository root:

```text
template/
  config.html           Main configuration page
  websocket.html        Arena credentials and message settings
  led.html              Hub LED settings
  inputs.html           Live input status
  update.html           Firmware upload page
  allianceLeds.html      Alliance LED information
  fmsLeds.html           FMS Table LED information
  reboot.html            Shared reboot page
  titleBar.html          Shared branding and version partial
static/
  css/                  Matching pageName.css files and common.css
  js/                   Matching pageName.js files
tools/embed_web_assets.py
```

Edit each page's HTML, CSS, and JavaScript directly. Pages with no script behavior
still have a matching `.js` file ready for future changes. `common.css` styles the
shared title bar. The existing page URLs and form POST endpoints are unchanged.

`pio run` automatically embeds all templates and static files in `firmware.bin`
using a [PlatformIO pre-build script](https://docs.platformio.org/en/latest/scripting/launch_types.html).
Upload that binary normally over USB or through the firmware update page. No
filesystem upload (`uploadfs`), SD card, or second update is needed. Generated C++
data lives under `.pio/build/<environment>/generated/`; do not edit it.

Templates use `{{name}}` placeholders. C++ page handlers in
`src/webserver/web_manager.cpp` pass values to `WebAssets::render`; OTA page values
are in `firmware_update.cpp`. Values are HTML-escaped and should only be used in
text or quoted attributes, not JavaScript or CSS. The built-in `{{titleBar}}`
inserts `template/titleBar.html`, which uses `{{version}}` from the firmware header.
The OTA token is provided through an HTML meta element read by `update.js`.
This is a small placeholder renderer, not the Freezy Arena template engine.

To add a page, create its three files, link `/static/css/pageName.css` and
`/static/js/pageName.js` in its template, and add a C++ route that renders
`WebAssets::render("pageName", {...})`. Templates are not directly public URLs.
To add an image, font, or other static object, place it under `static/` and refer
to its matching URL, such as `/static/images/logo.svg`. The build discovers new
static files recursively and serves their original bytes with the appropriate
content type (unknown types use `application/octet-stream`). Dotfiles are excluded.
Everything under `static/` is public: do not place credentials or private files there.
Assets count toward the OTA application slot size. Static responses disable caching
so browsers do not retain obsolete CSS or JavaScript after a firmware update.

## Seasonal updates

Review these files when the arena PLC mapping changes:

| File | Contents |
|---|---|
| `src/websocket/coil_map.h` | Coil indices |
| `src/websocket/register_map.h` | Register indices |
| `src/websocket/input_map.h` | Input indices |
| `src/role_config.h` | Role mappings and physical pins |
| `src/led_animator/led_animator.h` | Arena LED mode integer definitions |

## Build, upload, and monitor

### Firmware version

The web pages show a shared **Freexy IO Contoller** title bar and firmware version
badge. The version is compiled into the firmware from `src/firmware_version.h`.
It describes the running image, so OTA updates also update the
badge. Do not store this value in Preferences/NVS: an old saved value could survive
an update and misidentify the installed firmware.

Use `MAJOR.MINOR.PATCH`: increment PATCH for fixes, MINOR for compatible features,
and MAJOR for breaking configuration or protocol changes. Before publishing a
release, update the header, build, and tag the corresponding Git commit with the
same version (for example, `v0.1.0`). Archive its binary as
`freezy-io-controller-0.1.0.bin`. Development builds can use a suffix such as
`0.2.0-dev`; avoid automatically changing the release version on every build.

### PlatformIO commands

Freezy IO Controller uses [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Upload
pio run --target upload

# Serial monitor
pio device monitor --baud 115200
```

Key dependencies are declared in `platformio.ini`: FastLED, ESPAsyncWebServer, AsyncTCP, ArduinoJson, WebSockets, and Ethernet. The ESP32 framework supplies HTTPClient for arena form authentication.

## Debug output

Set `DEBUG_SERIAL` in `src/main.cpp` to enable or suppress verbose WebSocket logging:

```cpp
#define DEBUG_SERIAL true
```

Connection, authentication, LED mode, settings, and periodic status messages that use direct `Serial` calls remain visible.

## License

MIT
