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
#define SERIAL_READ_TIMEOUT_MS 100

// Function prototypes
void websocket_event(WStype_t type, uint8_t *payload, size_t length);
void log_message(const char *message, bool is_error = false);
void maintain_wifi();
void maintain_ws();
void handle_ws_payload(uint8_t *payload, size_t length);
bool read_serial_payload(uint8_t *buffer, size_t length);

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
  while (WiFi.status() != WL_CONNECTED && millis() - start_attempt_time < WIFI_MAX_RETRY_TIME_MS)
  {
    delay(100);
    log_message("Connecting to WiFi...");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    log_message("Connected to WiFi");
    log_message(WiFi.localIP().toString().c_str());
    maintain_ws();
  }
  else
  {
    log_message("Failed to connect to WiFi", true);
  }
}

void maintain_ws()
{
  if (ws_connecting || ws_connected)
    return;

  log_message("Connecting WebSocket...");
  ws.begin(F(WS_HOST), WS_PORT, F(WS_URL));
  ws.enableHeartbeat(WS_PING_INTERVAL_MS, WS_PONG_TIMEOUT_MS, WS_DISCONNECT_TIMEOUT_COUNT);

  ws.setExtraHeaders("CO3006-Sensor-Name: sensor-01\r\nCO3006-Auth: key-16888888");
  ws.onEvent(websocket_event);
  ws_connecting = true;
}

void websocket_event(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    log_message("WebSocket disconnected", true);
    ws_connecting = false;
    ws_connected = false;
    break;
  case WStype_CONNECTED:
    log_message("WebSocket connected");
    ws_connecting = false;
    ws_connected = true;
    break;
  case WStype_TEXT:
  case WStype_BIN:
    handle_ws_payload(payload, length);
    break;
  default:
    break;
  }
}

void handle_ws_payload(uint8_t *payload, size_t length)
{
  if (length < 1)
    return;

  uint8_t header = payload[0];
  switch (header)
  {
  case HEADER_SERVER_SET_CLIENT_CONFIG:
  case HEADER_SERVER_GET_CLIENT_CONFIG:
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
    log_message("Unknown header received", true);
    break;
  }
}

void log_message(const char *message, bool is_error)
{
  String filtered_message = String(message);
  filtered_message.replace("\n", " ");
  Serial.write(HEADER_ESP8266_LOG_MESSAGE_TO_ARDUINO);
  Serial.write(is_error ? "ERR " : "INFO ");
  Serial.write(filtered_message.c_str());
  Serial.write('\n');
  Serial.write(EOP);
}

bool read_serial_payload(uint8_t *buffer, size_t length)
{
  unsigned long start_time = millis();
  size_t index = 0;

  while (index < length && millis() - start_time < SERIAL_READ_TIMEOUT_MS)
  {
    if (Serial.available() > 0)
    {
      buffer[index++] = static_cast<uint8_t>(Serial.read());
    }
  }

  if (index != length || Serial.read() != EOP)
  {
    log_message("Invalid Serial payload", true);
    return false;
  }

  return true;
}

void setup()
{
  Serial.begin(9600);
  maintain_wifi();
  log_message("Setup complete");
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    maintain_wifi();
  }

  if (!ws_connecting && !ws_connected)
  {
    maintain_ws();
  }

  ws.loop();

  if (Serial.available() > 0)
  {
    uint8_t incoming = static_cast<uint8_t>(Serial.read());
    uint8_t buffer[CONFIG_PAYLOAD_LENGTH + 1];

    if (incoming == HEADER_CLIENT_SUBMIT_CONFIG && read_serial_payload(buffer, CONFIG_PAYLOAD_LENGTH))
    {
      ws.sendBIN(buffer, CONFIG_PAYLOAD_LENGTH);
    }
    else if (incoming == HEADER_CLIENT_GET_SERVER_CONFIG && Serial.read() == EOP)
    {
      ws.sendBIN(&incoming, 1);
    }
    else
    {
      log_message("Unknown or invalid Serial command", true);
    }
  }
}
