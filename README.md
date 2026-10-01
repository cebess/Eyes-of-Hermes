# Eyes of Hermes

Eyes of Hermes is an ESP32-C3 companion for Hermes. It connects to Wi-Fi,
opens a WebSocket connection to a Hermes relay or server, and drives an
SSD1306 OLED "eyes" display with an animation that matches the agent's
current state.

python "C:\Users\chasb\AppData\Local\hermes\bot_relay\hermes_relay.py"
MUST BE RUNNING TO TALK WITH THIS PROGRAM   

## Current functionality

At startup, the firmware:

1. Starts the serial port at `115200` baud.
2. Connects to the Wi-Fi network configured in `lib/secrets.h`.
3. Connects to `10.133.1.100:8090/ws` using the Arduino WebSockets client.
4. Reconnects every five seconds if the WebSocket connection is lost.
5. Initializes the SSD1306 display and LittleFS filesystem
   (`EyeDisplay::begin`), sets the state to `asleep`, and plays a random
   `asleep` animation.

When the WebSocket connects, the device sends this registration message:

```json
{"type":"test","device":"ESP32_Companion","status":"online"}
```

For incoming text messages, the firmware searches for the exact field prefix
`"state": "` and copies the following text up to the next quote into a
256-byte payload buffer. On disconnect the state is forced back to `asleep`;
on connect it is forced to `awake`.

The main loop compares the current state to the last one it displayed:

- If the state changed, it picks a random subfolder under `/<state>` on
  LittleFS (via `EyeDisplay::getRandomImageFolder`) and starts playing that
  folder's BMP frames on a background FreeRTOS task
  (`EyeDisplay::drawFrames`).
- If the state is unchanged but no animation is currently playing, and the
  `/<state>` folder has more than one variation (`EyeDisplay::countOfSubFolders`
  > 1), it picks another random variation and plays it again, so an idle
  state keeps cycling through its different animations.

The firmware currently recognizes these state values from the Hermes side,
each mapped to a top-level folder under `data/`:

- `idle` - The agent is at rest and waiting for input.
- `run` - A tool is executing or a turn is in progress.
- `review` - The model is thinking or processing context.
- `wave` - A turn completed successfully.
- `jump` - A plan or todo list completed successfully.
- `failed` - A tool execution or turn encountered an error.
- `waiting` - The agent is paused for approval or interaction.
- `asleep` - The WebSocket connection to the Hermes server is lost.
- `awake` - The WebSocket connection to the Hermes server is (re)established.

Each state folder contains one or more named subfolders (variations), and
each variation folder contains numbered BMP frames that are sorted and
played in sequence with a fixed delay between frames.

Binary, ping, pong, and error WebSocket events are logged to the serial
monitor but are not stored as Hermes state payloads.

## Project structure

```text
Eyes of Hermes/
├── platformio.ini       PlatformIO environment and dependencies
├── lib/
│   └── secrets.h        Local Wi-Fi credentials; keep this file private
├── include/
│   └── EyeDisplay.h     EyeDisplay namespace API (display + animation)
├── src/
│   ├── main.cpp         ESP32 firmware and WebSocket event handling
│   └── EyeDisplay.cpp   SSD1306/LittleFS animation playback logic
├── data/                LittleFS image data, one folder per state, each
│                        with subfolders of BMP animation frames
└── test/                PlatformIO test directory; no tests currently exist
```

## Configuration

Update the connection constants near the top of `src/main.cpp` when the
Hermes relay is hosted elsewhere:

```cpp
const char* websocket_server = "10.133.1.100";
const int websocket_port = 8090;
const char* websocket_path = "/ws";
```

The OLED is wired to the ESP32-C3 default I2C pins declared in `src/main.cpp`
(`I2C_SDA`, `I2C_SCL`) at address `SCREEN_ADDRESS` (default `0x3C`).

Create or update `lib/secrets.h` with the Wi-Fi credentials expected by the
firmware:

```cpp
const char* ssid = "your-wifi-name";
const char* password = "your-wifi-password";
```

Do not commit real credentials to source control.

## Build and upload

This project targets the `esp32-c3-devkitc-02` board with the Arduino
framework. From the project directory, use:

```sh
pio run
pio run --target upload
pio run --target uploadfs   # upload the data/ folder contents to LittleFS
pio device monitor --baud 115200
```

The PlatformIO environment also enables LittleFS support and USB CDC on boot.
The currently declared libraries are WebSockets, Adafruit SSD1306, Adafruit
GFX Library, and Adafruit BusIO.

## Current limitations

- The Wi-Fi connection blocks in `setup()` until it succeeds.
- The WebSocket state parser expects the exact spacing in `"state": "`.
- Only text payloads are parsed; binary payloads are ignored.
- There are no automated PlatformIO tests yet.

## Wiring

The SSD1306 communicates over I2C. Connect it to the ESP32-C3 as follows:

| SSD1306 Pin | ESP32-C3 Pin | Notes                  |
|-------------|--------------|------------------------|
| VCC         | 3V3          | Power (3.3V)           |
| GND         | GND          | Ground                 |
| SCL         | GPIO9        | I2C clock              |
| SDA         | GPIO8        | I2C data               |

```
   ESP32-C3                     SSD1306 OLED
  +---------+                  +-------------+
  |     3V3 |----------------->| VCC         |
  |     GND |----------------->| GND         |
  |   GPIO9 |----------------->| SCL         |
  |   GPIO8 |----------------->| SDA         |
  +---------+                  +-------------+