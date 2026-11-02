/*
 * ESP32 Wi-Fi to UART Transparent Bridge for STM32 OTA Updates
 *
 * This sketch runs on an ESP32, connects to a local Wi-Fi network,
 * listens on a TCP socket (port 8080), and bi-directionally bridges firmware
 * chunks between the Python host deployment tool and the STM32F446RE over USART1.
 *
 * Verified Hardware Pinout:
 *   ESP32 Pin 26 (RX2) <---> STM32 D8 (PA9 / USART1_TX)  [Red wire]
 *   ESP32 Pin 25 (TX2) <---> STM32 D2 (PA10 / USART1_RX) [Green wire]
 *   ESP32 GND          <---> STM32 GND                   [Yellow wire]
 *
 * Note on STM32 Nucleo-F446RE:
 *   Arduino headers D0 (PA3) and D1 (PA2) have factory solder bridges SB62/SB63
 *   OPEN by default, routing USART2 strictly to ST-LINK USB. Using STM32 D8 (PA9)
 *   and D2 (PA10) on USART1 avoids any board modifications!
 */

#include <WiFi.h>

// --- Wi-Fi Configuration ---
// Set to your local Wi-Fi credentials
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const int tcp_port = 8080;

// Pin Mapping for STM32 USART1 connection
#define STM32_RX_PIN 26  // ESP32 RX2 connects to STM32 D8 (PA9 TX)
#define STM32_TX_PIN 25  // ESP32 TX2 connects to STM32 D2 (PA10 RX)

WiFiServer server(tcp_port);
WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(300);

  // Initialize Serial2 with Pin 26 as RX and Pin 25 as TX at 115200 8N1
  Serial2.begin(115200, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);

  Serial.println("\n[ESP32] Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[ESP32] Wi-Fi Connected!");
  Serial.print("[ESP32] IP Address: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.printf("[ESP32] Listening for OTA on port %d...\n", tcp_port);
}

void loop() {
  // Check for incoming TCP client connection from Python deployment tool
  if (!client || !client.connected()) {
    client = server.available();
    if (client) {
      Serial.println("\n[ESP32] Host connected via Wi-Fi TCP!");
    }
  }

  // 1. Forward data from Wi-Fi TCP -> STM32 USART1
  if (client && client.connected()) {
    while (client.available()) {
      uint8_t byteIn = client.read();
      Serial2.write(byteIn);
    }
  }

  // 2. Forward data from USB Serial -> STM32 USART1 (optional debugging)
  while (Serial.available()) {
    uint8_t usbIn = Serial.read();
    Serial2.write(usbIn);
  }

  // 3. Forward data from STM32 USART1 -> Wi-Fi TCP & USB Serial
  while (Serial2.available()) {
    uint8_t byteOut = Serial2.read();
    if (client && client.connected()) {
      client.write(byteOut);
    }
    Serial.write(byteOut);
  }
}
