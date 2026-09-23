# Eyes of Hermes

Eyes of Hermes is an ESP32-C3 companion for Hermes. It connects to Wi-Fi,
opens a WebSocket connection to a Hermes relay or server, and reports the
agent's current state over the serial monitor.

python "C:\Users\chasb\AppData\Local\hermes\bot_relay\hermes_relay.py"
MUST BE RUNNING TO TALK WITH THIS PROGRAM   

## Current functionality

At startup, the firmware:

1. Starts the serial port at `115200` baud.
2. Connects to the Wi-Fi network configured in `lib/secrets.h`.
3. Connects to `10.133.1.100:8090/ws` using the Arduino WebSockets client.
4. Reconnects every five seconds if the WebSocket connection is lost.

When the WebSocket connects, the device sends this registration message:

```json
{"type":"test","device":"ESP32_Companion","status":"online"}
```

For incoming text messages, the firmware searches for the exact field prefix
`"state": "` and copies the following text up to the next quote. The state is
limited to 255 characters. Repeated states are not printed; a new state is
printed as:

```text
payload: <state>
```

The firmware currently recognizes these state values from the Hermes side:

- `idle` - The agent is at rest and waiting for input.
- `run` - A tool is executing or a turn is in progress.
- `review` - The model is thinking or processing context.
- `wave` - A turn completed successfully.
- `jump` - A plan or todo list completed successfully.
- `failed` - A tool execution or turn encountered an error.
- `waiting` - The agent is paused for approval or interaction.

Binary, ping, pong, error, connection, and disconnection events are logged to
the serial monitor but are not stored as Hermes state payloads.

## Project structure

```text
Eyes of Hermes/
├── platformio.ini       PlatformIO environment and dependencies
├── lib/
│   └── secrets.h        Local Wi-Fi credentials; keep this file private
├── src/
│   └── main.cpp         ESP32 firmware and WebSocket event handling
├── include/             Project header files, if needed later
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
pio device monitor --baud 115200
```

The PlatformIO environment also enables LittleFS support and USB CDC on boot.
The currently declared libraries are WebSockets, Adafruit SSD1306, Adafruit
GFX Library, and Adafruit BusIO.

## Current limitations

- The Wi-Fi connection blocks in `setup()` until it succeeds.
- The WebSocket state parser expects the exact spacing in `"state": "`.
- Only text payloads are parsed; binary payloads are ignored.
- The SSD1306 display libraries are configured as dependencies but are not
	currently used by `src/main.cpp`.
- There are no automated PlatformIO tests yet.