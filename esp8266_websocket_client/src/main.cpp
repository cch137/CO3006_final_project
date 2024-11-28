#include <Arduino.h>
#include "ESP8266WiFi.h"
#include <WebSocketsClient.h>

// GPIO pins
#define GPIO1_PIN 1
#define GPIO2_PIN 2

// WiFi settings
#define WIFI_SSID "9G"
#define WIFI_PASSWORD "chee8888"
#define WIFI_MAX_RETRY_TIME_MS 10000

#define WS_HOST "140.115.200.43"
#define WS_PORT 80
#define WS_URL "/jet-d/ncu/CO3006/conn"
#define WS_PING_INTERVAL_MS 10000
#define WS_PONG_TIMEOUT_MS 2000
#define WS_DISCONNECT_TIMEOUT_COUNT 5

#define HEADER_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define HEADER_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define HEADER_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define HEADER_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define HEADER_ESP8266_LOG_MESSAGE_TO_ARDUINO (uint8_t)120
#define HEADER_SERVER_DEBUG_ESP8266_RESET (uint8_t)121
#define HEADER_SERVER_DEBUG_ESP8266_RESTART (uint8_t)122
#define HEADER_SERVER_DEBUG_ESP8266_DISCONNECT_WS (uint8_t)123
#define EOP (uint8_t)0x00

#define CONFIG_PAYLOAD_LENGTH 16

// Function prototypes
void websocket_event(WStype_t type, uint8_t *payload, size_t length);
void log_message(const char *message);
void maintain_wifi();
void maintain_ws();
void handle_ws_payload(uint8_t *payload, size_t length);

// WebSocket client instance
WebSocketsClient ws;

// Timers
unsigned long send_interval_ms = 1000;

// WebSocket connection flag
bool ws_connecting = false;
bool ws_connected = false;

// Variables for configuration
uint32_t V_offset = 250;
uint32_t L = 30;
uint32_t U = 70;
uint32_t I = 1000;

void maintain_wifi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start_attempt_time = millis();
  uint8_t connecting_count = UINT8_MAX - 1;

  // Try connecting to WiFi until timeout
  while (WiFi.status() != WL_CONNECTED && millis() - start_attempt_time < WIFI_MAX_RETRY_TIME_MS)
  {
    delay(10);
    if (++connecting_count == UINT8_MAX)
    {
      log_message("Connecting to WiFi...");
      connecting_count = 0;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    log_message("Connected to WiFi, local IP:");
    log_message(WiFi.localIP().toString().c_str());

    // Setup WebSocket connection when WiFi is connected
    maintain_ws();
  }
  else
  {
    log_message("Failed to connect to WiFi");
  }
}

void maintain_ws()
{
  if (ws_connecting)
    return;

  log_message("Connecting WebSocket...");
  ws.begin(F(WS_HOST), WS_PORT, F(WS_URL));
  ws.enableHeartbeat(WS_PING_INTERVAL_MS, WS_PONG_TIMEOUT_MS, WS_DISCONNECT_TIMEOUT_COUNT);

  // Set custom header for WebSocket
  ws.setExtraHeaders("CO3006-Sensor-Name: sensor-01\r\nCO3006-Auth: key-16888888");

  ws.onEvent(websocket_event);
  ws_connecting = true;
}

// WebSocket event handler
void websocket_event(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    log_message("WebSocket disconnected");
    ws_connecting = false;
    ws_connected = false;
    break;
  case WStype_CONNECTED:
    log_message("WebSocket connected");
    ws_connecting = false;
    ws_connected = true;
    break;
  case WStype_TEXT:
    log_message("WebSocket received text");
    handle_ws_payload(payload, length);
    break;
  case WStype_BIN:
    log_message("WebSocket received binary data");
    handle_ws_payload(payload, length);
    break;
  case WStype_PING:
    log_message("WebSocket received ping, sending pong");
    break;
  case WStype_PONG:
    log_message("WebSocket received pong");
    break;
  default:
    break;
  }
}

// Function to handle incoming WebSocket payloads
void handle_ws_payload(uint8_t *payload, size_t length)
{
  if (length < 1)
    return;

  uint8_t header = payload[0];

  switch (header)
  {
  case HEADER_SERVER_SET_CLIENT_CONFIG:
  case HEADER_SERVER_GET_CLIENT_CONFIG:
    Serial.write(header);
    Serial.write(payload, length);
    Serial.write(EOP);
    break;
  case HEADER_SERVER_DEBUG_ESP8266_RESET:
    log_message("Resetting hardware...");
    ESP.reset();
    break;
  case HEADER_SERVER_DEBUG_ESP8266_RESTART:
    log_message("Restarting software...");
    ESP.restart();
    break;
  case HEADER_SERVER_DEBUG_ESP8266_DISCONNECT_WS:
    log_message("Disconnecting WebSocket...");
    ws.disconnect();
    ws_connected = false;
    ws_connecting = false;
    break;
  default:
    log_message("Unknown header received");
    break;
  }
}

// Function to log debug information
void log_message(const char *message)
{
  String filtered_message = String(message);
  filtered_message.replace("\n", " ");                 // Replace all newline characters with a space
  Serial.write(HEADER_ESP8266_LOG_MESSAGE_TO_ARDUINO); // Send header byte
  Serial.write(filtered_message.c_str());              // Send the filtered message string
  Serial.write('\n');                                  // Add a newline at the end
  Serial.write(EOP);                                   // Add End of Packet (EOP)
}

void setup()
{
  Serial.begin(9600);

  // Set up GPIO pins (currently commented out)
  // pinMode(GPIO1_PIN, OUTPUT);
  // digitalWrite(GPIO1_PIN, LOW);
  // pinMode(GPIO2_PIN, OUTPUT);
  // digitalWrite(GPIO2_PIN, LOW);

  // Initial WiFi connection
  maintain_wifi();

  log_message("Setup done");
}

void loop()
{
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED)
  {
    maintain_wifi();
  }

  // Maintain WebSocket connection
  if (!ws_connecting && !ws_connected)
  {
    maintain_ws();
  }

  // Handle WebSocket events
  ws.loop();

  // Read data from Serial (non-blocking)
  while (Serial.available() > 0)
  {
    uint8_t incoming = static_cast<uint8_t>(Serial.read());
    uint8_t payload[CONFIG_PAYLOAD_LENGTH + 1];

    if (incoming <= 100 && Serial.read() == EOP)
    {
      ws.sendBIN(&incoming, 1);
      continue;
    }

    if (incoming == HEADER_CLIENT_SUBMIT_CONFIG)
    {
      payload[0] = incoming;
      for (int i = 1; i < CONFIG_PAYLOAD_LENGTH + 1; i++)
      {
        payload[i] = Serial.read();
      }
      if (Serial.read() == EOP)
      {
        ws.sendBIN(payload, CONFIG_PAYLOAD_LENGTH);
      }
      continue;
    }

    if (incoming == HEADER_CLIENT_GET_SERVER_CONFIG && Serial.read() == EOP)
    {
      ws.sendBIN(&incoming, 1);
      continue;
    }

    log_message("Unknown header received from Arduino");
  }
}
