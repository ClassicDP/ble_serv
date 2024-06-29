#include <Arduino.h>
#include <NimBLEDevice.h>
#include "MessageBase.h"
#include <json.hpp>
#include <queue>

// UUIDs
static BLEUUID serviceUUID("abcd");
static BLEUUID publicCharUUID("1234");

NimBLEAdvertisedDevice* advDevice = nullptr;
std::string uniqueUUID;
unsigned long autoincrement = 0;
QueueHandle_t incomingQueue;
QueueHandle_t outgoingQueue;

// Цветные логи
enum class LColor {
    Reset,
    Red,
    LightRed,
    Yellow,
    LightBlue,
    Green,
    LightCyan
};

void logColor(LColor color, const __FlashStringHelper* format, ...) {
    const char* colorCode;

    switch (color) {
        case LColor::Reset: colorCode = "\x1B[0m"; break;
        case LColor::Red: colorCode = "\x1B[31m"; break;
        case LColor::LightRed: colorCode = "\x1B[91m"; break;
        case LColor::Yellow: colorCode = "\x1B[93m"; break;
        case LColor::LightBlue: colorCode = "\x1B[94m"; break;
        case LColor::Green: colorCode = "\x1B[92m"; break;
        case LColor::LightCyan: colorCode = "\x1B[96m"; break;
        default: colorCode = "\x1B[0m"; break;
    }

    Serial.print("\n");  // Add newline at the beginning
    Serial.print(millis());
    Serial.print("ms: ");
    Serial.print(colorCode);

    // Create a buffer to hold the formatted string
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf_P(buffer, sizeof(buffer), reinterpret_cast<const char*>(format), args);
    va_end(args);

    Serial.print(buffer);
    Serial.print("\x1B[0m");  // Reset color
    Serial.println();
}

class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) {
            logColor(LColor::Green, F("Found device: %s"), advertisedDevice->toString().c_str());
            advDevice = advertisedDevice;
            NimBLEDevice::getScan()->stop();
        }
    }
};

void onNotify(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    std::string data((char*)pData, length);
    auto msg = MessageBase::createInstance(data);
    if (msg) {
        xQueueSend(incomingQueue, &msg, portMAX_DELAY);
    }
}

void sendOutgoingMessagesTask(void* parameter) {
    while (true) {
        MessageBase* msg;
        if (xQueueReceive(outgoingQueue, &msg, portMAX_DELAY) == pdTRUE) {
            logColor(LColor::LightBlue, F("Sending message: %s"), msg->serialize().c_str());
            // Here, you should add the code to send the message using the BLE characteristic
            // For example:
            // pUniqueChar->writeValue(msg->serialize(), false);
            delete msg;
        }
    }
}

void connectToServer() {
    NimBLEClient* pClient = NimBLEDevice::createClient();
    if (pClient->connect(advDevice)) {
        logColor(LColor::Green, F("Connected to server"));

        NimBLERemoteService* pService = pClient->getService(serviceUUID);
        if (pService) {
            NimBLERemoteCharacteristic* pPublicChar = pService->getCharacteristic(publicCharUUID);
            if (pPublicChar) {
                std::string data = pPublicChar->readValue();
                uniqueUUID = data;

                NimBLERemoteCharacteristic* pUniqueChar = pService->getCharacteristic(uniqueUUID);
                if (pUniqueChar) {
                    pUniqueChar->subscribe(true, [](NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
                        onNotify(pBLERemoteCharacteristic, pData, length, isNotify);
                    });
                    logColor(LColor::Green, F("Subscribed to unique characteristic"));
                }
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    logColor(LColor::Green, F("Starting setup..."));

    // Initialize NimBLEDevice
    NimBLEDevice::init("");

    // Set up BLE scan
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setActiveScan(true);
    pScan->start(30, false);

    // Create queues
    incomingQueue = xQueueCreate(10, sizeof(MessageBase*));
    outgoingQueue = xQueueCreate(10, sizeof(MessageBase*));

    // Set up the task to send outgoing messages
    xTaskCreate(sendOutgoingMessagesTask, "SendOutgoingMessagesTask", 8192, NULL, 1, NULL);
}

void loop() {
    // Main loop can be empty, tasks handle the work
}

