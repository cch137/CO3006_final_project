#include <Arduino.h>
#include <WebSocketsClient.h>
#include "ESP8266WiFi.h"

#define WIFI_SSID "9G"
#define WIFI_PASSWORD "chee8888"
#define WIFI_MAX_RETRY_TIME_MS 10000

#define WS_HOST "140.115.200.43"
#define WS_PORT 80
#define WS_URL "/jet-d/ncu/CO3006/conn"
#define WS_PING_INTERVAL_MS 10000
#define WS_PONG_TIMEOUT_MS 2000
#define WS_DISCONNECT_TIMEOUT_COUNT 5

#define EOP (uint8_t)0x00

#define TASK_INTERVAL_MS 1000

#define HEADER_EMPTY (uint8_t)0
#define HEADER_SUBMIT_H (uint8_t)110
#define HEADER_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define HEADER_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define HEADER_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define HEADER_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define HEADER_ESP8266_LOG 120
#define HEADER_SERVER_DEBUG_ESP8266_RESET (uint8_t)121
#define HEADER_SERVER_DEBUG_ESP8266_RESTART (uint8_t)122
#define HEADER_SERVER_DEBUG_ESP8266_DISCONNECT_WS (uint8_t)123

typedef struct
{
  uint8_t header;
  uint8_t *payload;
  size_t payload_size;
} SerialPacket;

bool ws_connecting = false;
bool ws_connected = false;
WebSocketsClient ws;

void maintain_wifi();
void maintain_ws();
void ws_event_handler(WStype_t type, uint8_t *payload, size_t length);
void ws_payload_handler(uint8_t *ws_payload, size_t length);

void send_serial_packet(uint8_t header, uint8_t *payload, size_t payload_size);
void serial_println(String message);

void maintain_wifi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned int i = 10;
  unsigned long start_attempt_time = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start_attempt_time < WIFI_MAX_RETRY_TIME_MS)
  {
    delay(100);

    if (++i > 20)
    {
      serial_println("WiFi connecting...");
      i = 0;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    String msg = "WiFi connected (";
    msg.concat(WiFi.localIP().toString());
    msg.concat(")");
    serial_println(msg);
    maintain_ws();
  }
  else
  {
    serial_println("WiFi connection failed");
  }
}

void maintain_ws()
{
  if (ws_connecting || ws_connected)
    return;

  serial_println("ws connecting...");
  ws.begin(F(WS_HOST), WS_PORT, F(WS_URL));
  ws.enableHeartbeat(WS_PING_INTERVAL_MS, WS_PONG_TIMEOUT_MS, WS_DISCONNECT_TIMEOUT_COUNT);

  ws.setExtraHeaders("CO3006-Sensor-Name: sensor-01\r\nCO3006-Auth: key-16888888");
  ws.onEvent(ws_event_handler);
  ws_connecting = true;
}

void ws_event_handler(WStype_t type, uint8_t *payload, size_t length)
{
  static int disconn_counter = 0;

  switch (type)
  {
  case WStype_DISCONNECTED:
    if (!disconn_counter)
    {
      serial_println("ws closed");
      ws_connecting = false;
      ws_connected = false;
    }
    // 降低重新連接的頻率
    if (++disconn_counter > 10)
    {
      disconn_counter = 0;
    }
    break;
  case WStype_CONNECTED:
    serial_println("ws opened");
    ws_connecting = false;
    ws_connected = true;
    disconn_counter = 0;
    break;
  case WStype_TEXT:
  case WStype_BIN:
    ws_payload_handler(payload, length);
    break;
  default:
    break;
  }
}

void ws_payload_handler(uint8_t *ws_payload, size_t length)
{
  if (length < 1)
    return;

  uint8_t header = ws_payload[0];

  switch (header)
  {
  case HEADER_SERVER_SET_CLIENT_CONFIG:
  case HEADER_SERVER_GET_CLIENT_CONFIG:
    Serial.write(ws_payload, length);
    Serial.write(EOP);
    break;
  case HEADER_SERVER_DEBUG_ESP8266_RESET:
    serial_println("resetting");
    ESP.reset();
    break;
  case HEADER_SERVER_DEBUG_ESP8266_RESTART:
    serial_println("restarting");
    ESP.restart();
    break;
  case HEADER_SERVER_DEBUG_ESP8266_DISCONNECT_WS:
    serial_println("disconnecting ws");
    ws.disconnect();
    ws_connected = false;
    ws_connecting = false;
    break;
  default:
    serial_println("unknown header");
    break;
  }
}

void send_serial_packet(uint8_t header, uint8_t *payload, size_t payload_size)
{
  Serial.write(header);
  Serial.write(payload, payload_size);
  Serial.write('\n');
  Serial.write(EOP);
}

void serial_println(String message)
{
  send_serial_packet(HEADER_ESP8266_LOG, (uint8_t *)message.c_str(), (size_t)message.length());
}

void setup()
{
  Serial.begin(9600);
  maintain_wifi();
  serial_println("setup done");
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

  static unsigned long last_task_ms = 0;
  static unsigned long current_ms = 0;

  current_ms = millis();

  if (current_ms - last_task_ms > TASK_INTERVAL_MS)
  {
    last_task_ms = current_ms;
    serial_println("OK");
  }

  // 降低功耗
  delay(1);
}
