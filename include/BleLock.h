#ifndef BLELOCK_H
#define BLELOCK_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <SPIFFS.h>
#include <map>
#include <string>
#include <utility>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class BleLock;

class PublicCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    explicit PublicCharacteristicCallbacks(BleLock* lock);
    void onRead(BLECharacteristic* pCharacteristic) override;

private:
    BleLock* lock;
};

class UniqueCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    UniqueCharacteristicCallbacks(BleLock* lock, std::string uuid);
    void onWrite(BLECharacteristic* pCharacteristic) override;
    void onRead(BLECharacteristic* pCharacteristic) override;

private:
    BleLock* lock;
    std::string uuid;
};

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(BleLock* lock);
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;

private:
    BleLock* lock;
};

struct ResponseMessage {
    std::string uuid;
    std::string message;
};

class BleLock {
public:
    explicit BleLock(std::string lockName);
    void setup();

    [[noreturn]] static void characteristicCreationTask(void* pvParameter);

    [[noreturn]] static void responseMessageTask(void* pvParameter);

private:
    BLEServer* pServer;
    BLEService* pService;
    BLECharacteristic* pPublicCharacteristic;
    std::string generateUUID();
    void handlePublicCharacteristicRead(BLECharacteristic* pCharacteristic);

    std::string lockName;
    std::map<std::string, BLECharacteristic*> uniqueCharacteristics;
    std::map<std::string, bool> confirmedCharacteristics;
    std::string memoryFilename;
    uint16_t autoincrement;
    QueueHandle_t characteristicCreationQueue;
    QueueHandle_t responseMessageQueue;
    SemaphoreHandle_t bleMutex;  // Mutex for thread-safe operations

    void loadCharacteristicsFromMemory();
    void saveCharacteristicsToMemory();
    void resumeAdvertising();
    void stopService();
    void startService();

    void initializeMutex();
    friend class PublicCharacteristicCallbacks;
    friend class UniqueCharacteristicCallbacks;
    friend class ServerCallbacks;
};

#endif // BLELOCK_H
