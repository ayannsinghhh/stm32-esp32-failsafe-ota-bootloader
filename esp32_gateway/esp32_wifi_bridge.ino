/*
 * ESP32 Wi-Fi to UART Transparent Bridge for STM32 OTA Updates
 *
 * This sketch runs on an ESP32, connects to a local Wi-Fi network (or hosts a SoftAP),
 * listens on a TCP socket (port 8080), and bi-directionally streams firmware chunks
 * between the Python host deployment tool and the STM32F446RE bootloader over UART2.
 *
 * Pinout:
 *   ESP32 Pin 17 (TX2) -> STM32 PA3 (RX / D0)
 *   ESP32 Pin 16 (RX2) -> STM32 PA2 (TX / D1)
 *   ESP32 GND          -> STM32 GND
 */

#include <WiFi.h>

// Wi-Fi Configuration
const char* ssid     = "STM32_OTA_SERVER";     // Or your local Wi-Fi SSID
const char* password = "password123";          // Or your local Wi-Fi Password

#define TCP_PORT 8080
#define RX2_PIN  16
#define TX2_PIN  17
#define UART_BAUD 115200

WiFiServer server(TCP_PORT);
WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Hardware Serial 2 connected to STM32 UART2 (PA2/PA3)
  Serial2.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  Serial.println("\n[ESP32] Starting Wi-Fi Gateway...");
  
  // Create Access Point (or switch to WiFi.begin(ssid, password) for station mode)
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();

  Serial.print("[ESP32] Access Point Created! IP Address: ");
  Serial.println(IP);
  
  server.begin();
  Serial.printf("[ESP32] Listening for OTA connections on port %d...\n", TCP_PORT);
}

void loop() {
  // Check for incoming TCP client connection
  if (!client || !client.connected()) {
    client = server.available();
    if (client) {
      Serial.println("\n[ESP32] Client connected via Wi-Fi!");
    }
  }

  // 1. Forward data from WiFi client (Python host) -> STM32 UART
  if (client && client.connected()) {
    while (client.available()) {
      uint8_t b = client.read();
      Serial2.write(b);
    }
  }

  // 2. Forward data from STM32 UART -> WiFi client (Python host)
  if (Serial2.available()) {
    while (Serial2.available()) {
      uint8_t b = Serial2.read();
      if (client && client.connected()) {
        client.write(b);
      }
      // Also echo to ESP32 USB Serial for local hardware debugging
      Serial.write(b);
    }
  }
}
