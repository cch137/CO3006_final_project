#include <Arduino.h>
#include <SoftwareSerial.h>

#define RESET_PIN 7
#define WATER_PUMP_PIN 5
#define M01_SENSOR_PIN A5
#define M01_TX_PIN A3
#define M01_RX_PIN A4
#define ESP8266_EN_PIN 13

#define HEADER_EMPTY (uint8_t)0
#define HEADER_SUBMIT_M (uint8_t)110
#define HEADER_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define HEADER_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define HEADER_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define HEADER_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define HEADER_ESP8266_LOG_MESSAGE (uint8_t)120
#define EOP (uint8_t)0x00

// 檢查是否要澆水的頻率（正在澆水中）
#define DETECT_INTERVAL_BUSY_MS 100
// 檢查是否要澆水的頻率（待機中）
#define DETECT_INTERVAL_IDLE_MS 10000
// 未初始化時提示訊息的頻率
#define WAITING_LOG_INTERVAL_MS 3000
#define PACKET_CONFIG_PAYLOAD_SIZE 16

typedef struct
{
  uint8_t header;
  uint8_t *payload;
  size_t payload_size;
} Packet;

bool config_inited = false;
bool is_watering = false;

uint32_t V_offset = 350;
uint32_t L = 30;
uint32_t U = 70;
uint32_t I = 10000;

SoftwareSerial ESP8266Serial(M01_RX_PIN, M01_TX_PIN);

void reset_packet(Packet *packet);
bool push_packet_payload(Packet *packet, uint8_t data);
uint8_t get_M();

void reset_packet(Packet *packet)
{
  packet->header = HEADER_EMPTY;
  free(packet->payload);
  packet->payload = NULL;
  packet->payload_size = (size_t)0;
}

bool push_packet_payload(Packet *packet, uint8_t data)
{
  uint8_t *new_payload = (uint8_t *)realloc(packet->payload, ++packet->payload_size);

  if (!new_payload)
  {
    // 記憶體分配失敗時重啟機器
    Serial.println("RESET");
    reset_packet(packet);
    digitalWrite(RESET_PIN, LOW);

    return false;
  }

  packet->payload = new_payload;
  packet->payload[packet->payload_size - 1] = data;

  return true;
}

uint8_t get_M()
{
  int V_raw = analogRead(M01_SENSOR_PIN);

  uint8_t M = (1 - max(V_raw - (int)V_offset, 0) / float(1023 - V_offset)) * 100;

  return M;
}

void setup()
{
  Serial.begin(115200);
  ESP8266Serial.begin(9600);

  // 先寫入避免出錯
  digitalWrite(RESET_PIN, HIGH);

  pinMode(RESET_PIN, OUTPUT);
  pinMode(M01_SENSOR_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  pinMode(ESP8266_EN_PIN, OUTPUT);

  digitalWrite(RESET_PIN, HIGH);
  digitalWrite(WATER_PUMP_PIN, LOW);
  digitalWrite(ESP8266_EN_PIN, LOW);
  delay(100);
  digitalWrite(ESP8266_EN_PIN, HIGH);
}

void loop()
{
  static unsigned long last_task1_ms = 0;
  static unsigned long last_task2_ms = 0;
  static unsigned long current_ms = 0;

  static Packet packet;
  static uint8_t incoming = 0;
  static uint8_t M = UINT8_MAX;

  M = UINT8_MAX;
  current_ms = millis();

  // config 初始化之後才開始做定時任務和澆水
  if (config_inited)
  {
    // 提交資料到 server
    if (current_ms - last_task1_ms > I)
    {
      last_task1_ms = current_ms;
      if (M == UINT8_MAX)
      {
        M = get_M();
        Serial.print("M=");
        Serial.println(M);
      }
      ESP8266Serial.write(HEADER_SUBMIT_M);
      ESP8266Serial.write(M);
      ESP8266Serial.write(EOP);
    }

    // 檢查是否要澆水
    // 檢查是否要澆水的頻率
    if (current_ms - last_task2_ms > (is_watering ? DETECT_INTERVAL_BUSY_MS : DETECT_INTERVAL_IDLE_MS))
    {
      last_task2_ms = current_ms;
      if (M == UINT8_MAX)
      {
        M = get_M();
      }

      if (!is_watering && M < L)
      {
        // 開始澆水
        digitalWrite(WATER_PUMP_PIN, HIGH);
        is_watering = true;
        Serial.println("start watering");
      }

      if (is_watering && M >= U)
      {
        // 停止澆水
        digitalWrite(WATER_PUMP_PIN, LOW);
        is_watering = false;
        Serial.println("stop watering");
      }
    }
  }
  else
  {
    if (current_ms - last_task2_ms > WAITING_LOG_INTERVAL_MS)
    {
      last_task2_ms = current_ms;
      Serial.println("waiting for server initialization...");
      ESP8266Serial.write(HEADER_CLIENT_GET_SERVER_CONFIG);
      ESP8266Serial.write(EOP);
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
          push_packet_payload(&packet, incoming);
          break;
        }
        if (incoming == EOP)
        {
          V_offset = *(uint32_t *)&packet.payload[0];
          L = *(uint32_t *)&packet.payload[4];
          U = *(uint32_t *)&packet.payload[8];
          I = *(uint32_t *)&packet.payload[12];
          if (!config_inited)
          {
            config_inited = true;
            Serial.print("machine initialized: ");
            Serial.print("V_offset=");
            Serial.print(V_offset);
            Serial.print(", L=");
            Serial.print(L);
            Serial.print(", U=");
            Serial.print(U);
            Serial.print(", I=");
            Serial.println(I);
          }
        }
        reset_packet(&packet);
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
        reset_packet(&packet);
        break;

      case HEADER_ESP8266_LOG_MESSAGE:
        if (incoming == EOP)
        {
          Serial.write("[ESP8266]: ");
          Serial.write(packet.payload, packet.payload_size);
          reset_packet(&packet);
          break;
        }
        push_packet_payload(&packet, incoming);
        break;

      default:
        reset_packet(&packet);
        break;
      }
    }
  }

  // 降低功耗
  delay(1);
}
