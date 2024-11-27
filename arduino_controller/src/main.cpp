#include <Arduino.h>
#include <SoftwareSerial.h>

// Define pins
#define RX_PIN 0
#define TX_PIN 1

#define SENSOR_ANALOG_PIN A0
#define SENSOR_DIGITAL_PIN 2

// Create SoftwareSerial instance for communication with ESP8266
SoftwareSerial espSerial(RX_PIN, TX_PIN);

// Function prototypes
void handleIncomingData();
void sendDataToEsp(uint8_t data);

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
  pinMode(SENSOR_DIGITAL_PIN, INPUT);
}

void loop()
{
  // Handle incoming data from ESP8266
  handleIncomingData();

  // Simulate sending data to ESP8266 periodically
  static unsigned long lastSentTimeMs = 0;
  unsigned long currentTimeMs = millis();
  if (currentTimeMs - lastSentTimeMs >= 1000)
  {
    uint8_t dataToSend = random(0, 256); // Simulate data to send
    sendDataToEsp(dataToSend);
    lastSentTimeMs = currentTimeMs;

    Serial.print("Analog Sensor Value: ");
    Serial.println(analogRead(SENSOR_ANALOG_PIN));
  }
}

// Function to handle incoming data from ESP8266
void handleIncomingData()
{
  static bool isReading = false;
  static String messageBuffer = "";

  while (espSerial.available() > 0)
  {
    uint8_t incomingData = espSerial.read();

    if (isReading)
    {
      if (incomingData == '\n')
      {
        Serial.print("[ESP8266]: ");
        Serial.println(messageBuffer);
        isReading = false;
        messageBuffer = "";
      }
      else
      {
        messageBuffer += (char)incomingData;
      }
      continue;
    }

    switch (incomingData)
    {
    case 200:
    {
      messageBuffer = ""; // Clear the buffer
      isReading = true;
      break;
    }
    default:
      break;
    }
  }
}

// Function to send data to ESP8266
void sendDataToEsp(uint8_t data)
{
  // espSerial.write(data);
  // Serial.print("Sent (fake) to ESP8266: ");
  // Serial.println(data);
}
