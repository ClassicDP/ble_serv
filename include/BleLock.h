#ifndef BLE_LOCK_H
#define BLE_LOCK_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <NimBLEDevice.h>
#include <json.hpp>
#include <SPIFFS.h>
#include "MessageBase.h"
#include "ArduinoLog.h"

using json = nlohmann::json;

struct CreateCharacteristicCmd {
    std::string uuid;
    NimBLECharacteristic * pCharacteristic;
};

enum class LColor {
    Reset,
    Red,
    LightRed,
    Yellow,
    LightBlue,
    Green,
    LightCyan
};

void logColor(LColor color, const __FlashStringHelper* format, ...);

class BleLock {
public:
    explicit BleLock(std::string lockName);

    void setup();

    QueueHandle_t getOutgoingQueueHandle();

    void handlePublicCharacteristicRead(BLECharacteristic *pCharacteristic);

    void resumeAdvertising();

    void stopService();

    void startService();

    std::string generateUUID();

    void saveCharacteristicsToMemory();

    void loadCharacteristicsFromMemory();

    void initializeMutex();

    QueueHandle_t outgoingQueue;
    QueueHandle_t responseQueue;
    QueueHandle_t characteristicCreationQueue;
    std::unordered_map<std::string, BLECharacteristic *> uniqueCharacteristics;
    std::unordered_map<std::string, bool> confirmedCharacteristics;
    std::unordered_set<std::string> awaitingKeys;
    std::string memoryFilename;
    uint32_t autoincrement;
    std::string lockName;
    SemaphoreHandle_t bleMutex;
    BLEServer *pServer;
    BLEService *pService;
    BLECharacteristic *pPublicCharacteristic;

    QueueHandle_t jsonParsingQueue;
private:
    [[noreturn]] [[noreturn]] static void characteristicCreationTask(void *pvParameter);

    [[noreturn]] static void outgoingMessageTask(void *pvParameter);

    [[noreturn]] static void jsonParsingTask(void *pvParameter);

    MessageBase *request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const;

    QueueHandle_t getOutgoingQueueHandle() const;

};

class PublicCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    explicit PublicCharacteristicCallbacks(BleLock *lock);

    void onRead(BLECharacteristic *pCharacteristic) override;

private:
    BleLock *lock;
};

class UniqueCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid);

    void onWrite(BLECharacteristic *pCharacteristic) override;

    void onRead(BLECharacteristic *pCharacteristic) override;

private:
    BleLock *lock;
    std::string uuid;
};

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(BleLock *lock);

    void onConnect(BLEServer *pServer) override;

    void onDisconnect(BLEServer *pServer) override;

private:
    BleLock *lock;
};

#endif // BLE_LOCK_H
