/*
   Programme de lecture, de décodage et d'affichage des messages Railcom©
   Affiche l'adresse d'un décodeur DCC sur un afficheur LED 7 segments (4 digits)
   Fonctionne exclusivement sur ESP32
   © christophe bobille - www.locoduino.org 11/2022-2025

   Pour plus d'infos : https://forum.locoduino.org/index.php?topic=1352.msg15944#msg15944

   Architecture:
   - 4 FreeRTOS tasks: Data reception, decoding, intermediate output, and display multiplexing
   - 2 Queues for task communication
   - Ring buffer to filter address noise
   - Hardware: Railcom RX on GPIO4, 7-segment display multiplexed on 4 cathodes/7 anodes

   Dependencies: locoduino/RingBuffer@^1.0.3
*/

#ifndef ARDUINO_ARCH_ESP32
#error "Select an ESP32 board"
#endif

#include <Arduino.h>
#include <RingBuf.h> // -> https://github.com/Locoduino/RingBuffer

// ==== Version Info ====
#define VERSION "v 4.0"
#define PROJECT "Railcom Detector/ Display (ESP32 freeRTOS)"
#define AUTHOR "christophe BOBILLE Locoduino : christophe.bobille@gmail.com"

// ==== Railcom/Serial Pins ====
constexpr uint8_t RAILCOM_RX_PIN = 4;    // GPIO4: Railcom RX input
constexpr uint8_t RAILCOM_TX_PIN = 255;  // 255 : Not used, but needed for Serial1

// ==== Display Pins (7-segment, 4 digits) ====
constexpr uint8_t NUM_DIGITS = 4;
constexpr uint8_t NUM_SEG = 7;
constexpr uint8_t DIGIT_PINS[NUM_DIGITS] = { 26, 25, 12, 14 };         // Cathodes (digit select)
constexpr uint8_t SEG_PINS[NUM_SEG] = { 13, 27, 18, 19, 21, 22, 23 };  // Anodes (segment select)
constexpr uint8_t SEG_MAP[10] = {
  B1111011,   // 0
  B1001000,   // 1
  B01110101,  // 2
  B1011101,   // 3
  B1001110,   // 4
  B0011111,   // 5
  B0111111,   // 6
  B1001001,   // 7
  B1111111,   // 8
  B1011111    // 9
};

// ==== FreeRTOS Queues ====
constexpr uint8_t QUEUE_SIZE_0 = 20;
constexpr uint8_t QUEUE_SIZE_1 = 20;
QueueHandle_t xQueue_0, xQueue_1;

// ==== Address Buffer (Noise Filtering) ====
#include <RingBuf.h>
constexpr uint16_t NB_ADDRESS_TO_COMPARE = 20;
RingBuf<uint16_t, NB_ADDRESS_TO_COMPARE> buffer;

// ==== Railcom Decoding Constants ====
#define CH1_ADR_LOW (1 << 2)
#define CH1_ADR_HIGH (1 << 3)

// ==== Function Prototypes ====
void receiveData(void *p);
void parseData(void *p);
void printAddress(void *p);
void displayAddAddress(void *p);

// ==== Railcom 4-out-of-8 decode table (optimized for DCC address extraction) ====
const uint8_t decodeArray[] = {
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 64, 255, 255, 255, 255, 255, 255, 255, 51, 255, 255, 255, 52,
  255, 53, 54, 255, 255, 255, 255, 255, 255, 255, 255, 58, 255, 255, 255, 59, 255, 60, 55, 255, 255, 255, 255, 63, 255, 61, 56, 255, 255, 62,
  57, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 36, 255, 255, 255, 35, 255, 34, 33, 255, 255, 255, 255, 31, 255, 30, 32, 255,
  255, 29, 28, 255, 27, 255, 255, 255, 255, 255, 255, 25, 255, 24, 26, 255, 255, 23, 22, 255, 21, 255, 255, 255, 255, 37, 20, 255, 19, 255, 255,
  255, 50, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 14, 255, 13, 12, 255, 255, 255, 255, 10, 255,
  9, 11, 255, 255, 8, 7, 255, 6, 255, 255, 255, 255, 255, 255, 4, 255, 3, 5, 255, 255, 2, 1, 255, 0, 255, 255, 255, 255, 15, 16, 255, 17, 255, 255,
  255, 18, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 43, 48, 255, 255, 42, 47, 255, 49, 255, 255, 255, 255, 41, 46, 255, 45, 255, 255,
  255, 44, 255, 255, 255, 255, 255, 255, 255, 255, 66, 40, 255, 39, 255, 255, 255, 38, 255, 255, 255, 255, 255, 255, 255, 65, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

// ==== Task: Receives Railcom bytes from Serial1 and sends them to xQueue_0 ====
// Each message starts with '\0', followed by two data bytes.
void receiveData(void *p) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint8_t inByte = 0;
  uint8_t compt = 0;
  for (;;) {
    while (Serial1.available() > 0) {
      inByte = (compt == 0) ? '\0' : (uint8_t)Serial1.read();
      if (compt < 3) {
        xQueueSend(xQueue_0, &inByte, 0);  // Send start marker + 2 data bytes
      }
      compt++;
    }
    compt = 0;
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1));  // polling delay
  }
}

// ==== Task: Parses Railcom messages, decodes address, filters via ring buffer ====
// Sends stable address to xQueue_1
void parseData(void *p) {
  bool start = false;
  uint8_t inByte = 0;
  uint8_t rxArray[2]{ 0 };
  uint8_t rxArrayCnt = 0;
  uint8_t dccAddr[2]{ 0 };
  int16_t address = 0;
  TickType_t xLastWakeTime = xTaskGetTickCount();

  auto check_4_8_code = [&](uint8_t &val) -> bool {
    if (decodeArray[val] < 255) {
      val = decodeArray[val];
      return true;
    }
    return false;
  };

  for (;;) {
    // Wait for start marker '\0'
    do {
      xQueueReceive(xQueue_0, &inByte, portMAX_DELAY);
      if (inByte == '\0')
        start = true;
    } while (!start);
    start = false;

    // Read and decode 2 data bytes
    rxArrayCnt = 0;
    for (uint8_t i = 0; i < 2; i++) {
      if (xQueueReceive(xQueue_0, &inByte, portMAX_DELAY) == pdPASS) {
        if (inByte > 0x0F && inByte < 0xF0 && check_4_8_code(inByte)) {
          rxArray[rxArrayCnt++] = inByte;
        }
      }
    }

    // Valid message?
    if (rxArrayCnt == 2) {
      // Decode DCC address (short or long)
      if (rxArray[0] & CH1_ADR_HIGH)
        dccAddr[0] = rxArray[1] | (rxArray[0] << 6);
      if (rxArray[0] & CH1_ADR_LOW)
        dccAddr[1] = rxArray[1] | (rxArray[0] << 6);

      address = (dccAddr[1] - 128) << 8;
      if (address < 0)
        address = dccAddr[0];
      else
        address += dccAddr[0];

      // Ring buffer for address stability (filtering noise)
      uint16_t oldest;
      buffer.pop(oldest);
      buffer.push(address);
      bool stable = true;
      for (size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] != address) {
          stable = false;
          //vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
          break;
        }
      }
      if (stable) {
        xQueueSend(xQueue_1, &address, portMAX_DELAY);
      }
    }
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
  }
}

// ==== Utility: Split address into 4 digits ====
void splitDigits(uint16_t value, uint8_t *digits) {
  for (uint8_t i = 0; i < NUM_DIGITS; ++i) {
    digits[i] = value % 10;
    value /= 10;
  }
}

// ==== Task: Drives the 4-digit 7-segment display ====
void displayAddAddress(void *p) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint8_t digits[NUM_DIGITS] = { 0 };

  // Init pins
  for (auto pin : DIGIT_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);  // All digits off (for NPN transistor: LOW=on)
  }
  for (auto pin : SEG_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  for (;;) {
    // Get address for display (non-blocking)
    uint16_t address = 0;
    xQueueReceive(xQueue_1, &address, pdMS_TO_TICKS(0));
    splitDigits(address, digits);

    // Multiplex display: show each digit in turn
    for (uint8_t j = 0; j < NUM_DIGITS; ++j) {
      for (auto pin : DIGIT_PINS) digitalWrite(pin, LOW);

      uint8_t digitIndex = j;  // unités, dizaines, centaines, milliers

      bool show =
        (digitIndex == 0) || (digitIndex == 1 && address >= 10) || (digitIndex == 2 && address >= 100) || (digitIndex == 3 && address >= 1000);

      for (uint8_t i = 0; i < NUM_SEG; ++i) {
        bool seg_on = show ? ((SEG_MAP[digits[digitIndex]] & (1 << i)) != 0) : false;
        digitalWrite(SEG_PINS[i], seg_on);
      }

      digitalWrite(DIGIT_PINS[j], HIGH);
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5));  // Persistence for this digit
      digitalWrite(DIGIT_PINS[j], LOW);
    }
  }
}

// ==== Setup: Initializes serial, queues, tasks ====
void setup() {
  Serial.begin(115200);
  Serial.printf("\n\nProject :    %s", PROJECT);
  Serial.printf("\nVersion :      %s", VERSION);
  Serial.printf("\nAuthor :       %s", AUTHOR);
  Serial.printf("\nFichier :      %s", __FILE__);
  Serial.printf("\nCompiled :     %s", __DATE__);
  Serial.printf(" - %s\n\n", __TIME__);

  Serial1.begin(250000, SERIAL_8N1, RAILCOM_RX_PIN, RAILCOM_TX_PIN);

  // Initialize ring buffer
  constexpr byte initVal = 0;
  for (size_t i = 0; i < buffer.size(); ++i)
    buffer.push(initVal);

  // Create queues
  xQueue_0 = xQueueCreate(QUEUE_SIZE_0, sizeof(uint8_t));
  xQueue_1 = xQueueCreate(QUEUE_SIZE_1, sizeof(uint16_t));

  // Start tasks
  xTaskCreatePinnedToCore(receiveData, "ReceiveData", 2048, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(parseData, "ParseData", 2048, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(displayAddAddress, "DisplayAddress", 2048, NULL, 5, NULL, 0);
}

void loop() {}
