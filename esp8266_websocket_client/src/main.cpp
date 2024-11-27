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

// WebSocket settings
#define WS_HOST "140.115.200.43"
#define WS_PORT 80
#define WS_URL "/jet-d/ncu/CO3006/conn"
#define WS_PING_INTERVAL_MS 10000
#define WS_PONG_TIMEOUT_MS 2000
#define WS_DISCONNECT_TIMEOUT_COUNT 5

// Function prototypes
void websocket_event(WStype_t type, uint8_t *payload, size_t length);
void log_debug_info(const char *message);
void maintain_wifi();
void maintain_ws();
void send_data(uint8_t data);

// WebSocket client instance
WebSocketsClient ws;

// Timers
unsigned long send_interval_ms = 1000;

// WebSocket connection flag
bool ws_connecting = false;
bool ws_connected = false;

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
      log_debug_info("Connecting to WiFi...");
      connecting_count = 0;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    log_debug_info("Connected to WiFi, local IP:");
    log_debug_info(WiFi.localIP().toString().c_str());

    // Setup WebSocket connection when WiFi is connected
    maintain_ws();
  }
  else
  {
    log_debug_info("Failed to connect to WiFi");
  }
}

void maintain_ws()
{
  if (ws_connecting)
    return;

  log_debug_info("Connecting WebSocket...");
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
    log_debug_info("WebSocket disconnected");
    ws_connecting = false;
    ws_connected = false;
    break;
  case WStype_CONNECTED:
    log_debug_info("WebSocket connected");
    ws_connecting = false;
    ws_connected = true;
    break;
  case WStype_TEXT:
    log_debug_info("WebSocket received text");
    break;
  case WStype_BIN:
    log_debug_info("WebSocket received binary data");
    break;
  case WStype_PING:
    log_debug_info("WebSocket received ping, sending pong");
    break;
  case WStype_PONG:
    log_debug_info("WebSocket received pong");
    break;
  default:
    break;
  }
}

// Function to log debug information
void log_debug_info(const char *message)
{
  String filtered_message = String(message);
  filtered_message.replace("\n", " ");    // Replace all newline characters with a space
  Serial.write((uint8_t)200);             // Send header byte
  Serial.write(filtered_message.c_str()); // Send the filtered message string
  Serial.write('\n');                     // Add a newline at the end
}

// Function to send data over WebSocket
void send_data(uint8_t data)
{
  if (WiFi.status() == WL_CONNECTED && ws.isConnected())
  {
    ws.sendBIN(&data, 1);
  }
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

  log_debug_info("Setup done");
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

  if (!ws_connected)
    return;

  static unsigned long last_sent_time_ms = 0;
  static unsigned long current_time_ms = 0;
  static uint8_t uint8_data = 0;

  current_time_ms = millis();

  // Periodically send data
  if (current_time_ms - last_sent_time_ms >= send_interval_ms)
  {
    // Increment data, 255 + 1 wraps around to 0
    uint8_data++;
    send_data(uint8_data);
    last_sent_time_ms = current_time_ms;
  }

  // Read data from Serial (non-blocking)
  while (Serial.available() > 0)
  {
    uint8_t data = static_cast<uint8_t>(Serial.read());
    data++;
    // log_debug_info("Serial read a byte");
  }
}
