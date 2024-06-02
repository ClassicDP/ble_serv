#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <esp_system.h> // Для esp_random()
#include <SPIFFS.h>
#include <map>
#include <string>
#include <utility>

class BleLock;

class PublicCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    explicit PublicCharacteristicCallbacks(BleLock* lock) : lock(lock) {}
    void onRead(BLECharacteristic* pCharacteristic) override;

private:
    BleLock* lock;
};

class UniqueCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    UniqueCharacteristicCallbacks(BleLock* lock, std::string  uuid) : lock(lock), uuid(std::move(uuid)) {}
    void onWrite(BLECharacteristic* pCharacteristic) override;

private:
    BleLock* lock;
    std::string uuid;
};

class BleLock {
public:
    explicit BleLock(std::string  lockName);
    void setup();
    void loop();

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

    void loadCharacteristicsFromMemory();
    void saveCharacteristicsToMemory();

    friend class PublicCharacteristicCallbacks;
    friend class UniqueCharacteristicCallbacks;
};

BleLock::BleLock(std::string  lockName)
        : lockName(std::move(lockName)), pServer(nullptr), pService(nullptr), pPublicCharacteristic(nullptr), autoincrement(0) {
    memoryFilename = "/ble_lock_memory.json";
}

void BleLock::setup() {
    BLEDevice::init(lockName);
    pServer = BLEDevice::createServer();
    pService = pServer->createService(BLEUUID((uint16_t)0xABCD));

    pPublicCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0xABCD),
            BLECharacteristic::PROPERTY_READ
    );

    pPublicCharacteristic->setCallbacks(new PublicCharacteristicCallbacks(this));

    pService->start();
    BLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->start();

    loadCharacteristicsFromMemory();
}

void BleLock::loop() {
    // Main loop
}

std::string BleLock::generateUUID() {
    char uuid[37];
    sprintf(uuid, "%08x-%04x-%04x-%04x-%012x",
            esp_random(), autoincrement++ & 0xFFFF, (esp_random() & 0x0FFF) | 0x4000,
            (esp_random() & 0x3FFF) | 0x8000, esp_random());

    saveCharacteristicsToMemory(); // Сохранить автоприращение после генерации UUID

    return {uuid};
}

void BleLock::handlePublicCharacteristicRead(BLECharacteristic* pCharacteristic) {
    std::string newUUID = generateUUID();
    BLECharacteristic* newCharacteristic = pService->createCharacteristic(
            BLEUUID::fromString(newUUID),
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );

    newCharacteristic->setCallbacks(new UniqueCharacteristicCallbacks(this, newUUID));

    uniqueCharacteristics[newUUID] = newCharacteristic;
    confirmedCharacteristics[newUUID] = false;
    pCharacteristic->setValue(newUUID);
    saveCharacteristicsToMemory();
}

void PublicCharacteristicCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    lock->handlePublicCharacteristicRead(pCharacteristic);
}

void UniqueCharacteristicCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
    // Подтверждение получения характеристики ключом
    lock->confirmedCharacteristics[uuid] = true;
    lock->saveCharacteristicsToMemory();
}

void BleLock::loadCharacteristicsFromMemory() {
    if (SPIFFS.begin(true)) {
        File file = SPIFFS.open(memoryFilename.c_str(), FILE_READ);
        if (file) {
            size_t size = file.size();
            std::unique_ptr<char[]> buf(new char[size]);
            file.readBytes(buf.get(), size);
            file.close();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, buf.get());
            if (!error) {
                autoincrement = doc["autoincrement"] | 0;
                JsonObject characteristics = doc["characteristics"].as<JsonObject>();
                for (JsonPair kv : characteristics) {
                    std::string uuid = kv.key().c_str();
                    bool confirmed = kv.value().as<bool>();
                    BLECharacteristic* characteristic = pService->createCharacteristic(
                            BLEUUID::fromString(uuid),
                            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
                    );

                    characteristic->setCallbacks(new UniqueCharacteristicCallbacks(this, uuid));
                    uniqueCharacteristics[uuid] = characteristic;
                    confirmedCharacteristics[uuid] = confirmed;
                }
            }
        }
    }
}

void BleLock::saveCharacteristicsToMemory() {
    JsonDocument doc;
    doc["autoincrement"] = autoincrement;

    JsonObject characteristics = doc["characteristics"].to<JsonObject>();
    for (const auto& pair : uniqueCharacteristics) {
        if (confirmedCharacteristics[pair.first]) {
            characteristics[pair.first] = true;
        }
    }

    File file = SPIFFS.open(memoryFilename.c_str(), FILE_WRITE);
    if (file) {
        serializeJson(doc, file);
        file.close();
    }
}
