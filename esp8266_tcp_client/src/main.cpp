#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#define API_KEY "key-16888888"

#define WIFI_MAX_RETRY_TIME_MS 10000

#define TCP_HOST "140.115.200.43"
#define TCP_PORT 9453
#define TCP_PING_INTERVAL_MS 5000
#define TCP_PONG_TIMEOUT_MS 10000

#define OPCODE_EMPTY (uint8_t)0
#define OPCODE_PING (uint8_t)101
#define OPCODE_PONG (uint8_t)102
#define OPCODE_SUBMIT_M (uint8_t)110
#define OPCODE_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define OPCODE_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define OPCODE_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define OPCODE_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define OPCODE_ESP8266_LOG 120
#define OPCODE_SERVER_DEBUG_ESP8266_RESET (uint8_t)121
#define OPCODE_SERVER_DEBUG_ESP8266_RESTART (uint8_t)122
#define OPCODE_SERVER_DEBUG_ESP8266_DISCONNECT_TCP (uint8_t)123
#define EOP (uint8_t)0x00

#define PACKET_CONFIG_PAYLOAD_SIZE 16

typedef struct
{
  uint8_t opcode;
  uint8_t *payload;
  size_t payload_size;
} Packet;

typedef struct
{
  const char *ssid;
  const char *password;
} WiFiCredentials;

WiFiCredentials wifi_list[] = {
    {"9G", "chee8888"},
    {"CH4", "chee8888"},
    {"Galaxy A21s5CF7", "94878787"},
};

bool tcp_connecting = false;
bool tcp_connected = false;
WiFiClient tcp_client;

void connect_to_best_wifi();
void maintain_wifi();
bool maintain_tcp();

void tcp_close();
inline void tcp_send(Packet *packet);
void tcp_send(uint8_t opcode, uint8_t *payload, size_t payload_size);
inline void serial_send(Packet *packet);
void serial_send(uint8_t opcode, uint8_t *payload, size_t payload_size);
void serial_println(String message);
void reset_packet(Packet *packet);
bool push_packet_payload(Packet *packet, uint8_t data);

void connect_to_best_wifi()
{
  int wifi_count = WiFi.scanNetworks();
  if (wifi_count == 0)
  {
    serial_println("no Wi-Fi found");
    delay(100);
    return;
  }

  int best_signal_strength = -100;
  const char *best_ssid = nullptr;
  const char *best_password = nullptr;

  for (int i = 0; i < wifi_count; i++)
  {
    String ssid = WiFi.SSID(i);
    int signal_strength = WiFi.RSSI(i);

    for (auto &credentials : wifi_list)
    {
      if (ssid == credentials.ssid && signal_strength > best_signal_strength)
      {
        best_signal_strength = signal_strength;
        best_ssid = credentials.ssid;
        best_password = credentials.password;
      }
    }
  }

  if (best_ssid)
  {
    WiFi.begin(best_ssid, best_password);
    unsigned long start_attempt_time = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start_attempt_time < WIFI_MAX_RETRY_TIME_MS)
    {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      String msg = "Wi-Fi connected, SSID: ";
      msg.concat(best_ssid);
      serial_println(msg);
      msg = "IP address: ";
      msg.concat(WiFi.localIP().toString());
      serial_println(msg);
    }
    else
    {
      serial_println("Wi-Fi connection failed");
    }
  }
  else
  {
    serial_println("no valid Wi-Fi");
  }
}

void maintain_wifi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connect_to_best_wifi();
    if (WiFi.status() == WL_CONNECTED)
    {
      maintain_tcp();
    }
  }
}

bool maintain_tcp()
{
  if (tcp_connecting || tcp_connected)
    return false;

  serial_println("TCP connecting...");
  tcp_connecting = true;
  if (tcp_client.connect(TCP_HOST, TCP_PORT))
  {
    tcp_client.setTimeout(10000);
    tcp_connected = true;
    tcp_connecting = false;

    // 發送認證頭
    String auth_message = "CO3006-Name: ";
    auth_message += WiFi.macAddress();
    auth_message += "\r\nCO3006-Auth: ";
    auth_message += API_KEY;
    auth_message += "\r\nCO3006-WiFi: ";
    auth_message += WiFi.SSID();
    auth_message += "\r\nCO3006-Local-IP: ";
    auth_message += WiFi.localIP().toString();
    tcp_client.print(auth_message);
    serial_println("TCP connected");
    return true;
  }
  else
  {
    tcp_connecting = false;
    serial_println("TCP connection failed");
    delay(100);
    return false;
  }
}

void tcp_close()
{
  tcp_client.stop();
  tcp_connecting = false;
  tcp_connected = false;
  serial_println("TCP closed");
}

inline void tcp_send(Packet *packet)
{
  tcp_send(packet->opcode, packet->payload, packet->payload_size);
}

void tcp_send(uint8_t opcode, uint8_t *payload, size_t payload_size)
{
  uint8_t *buffer = (uint8_t *)malloc(payload_size + 1);
  buffer[0] = opcode;
  memcpy(buffer + 1, payload, payload_size);
  tcp_client.write(buffer, payload_size + 1);
}

inline void serial_send(Packet *packet)
{
  serial_send(packet->opcode, packet->payload, packet->payload_size);
}

void serial_send(uint8_t opcode, uint8_t *payload, size_t payload_size)
{
  Serial.write(opcode);
  Serial.write(payload, payload_size);
  Serial.write(EOP);
}

void serial_println(String message)
{
  Serial.write(OPCODE_ESP8266_LOG);
  Serial.write(message.c_str());
  Serial.write('\n');
  Serial.write(EOP);
}

void reset_packet(Packet *packet)
{
  packet->opcode = OPCODE_EMPTY;
  free(packet->payload);
  packet->payload = NULL;
  packet->payload_size = (size_t)0;
}

bool push_packet_payload(Packet *packet, uint8_t data)
{
  uint8_t *new_payload = (uint8_t *)realloc(packet->payload, ++packet->payload_size);

  if (!new_payload)
  {
    // memory error
    reset_packet(packet);
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
  tcp_client.setNoDelay(true);
  maintain_wifi();
  serial_println("setup done");
}

void loop()
{
  static unsigned long last_tcp_ping_ms = 0;
  static unsigned long last_tcp_last_received_ms = 0;
  static unsigned long current_ms = 0;
  static uint8_t incoming;
  static Packet tcp_packet = {OPCODE_EMPTY, NULL, (size_t)0};
  static Packet serial_packet = {OPCODE_EMPTY, NULL, (size_t)0};

  current_ms = millis();

  if (WiFi.status() != WL_CONNECTED)
  {
    maintain_wifi();
  }

  if (!tcp_connecting && !tcp_connected)
  {
    if (maintain_tcp())
    {
      last_tcp_last_received_ms = current_ms;
    }
  }

  // heartbeat
  if (tcp_connected)
  {
    if (current_ms - last_tcp_ping_ms >= TCP_PING_INTERVAL_MS)
    {
      serial_println("TCP ping");
      tcp_send(OPCODE_PING, NULL, 0);
      last_tcp_ping_ms = current_ms;
    }

    if (current_ms - last_tcp_last_received_ms >= TCP_PONG_TIMEOUT_MS)
    {
      tcp_close();
    }
  }

  if (tcp_connected && tcp_client.available())
  {
    last_tcp_last_received_ms = current_ms;
    // read packet
    do
    {
      incoming = (uint8_t)tcp_client.read();

      if (tcp_packet.opcode == OPCODE_EMPTY)
      {
        tcp_packet.opcode = incoming;
        // no payload packet handler
        switch (tcp_packet.opcode)
        {
        case OPCODE_PING:
          tcp_send(OPCODE_PONG, NULL, 0);
          reset_packet(&tcp_packet);
          serial_println("TCP on ping");
          break;

        case OPCODE_PONG:
          serial_println("TCP on pong");
          reset_packet(&tcp_packet);
          break;

        case OPCODE_SERVER_GET_CLIENT_CONFIG:
          serial_send(&tcp_packet);
          reset_packet(&tcp_packet);
          break;

        case OPCODE_SERVER_DEBUG_ESP8266_RESET:
          serial_println("debug restart");
          ESP.restart();
          reset_packet(&tcp_packet);
          break;

        case OPCODE_SERVER_DEBUG_ESP8266_RESTART:
          serial_println("debug reset");
          ESP.reset();
          reset_packet(&tcp_packet);
          break;

        case OPCODE_SERVER_DEBUG_ESP8266_DISCONNECT_TCP:
          serial_println("debug disconnect");
          tcp_close();
          reset_packet(&tcp_packet);
          break;

        default:
          break;
        }
      }
      else
      {
        switch (tcp_packet.opcode)
        {
        case OPCODE_SERVER_SET_CLIENT_CONFIG:
          push_packet_payload(&tcp_packet, incoming);
          if (tcp_packet.payload_size < PACKET_CONFIG_PAYLOAD_SIZE)
          {
            break;
          }
          serial_send(&tcp_packet);
          reset_packet(&tcp_packet);
          break;

        default:
          reset_packet(&tcp_packet);
          break;
        }
      }
    } while (tcp_connected && tcp_client.available());
  }

  if (Serial.available())
  {
    incoming = (uint8_t)Serial.read();

    if (serial_packet.opcode == OPCODE_EMPTY)
    {
      serial_packet.opcode = incoming;
    }
    else
    {
      switch (serial_packet.opcode)
      {
      case OPCODE_SUBMIT_M:
        if (serial_packet.payload_size == 0)
        {
          push_packet_payload(&serial_packet, incoming);
          break;
        }
        if (incoming == EOP)
        {
          if (serial_packet.payload_size == 1)
          {
            // 轉發封包
            tcp_send(&serial_packet);
          }
        }
        reset_packet(&serial_packet);
        break;

      case OPCODE_CLIENT_SUBMIT_CONFIG:
        if (serial_packet.payload_size < PACKET_CONFIG_PAYLOAD_SIZE)
        {
          push_packet_payload(&serial_packet, incoming);
          break;
        }
        if (incoming == EOP)
        {
          // 轉發封包
          tcp_send(&serial_packet);
        }
        reset_packet(&serial_packet);
        break;

      case OPCODE_CLIENT_GET_SERVER_CONFIG:
        if (incoming == EOP)
        {
          // 轉發封包
          tcp_send(&serial_packet);
        }
        reset_packet(&serial_packet);
        break;

      default:
        serial_println("unknown opcode");
        reset_packet(&serial_packet);
        break;
      }
    }
  }

  delay(1);
}
