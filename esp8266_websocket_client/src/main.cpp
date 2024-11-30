#include <Arduino.h>
#include <WebSocketsClient.h>
#include "ESP8266WiFi.h"

#define API_KEY "key-16888888"

#define WIFI_SSID "9G"
#define WIFI_PASSWORD "chee8888"
#define WIFI_MAX_RETRY_TIME_MS 10000

#define WS_HOST "140.115.200.43"
#define WS_PORT 80
#define WS_URL "/jet/ncu/CO3006/conn"
#define WS_PING_INTERVAL_MS 10000
#define WS_PONG_TIMEOUT_MS 2000
#define WS_DISCONNECT_TIMEOUT_COUNT 5

#define HEADER_EMPTY (uint8_t)0
#define HEADER_SUBMIT_M (uint8_t)110
#define HEADER_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define HEADER_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define HEADER_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define HEADER_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define HEADER_ESP8266_LOG 120
#define HEADER_SERVER_DEBUG_ESP8266_RESET (uint8_t)121
#define HEADER_SERVER_DEBUG_ESP8266_RESTART (uint8_t)122
#define HEADER_SERVER_DEBUG_ESP8266_DISCONNECT_WS (uint8_t)123
#define EOP (uint8_t)0x00

#define PACKET_CONFIG_PAYLOAD_SIZE 16

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
void reset_serial_packet(SerialPacket *packet);
bool append_serial_packet_payload(SerialPacket *packet, uint8_t data);

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

  String headers = "";
  headers.concat("CO3006-Name: ");
  headers.concat(WiFi.macAddress());
  headers.concat("\r\nCO3006-Auth: ");
  headers.concat(API_KEY);

  ws.setExtraHeaders(headers.c_str());
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

void reset_serial_packet(SerialPacket *packet)
{
  packet->header = HEADER_EMPTY;
  free(packet->payload);
  packet->payload = NULL;
  packet->payload_size = (size_t)0;
}

bool append_serial_packet_payload(SerialPacket *packet, uint8_t data)
{
  uint8_t *new_payload = (uint8_t *)realloc(packet->payload, ++packet->payload_size);

  if (!new_payload)
  {
    // 記憶體分配失敗時重啟機器
    reset_serial_packet(packet);
    ESP.reset();

    return false;
  }

  packet->payload = new_payload;
  packet->payload[packet->payload_size - 1] = data;

  return true;
}

void setup()
{
  Serial.begin(9600);
  maintain_wifi();
  serial_println("setup done");
}

void loop()
{
  static SerialPacket packet;
  static uint8_t incoming = 0;

  if (WiFi.status() != WL_CONNECTED)
  {
    maintain_wifi();
  }

  if (!ws_connecting && !ws_connected)
  {
    maintain_ws();
  }

  ws.loop();

  if (Serial.available())
  {
    incoming = Serial.read();

    if (packet.header == HEADER_EMPTY)
    {
      packet.header = incoming;
    }
    else
    {
      switch (packet.header)
      {
      case HEADER_SUBMIT_M:
        if (packet.payload_size == 0)
        {
          append_serial_packet_payload(&packet, incoming);
          break;
        }
        if (incoming == EOP)
        {
          if (packet.payload_size == 1)
          {
            // 發送 M 到 server
            append_serial_packet_payload(&packet, packet.payload[0]);
            packet.payload[0] = packet.header;
            ws.sendBIN(packet.payload, packet.payload_size);
          }
        }
        reset_serial_packet(&packet);
        break;

      case HEADER_CLIENT_SUBMIT_CONFIG:
        if (packet.payload_size < PACKET_CONFIG_PAYLOAD_SIZE)
        {
          append_serial_packet_payload(&packet, incoming);
          break;
        }
        if (incoming == EOP)
        {
          append_serial_packet_payload(&packet, EOP);
          for (int i = packet.payload_size - 1; i > 0; --i)
          {
            // 不要把 --i 搬到右式，因為右式會先被計算。
            packet.payload[i] = packet.payload[i - 1];
          }
          packet.payload[0] = packet.header;
          ws.sendBIN(packet.payload, packet.payload_size);
        }
        reset_serial_packet(&packet);
        break;

      case HEADER_CLIENT_GET_SERVER_CONFIG:
        if (incoming == EOP)
        {
          // 轉發封包到 server
          append_serial_packet_payload(&packet, packet.header);
          ws.sendBIN(packet.payload, packet.payload_size);
        }
        reset_serial_packet(&packet);
        break;

      default:
        reset_serial_packet(&packet);
        break;
      }
    }
  }

  // 降低功耗
  delay(1);
}
