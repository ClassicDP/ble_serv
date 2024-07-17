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
#include "SecureConnection.h"

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


    void handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string& mac);

    void resumeAdvertising();

    void stopService();

    void startService();

    std::string generateUUID();

    void saveCharacteristicsToMemory();

    void loadCharacteristicsFromMemory();

    void initializeMutex();

    bool confirm ()
    { 
        return true;
    }

    SecureConnection secureConnection;

    QueueHandle_t outgoingQueue;
    QueueHandle_t responseQueue;
    QueueHandle_t characteristicCreationQueue;
    std::unordered_map<std::string, BLECharacteristic *> uniqueCharacteristics;
    std::unordered_map<std::string, bool> confirmedCharacteristics;
    std::unordered_map<std::string, std::string> pairedDevices; // map for paired devices
    std::unordered_set<std::string> awaitingKeys;
    std::string memoryFilename;
    uint32_t autoincrement;
    std::string lockName;
    SemaphoreHandle_t bleMutex{};
    BLEServer *pServer;
    BLEService *pService;
    BLECharacteristic *pPublicCharacteristic;

    static std::unordered_map<std::string, std::string> messageMacBuff; // map for multypart messages
    static std::unordered_map<std::string, bool> messageMacState; // map for multypart messages
    static std::unordered_map<std::string, bool> messageControll; // map for multypart messages
    static void setWaiter (std::string who, bool val)
    {
        messageControll[who] = val;
    }
    static bool getWaiter(std::string who)
    {
        return messageControll[who];
    }

    static void waitForMessage (std::string who)
    {
        while (!getWaiter(who))
            vTaskDelay(10/portTICK_PERIOD_MS);
    }

    QueueHandle_t incomingQueue{};
    std::string getMacAddress ()
    {
        return macAddress;
    }

    MessageBase *request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const;

private:
    [[noreturn]] [[noreturn]] static void characteristicCreationTask(void *pvParameter);

    [[noreturn]] static void outgoingMessageTask(void *pvParameter);

    [[noreturn]] static void parsingIncomingTask(void *pvParameter);

    QueueHandle_t getOutgoingQueueHandle() const;
    static void processRequestTask(void *pvParameter);

    std::string macAddress;
};

class PublicCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    void onRead(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override;


    explicit PublicCharacteristicCallbacks(BleLock *lock);
private:

    BleLock *lock;
};

class UniqueCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid);

    void onWrite(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override;


private:
    BleLock *lock;
    std::string uuid;
};

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(BleLock *lock);


private:
    BleLock *lock;

    void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override;

    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override;
};

#endif // BLE_LOCK_H
