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

#include <unordered_map>
#include <unordered_set>


void registerClient (std::string mac);
void registerClientCharacteristic (std::string mac, std::string uuid, BLECharacteristic *characteristic);
void unregisterClient (std::string mac);
void unregisterClientCharacteristic (std::string mac, std::string uuid);
BLECharacteristic * findClientCharacteristic (std::string mac, std::string uuid);
BLECharacteristic * firstClientCharacteristic (std::string mac, std::string &uuid);



extern std::unordered_map<std::string, std::string > uniqueServers;

using json = nlohmann::json;

struct CreateCharacteristicCmd {
    std::string uuid;
    NimBLECharacteristic * pCharacteristic;
    bool alreadyCreated;
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

    void handlePublicCharacteristicRead(BLECharacteristic *pCharacteristic, std::string mac);

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

    void onRead(BLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) override;

private:
    BleLock *lock;
};

class UniqueCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid);

    void onWrite(BLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) override;

    void onRead(BLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) override;

private:
    BleLock *lock;
    std::string uuid;
};

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(BleLock *lock);

    void onConnect(BLEServer *pServer, ble_gap_conn_desc* desc) override;

    void onDisconnect(BLEServer *pServer, ble_gap_conn_desc* desc) override;

private:
    BleLock *lock;
};

#endif // BLE_LOCK_H
