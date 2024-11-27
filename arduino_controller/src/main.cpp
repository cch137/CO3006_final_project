#include <Arduino.h>
#include <SoftwareSerial.h>

// Define pins for SoftwareSerial
#define RX_PIN 0
#define TX_PIN 1

// Create SoftwareSerial instance for communication with ESP8266
SoftwareSerial espSerial(RX_PIN, TX_PIN);

// Function prototypes
void handleIncomingData();
void sendDataToEsp(uint8_t data);

void setup() {
  // Initialize Serial for debugging
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for Serial to be ready
  }
  Serial.println("Arduino Serial Ready");

  // Initialize SoftwareSerial for communication with ESP8266
  espSerial.begin(9600);
  Serial.println("SoftwareSerial Ready");
}

void loop() {
  // Handle incoming data from ESP8266
  handleIncomingData();

  // Simulate sending data to ESP8266 periodically
  static unsigned long lastSentTimeMs = 0;
  unsigned long currentTimeMs = millis();
  if (currentTimeMs - lastSentTimeMs >= 1000) {
    uint8_t dataToSend = random(0, 256); // Simulate data to send
    sendDataToEsp(dataToSend);
    lastSentTimeMs = currentTimeMs;
  }
}

// Function to handle incoming data from ESP8266
void handleIncomingData() {
  static bool readingMessage = false;
  static String messageBuffer = "";

  while (espSerial.available() > 0) {
    uint8_t incomingData = espSerial.read();

    if (!readingMessage) {
      if (incomingData == 200) {
        readingMessage = true; // Start reading the message
        messageBuffer = "";   // Clear the buffer
      }
    } else {
      if (incomingData == '\n') {
        // End of message
        Serial.print("Received from ESP8266: ");
        Serial.println(messageBuffer);
        readingMessage = false;
      } else {
        messageBuffer += (char)incomingData; // Append data to buffer
      }
    }
  }
}

// Function to send data to ESP8266
void sendDataToEsp(uint8_t data) {
  // espSerial.write(data);
  Serial.print("Sent (fake) to ESP8266: ");
  Serial.println(data);
}
