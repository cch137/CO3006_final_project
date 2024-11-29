#include <Arduino.h>
#include <SoftwareSerial.h>

#define RESET_PIN 7
#define RX_PIN 0
#define TX_PIN 1
#define WATER_PUMP_PIN 3
#define SENSOR_ANALOG_PIN A0

#define HEADER_EMPTY (uint8_t)0
#define HEADER_SUBMIT_M (uint8_t)110
#define HEADER_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define HEADER_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define HEADER_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define HEADER_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define HEADER_ESP8266_LOG_MESSAGE (uint8_t)120
#define EOP (uint8_t)0x00

// 檢查是否要澆水的頻率
#define INNER_INTERVAL_MS 100
#define PACKET_CONFIG_PAYLOAD_SIZE 16

typedef struct
{
  uint8_t header;
  uint8_t *payload;
  size_t payload_size;
} SerialPacket;

bool is_watering = false;

uint32_t V_offset = 250;
uint32_t L = 30;
uint32_t U = 70;
uint32_t I = 1000;

SoftwareSerial ESP8266Serial(RX_PIN, TX_PIN);

void reset_serial_packet(SerialPacket *packet);
bool append_serial_packet_payload(SerialPacket *packet, uint8_t data);
uint8_t get_M();

void reset_serial_packet(SerialPacket *packet)
{
  packet->header = HEADER_EMPTY;
  free(packet->payload);
  packet->payload = NULL;
  packet->payload_size = (size_t)0;
}

bool append_serial_packet_payload(SerialPacket *packet, uint8_t data)
{
  packet->payload = (uint8_t *)realloc(packet->payload, packet->payload_size + 1);

  if (!packet->payload)
  {
    // 記憶體分配失敗時重啟機器
    Serial.println("RESET");
    reset_serial_packet(packet);
    digitalWrite(RESET_PIN, LOW);
    return false;
  }

  packet->payload[packet->payload_size] = data;
  ++packet->payload_size;
  return true;
}

uint8_t get_M()
{
  // Read the analog sensor value
  uint32_t V_raw = (uint32_t)analogRead(SENSOR_ANALOG_PIN);

  // Calculate soil moisture percentage
  uint8_t M = (uint8_t)((uint32_t)1 - max(V_raw - V_offset, (uint32_t)0) / float(1023.0 - V_offset)) * (uint32_t)100;

  return M;
}

void setup()
{
  Serial.begin(115200);
  ESP8266Serial.begin(9600);

  // 先寫入避免出錯
  digitalWrite(RESET_PIN, HIGH);

  pinMode(RESET_PIN, OUTPUT);
  pinMode(SENSOR_ANALOG_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);

  digitalWrite(RESET_PIN, HIGH);
  digitalWrite(WATER_PUMP_PIN, LOW);
}

void loop()
{
  static unsigned long last_task1_ms = 0;
  static unsigned long last_task2_ms = 0;
  static unsigned long current_ms = 0;

  static SerialPacket packet;
  static uint8_t incoming = 0;
  static uint8_t M = UINT8_MAX;

  M = UINT8_MAX;
  current_ms = millis();

  // 提交資料到 server
  if (current_ms - last_task1_ms > I)
  {
    last_task1_ms = current_ms;
    if (M == UINT8_MAX)
    {
      M = get_M();
    }
    Serial.println("Arduino M");
    ESP8266Serial.write(HEADER_SUBMIT_M);
    // ESP8266Serial.write(M);
    // ESP8266Serial.write(EOP);
  }

  // 檢查是否要澆水
  // 檢查是否要澆水的頻率
  if (current_ms - last_task2_ms > INNER_INTERVAL_MS)
  {
    last_task2_ms = current_ms;
    if (M == UINT8_MAX)
    {
      M = get_M();
    }

    if (M < L)
    {
      // 開始澆水
      digitalWrite(WATER_PUMP_PIN, HIGH);
      is_watering = true;
    }

    if (is_watering && M >= U)
    {
      // 停止澆水
      digitalWrite(WATER_PUMP_PIN, LOW);
      is_watering = false;
    }
  }

  if (ESP8266Serial.available())
  {
    incoming = ESP8266Serial.read();

    if (packet.header == HEADER_EMPTY)
    {
      packet.header = incoming;
    }
    else
    {
      switch (packet.header)
      {
      case HEADER_SERVER_SET_CLIENT_CONFIG:
        if (packet.payload_size < PACKET_CONFIG_PAYLOAD_SIZE)
        {
          append_serial_packet_payload(&packet, incoming);
          break;
        }
        if (incoming == EOP)
        {
          V_offset = *(uint32_t *)&packet.payload[0];
          L = *(uint32_t *)&packet.payload[4];
          U = *(uint32_t *)&packet.payload[8];
          I = *(uint32_t *)&packet.payload[12];
        }
        reset_serial_packet(&packet);
        break;

      case HEADER_SERVER_GET_CLIENT_CONFIG:
        if (incoming == EOP)
        {
          ESP8266Serial.write(HEADER_CLIENT_SUBMIT_CONFIG);
          ESP8266Serial.write((uint8_t *)&V_offset, (size_t)4);
          ESP8266Serial.write((uint8_t *)&L, (size_t)4);
          ESP8266Serial.write((uint8_t *)&U, (size_t)4);
          ESP8266Serial.write((uint8_t *)&I, (size_t)4);
          ESP8266Serial.write(EOP);
        }
        reset_serial_packet(&packet);
        break;

      case HEADER_ESP8266_LOG_MESSAGE:
        if (incoming == EOP)
        {
          Serial.write("[ESP8266]: ");
          Serial.write(packet.payload, packet.payload_size);
          reset_serial_packet(&packet);
          break;
        }
        append_serial_packet_payload(&packet, incoming);
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
