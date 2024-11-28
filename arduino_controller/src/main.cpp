#include <Arduino.h>
#include <SoftwareSerial.h>

// Define pins
#define RX_PIN 0
#define TX_PIN 1

#define SENSOR_ANALOG_PIN A0
#define WATER_PUMP_PIN 3

#define HEADER_CLIENT_SUBMIT_CONFIG (uint8_t)111
#define HEADER_SERVER_SET_CLIENT_CONFIG (uint8_t)112
#define HEADER_SERVER_GET_CLIENT_CONFIG (uint8_t)113
#define HEADER_CLIENT_GET_SERVER_CONFIG (uint8_t)114
#define HEADER_ESP8266_LOG_MESSAGE_TO_ARDUINO (uint8_t)120
#define EOP (uint8_t)0x00

#define CONFIG_PAYLOAD_LENGTH 16

// Create SoftwareSerial instance for communication with ESP8266
SoftwareSerial espSerial(RX_PIN, TX_PIN);

// Variables for soil moisture calculation
uint32_t V_offset = 250;
uint32_t L = 30;
uint32_t U = 70;
uint32_t I = 1000;

bool isWatering = false;

// Function prototypes
void handleIncomingData();
void sendSoilMoistureToESP8266(uint8_t moisture);
void sendClientConfigToESP8266();

void setup()
{
  // Initialize Serial for debugging
  Serial.begin(115200);
  while (!Serial)
  {
    ; // Wait for Serial to be ready
  }
  espSerial.begin(9600);
  while (!espSerial)
  {
    ; // Wait for SoftwareSerial to be ready
  }
  pinMode(SENSOR_ANALOG_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW); // Ensure the water pump is off at startup
}

void loop()
{
  // Handle incoming data from ESP8266
  handleIncomingData();

  // Read soil moisture and send data periodically
  static unsigned long lastSentTimeMs = 0;
  unsigned long currentTimeMs = millis();
  if (currentTimeMs - lastSentTimeMs >= I)
  {
    // Read the analog sensor value
    int V_raw = analogRead(SENSOR_ANALOG_PIN);

    // Calculate soil moisture percentage
    uint8_t M = (1 - max(V_raw - V_offset, 0) / float(1023 - V_offset)) * 100;

    // Send soil moisture to ESP8266
    sendSoilMoistureToESP8266(M);

    // Control watering based on moisture level
    if (M < L && !isWatering)
    {
      digitalWrite(WATER_PUMP_PIN, HIGH); // Start watering
      isWatering = true;
    }
    else if (M >= U && isWatering)
    {
      digitalWrite(WATER_PUMP_PIN, LOW); // Stop watering
      isWatering = false;
    }

    // Debug output
    Serial.print("A0: ");
    Serial.print(V_raw);
    Serial.print(" (");
    Serial.print(M);
    Serial.println("%)");

    lastSentTimeMs = currentTimeMs;
  }
}

// Function to handle incoming data from ESP8266
void handleIncomingData()
{
  static bool isReadingLogMessage = false;
  static bool isMessageBufferEnd = false;
  static String logMessageBuffer = "";
  static uint8_t configPayloadBuffer[CONFIG_PAYLOAD_LENGTH];
  static uint8_t incomingData = 0;

  while (espSerial.available() > 0)
  {
    incomingData = espSerial.read();

    if (isReadingLogMessage)
    {
      if (isMessageBufferEnd)
      {
        if (incomingData == EOP)
        {
          Serial.print("[ESP8266]: ");
          Serial.println(logMessageBuffer);
        }
        else
        {
          Serial.print("[ESP8266]: (incomplete) ");
          Serial.println(logMessageBuffer);
        }
        isMessageBufferEnd = false;
        isReadingLogMessage = false;
        logMessageBuffer = "";
      }
      else if (incomingData == '\n')
      {
        isMessageBufferEnd = true;
      }
      else
      {
        logMessageBuffer += (char)incomingData;
      }
      continue;
    }

    if (incomingData == HEADER_ESP8266_LOG_MESSAGE_TO_ARDUINO)
    {
      isReadingLogMessage = true;
      continue;
    }

    if (incomingData == HEADER_SERVER_SET_CLIENT_CONFIG)
    {
      int i;
      for (i = 0; i < CONFIG_PAYLOAD_LENGTH; i++)
      {
        configPayloadBuffer[i] = (uint8_t)espSerial.read();
      }
      if ((uint8_t)espSerial.read() == EOP)
      {
        V_offset = *(uint32_t *)&configPayloadBuffer[0];
        L = *(uint32_t *)&configPayloadBuffer[4];
        U = *(uint32_t *)&configPayloadBuffer[8];
        I = *(uint32_t *)&configPayloadBuffer[12];
        sendClientConfigToESP8266();
      }
      continue;
    }

    if (incomingData == HEADER_SERVER_GET_CLIENT_CONFIG && (uint8_t)espSerial.read() == EOP)
    {
      sendClientConfigToESP8266();
      continue;
    }
  }
}

// Function to send soil moisture data to ESP8266
void sendSoilMoistureToESP8266(uint8_t moisture)
{
  espSerial.write(moisture);
  espSerial.write(EOP);
}

// Function to send current configuration to ESP8266
void sendClientConfigToESP8266()
{
  espSerial.write(HEADER_CLIENT_SUBMIT_CONFIG);
  espSerial.write((uint8_t *)&V_offset, 4);
  espSerial.write((uint8_t *)&L, 4);
  espSerial.write((uint8_t *)&U, 4);
  espSerial.write((uint8_t *)&I, 4);
  espSerial.write(EOP);
}
