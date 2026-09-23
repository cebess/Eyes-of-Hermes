// hermes ESP32 companion code
// Connects the ESP32 to WiFi and establishes a WebSocket connection with the Hermes server.
// runs on an ESP32 mini C microcontroller.
#include <Arduino.h> 
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "secrets.h"
#include <cstring>
#include "EyeDisplay.h"

// ESP32-C3 default I2C pins (adjust to match your wiring)
#define I2C_SDA 8
#define I2C_SCL 9

#define SCREEN_ADDRESS 0x3C

// Point this to your PC running Hermes (or your Hermes Relay endpoint)
const char* websocket_server = "10.133.1.100"; 
const int websocket_port = 8090; // my custom port
const char* websocket_path = "/ws";

WebSocketsClient webSocket;
constexpr size_t HERMES_PAYLOAD_CAPACITY = 256;
char hermesPayload[HERMES_PAYLOAD_CAPACITY] = {};

const char* getHermesPayload() {
    return hermesPayload;
}

String last_payload;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

    Serial.printf("[WS EVENT] type=%d length=%u\n",
                  type,
                  (unsigned)length);

    switch(type) {

        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            strncpy(hermesPayload, "asleep", HERMES_PAYLOAD_CAPACITY - 1);
            hermesPayload[HERMES_PAYLOAD_CAPACITY - 1] = '\0';
            break;

        case WStype_CONNECTED:
            Serial.print("[WSc] Connected to url: ");
            Serial.write(payload, length);
            Serial.println();
            strncpy(hermesPayload, "awake", HERMES_PAYLOAD_CAPACITY - 1);
            hermesPayload[HERMES_PAYLOAD_CAPACITY - 1] = '\0';
            webSocket.sendTXT(
                "{\"type\":\"test\",\"device\":\"ESP32_Companion\",\"status\":\"online\"}"
            );

            Serial.println("[WSc] Test registration sent");
            break;

        case WStype_TEXT:
            Serial.print("[WSc] TEXT: ");
            Serial.write(payload, length);
            Serial.println();
            {
                constexpr char stateMarker[] = "\"state\": \"";
                constexpr size_t stateMarkerLength = sizeof(stateMarker) - 1;
                size_t stateStart = length;

                for (size_t index = 0;
                     index + stateMarkerLength <= length;
                     ++index) {
                    if (memcmp(payload + index,
                               stateMarker,
                               stateMarkerLength) == 0) {
                        stateStart = index + stateMarkerLength;
                        break;
                    }
                }

                if (stateStart < length) {
                    size_t stateLength = 0;
                    while (stateStart + stateLength < length &&
                           payload[stateStart + stateLength] != '"' &&
                           stateLength < HERMES_PAYLOAD_CAPACITY - 1) {
                        ++stateLength;
                    }

                    memcpy(hermesPayload, payload + stateStart, stateLength);
                    hermesPayload[stateLength] = '\0';
                } else {
                    hermesPayload[0] = '\0';
                }
            }
            break;

        case WStype_BIN:
            Serial.printf("[WSc] BINARY message, %u bytes\n",
                          (unsigned)length);
            break;

        case WStype_ERROR:
            Serial.println("[WSc] ERROR");
            break;

        case WStype_PING:
            Serial.println("[WSc] PING");
            break;

        case WStype_PONG:
            Serial.println("[WSc] PONG");
            break;

        default:
            Serial.println("[WSc] Other WebSocket event");
            break;
    }
}

int networkRetry =0;
int maxNetworkRetry = 20;
void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        networkRetry++;
        if (networkRetry >= maxNetworkRetry) {
            Serial.println("\nFailed to connect to WiFi. Program stopped");
            while (true) {
                delay(1000); // go into a loop
            }
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected.");

    // Configure WebSocket client connection
    webSocket.begin(websocket_server, websocket_port, websocket_path);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);

    if (!EyeDisplay::begin(I2C_SDA, I2C_SCL, SCREEN_ADDRESS)) {
        while (true) {
            delay(1000);
        }
    }
    strncpy(hermesPayload, "asleep", HERMES_PAYLOAD_CAPACITY - 1);
    hermesPayload[HERMES_PAYLOAD_CAPACITY - 1] = '\0';
    const char *movement = EyeDisplay::getRandomImageFolder(String("/asleep"));
    if (movement) {
        EyeDisplay::drawFrames(movement);
    }
}

void loop() {
    webSocket.loop();
    const char* currentPayload = getHermesPayload();
    // there are known payloads:
    // idle: Triggered when the agent is completely at rest and waiting for your input.
    // run: Triggered while a tool is actively executing or a turn is currently in-flight.
    // review: Triggered when the language model is actively thinking, reasoning, or processing input context.
    // wave: Triggered when a single turn finishes cleanly and successfully.
    // jump: Triggered when a complete multi-step plan or all todo items have finished successfully, signaling a celebration.
    // failed: Triggered immediately when a tool execution or a turn encounters an error.
    // waiting: Triggered when the agent is blocked and paused, waiting for explicit user approval or interaction prompts.
    // I have a few special driven by network activity
    // asleep: triggered by loss of network connection to the WebSocket server.
    // awake: triggered when the network connection to the WebSocket server is restored.
    if (last_payload != currentPayload) {
        Serial.print("payload: ");
        Serial.println(currentPayload);
        last_payload = currentPayload;
        String folderPath = "/" + String(currentPayload);
        Serial.println("going to access: " + folderPath);
        const char *movement = EyeDisplay::getRandomImageFolder(folderPath);
        EyeDisplay::drawFrames(movement);
    } else {
        bool drawing = EyeDisplay::isDrawingFrames();
        if (!drawing && currentPayload != nullptr && currentPayload[0] != '\0') {
            String folderPath = "/" + String(currentPayload);
            if (EyeDisplay::countOfSubFolders(folderPath) > 1) {
                // we have something new we can display
                //Serial.println("going to access again: '" + folderPath + "'");
                const char *movement = EyeDisplay::getRandomImageFolder(folderPath);
                EyeDisplay::drawFrames(movement);
            
            }
        }
    }
    delay(100); // Small delay to avoid overwhelming the loop
}