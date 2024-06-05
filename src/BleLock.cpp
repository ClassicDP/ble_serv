#include "BleLock.h"
#include "MessageBase.h"

PublicCharacteristicCallbacks::PublicCharacteristicCallbacks(BleLock *lock) : lock(lock) {}

void PublicCharacteristicCallbacks::onRead(BLECharacteristic *pCharacteristic) {
    lock->handlePublicCharacteristicRead(pCharacteristic);
}

UniqueCharacteristicCallbacks::UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid)
        : lock(lock), uuid(std::move(uuid)) {}

void UniqueCharacteristicCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
    // Подтверждение получения характеристики ключом
    if (!lock->confirmedCharacteristics[uuid]) {
        lock->confirmedCharacteristics[uuid] = true;
        lock->saveCharacteristicsToMemory();
    }

    // Обработка сообщения от клиента
    std::string receivedMessage = pCharacteristic->getValue();
    auto msg = MessageBase::createInstance(receivedMessage);
    if (msg) {
        msg->sourceAddress = uuid.c_str();
        Serial.printf("Received request from: %s with type: %s\n", msg->sourceAddress.c_str(), msg->type.c_str());

        // Обработка запроса
        MessageBase *responseMessage = msg->processRequest(lock);

        // Отправка исходящего сообщения
        if (responseMessage) {
            xQueueSend(lock->outgoingQueue, &responseMessage, portMAX_DELAY);
        } else {
            auto responseMessageStr = new std::string(receivedMessage);
            xQueueSend(lock->responseQueue, &responseMessageStr, portMAX_DELAY);
        }
    } else {
        Serial.println("Failed to create message instance");
    }
}

MessageBase* BleLock::request(MessageBase* requestMessage, const String& destAddr, uint32_t timeout = portMAX_DELAY) {
    // Установка адресов
    requestMessage->sourceAddress = "local_address";  // Установите ваш локальный адрес
    requestMessage->destinationAddress = destAddr;

    // Отправка запроса через очередь
    if (xQueueSend(outgoingQueue, &requestMessage, portMAX_DELAY) != pdPASS) {
        Serial.println("Failed to send request to the outgoing queue");
        return nullptr;
    }

    std::string *receivedMessage;
    // Получение ответа
    if (xQueueReceive(responseQueue, &receivedMessage, pdMS_TO_TICKS(timeout)) == pdTRUE) {
        MessageBase* instance = MessageBase::createInstance(*receivedMessage);
        delete receivedMessage;
        return instance;
    }
    return nullptr;
}


void UniqueCharacteristicCallbacks::onRead(BLECharacteristic *pCharacteristic) {
    Serial.println("UniqueCharacteristicCallbacks::onRead");
    BLECharacteristicCallbacks::onRead(pCharacteristic);
}

ServerCallbacks::ServerCallbacks(BleLock *lock) : lock(lock) {}

void ServerCallbacks::onConnect(BLEServer *pServer) {
    Serial.println("Device connected");
}

void ServerCallbacks::onDisconnect(BLEServer *pServer) {
    Serial.println("Device disconnected");
    lock->resumeAdvertising();
}

BleLock::BleLock(std::string lockName)
        : lockName(std::move(lockName)), pServer(nullptr), pService(nullptr), pPublicCharacteristic(nullptr),
          autoincrement(0) {
    memoryFilename = "/ble_lock_memory.json";
    Serial.println("xQueueCreate ");
    characteristicCreationQueue = xQueueCreate(10, sizeof(std::string *));
    outgoingQueue = xQueueCreate(10, sizeof(MessageBase *));
    responseQueue = xQueueCreate(10, sizeof(std::string *));
    Serial.println("initializeMutex ");
    initializeMutex();
    Serial.println("done ");

}

void BleLock::setup() {
    Serial.println("Starting BLE setup...");
    loadCharacteristicsFromMemory();
    Serial.println("loadCharacteristicsFromMemory");
    BLEDevice::init(lockName);
    Serial.println("BLEDevice::init");
    pServer = BLEDevice::createServer();
    if (!pServer) {
        Serial.println("Failed to create BLE server");
        return;
    }
    pServer->setCallbacks(new ServerCallbacks(this));

    pService = pServer->createService(BLEUUID((uint16_t) 0xABCD));
    if (!pService) {
        Serial.println("Failed to create service");
        return;
    }

    pPublicCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t) 0xABCD),
            BLECharacteristic::PROPERTY_READ
    );
    pPublicCharacteristic->setCallbacks(new PublicCharacteristicCallbacks(this));

    pService->start();
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->start();

    Serial.println("BLE setup complete");

    xTaskCreate(characteristicCreationTask, "CharacteristicCreationTask", 4096, this, 1, nullptr);
    xTaskCreate(outgoingMessageTask, "OutgoingMessageTask", 4096, this, 1, nullptr);
}


QueueHandle_t BleLock::getOutgoingQueueHandle() {
    return outgoingQueue;
}

void BleLock::initializeMutex() {
    bleMutex = xSemaphoreCreateMutex();
    if (bleMutex == nullptr) {
        Serial.println("Failed to create the mutex");
    }
}

std::string BleLock::generateUUID() {
    char uuid[37];
    sprintf(uuid, "%08x-%04x-%04x-%04x-%012x",
            esp_random(), autoincrement++ & 0xFFFF, (esp_random() & 0x0FFF) | 0x4000,
            (esp_random() & 0x3FFF) | 0x8000, esp_random());

    saveCharacteristicsToMemory(); // Сохранить автоприращение после генерации UUID

    return {uuid};
}

void BleLock::handlePublicCharacteristicRead(BLECharacteristic *pCharacteristic) {
    std::string newUUID = generateUUID();
    auto *uuid = new std::string(newUUID);
    xQueueSend(characteristicCreationQueue, &uuid, portMAX_DELAY);
    pCharacteristic->setValue(newUUID);
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
                for (JsonPair kv: characteristics) {
                    std::string uuid = kv.key().c_str();
                    bool confirmed = kv.value().as<bool>();
                    BLECharacteristic *characteristic = pService->createCharacteristic(
                            BLEUUID::fromString(uuid),
                            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
                            BLECharacteristic::PROPERTY_NOTIFY
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
    for (const auto &pair: uniqueCharacteristics) {
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

void BleLock::resumeAdvertising() {
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    if (xSemaphoreTake(bleMutex, portMAX_DELAY) == pdTRUE) {
        pAdvertising->start();
        xSemaphoreGive(bleMutex);
    }
    Serial.println("Advertising resumed");
}

void BleLock::stopService() {
    if (xSemaphoreTake(bleMutex, portMAX_DELAY) == pdTRUE) {
        pService->stop();
        xSemaphoreGive(bleMutex);
    }
}

void BleLock::startService() {
    if (xSemaphoreTake(bleMutex, portMAX_DELAY) == pdTRUE) {
        pService->start();
        BLEAdvertising *pAdvertising = pServer->getAdvertising();
        pAdvertising->start();
        Serial.println("Service restarted and advertising resumed");
        xSemaphoreGive(bleMutex);
    }
}




[[noreturn]] void BleLock::characteristicCreationTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    std::string *uuid;
    while (true) {
        if (xQueueReceive(bleLock->characteristicCreationQueue, &uuid, portMAX_DELAY) == pdTRUE) {
            bleLock->stopService();
            Serial.printf(" BleLock::characteristicCreationTask uuid to create: %s \n", uuid->c_str());
            BLECharacteristic *newCharacteristic = bleLock->pService->createCharacteristic(
                    BLEUUID::fromString(*uuid),
                    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
                    BLECharacteristic::PROPERTY_NOTIFY
            );
            Serial.printf(" - createCharacteristic \n");
            newCharacteristic->setCallbacks(new UniqueCharacteristicCallbacks(bleLock, *uuid));
            Serial.printf(" - setCallbacks \n");
            bleLock->uniqueCharacteristics[*uuid] = newCharacteristic;
            bleLock->confirmedCharacteristics[*uuid] = false;

            delete uuid;  // Free the allocated memory

            bleLock->saveCharacteristicsToMemory();
            Serial.printf(" - saveCharacteristicsToMemory \n");
            bleLock->startService();
            Serial.printf(" - startService \n");
            Serial.flush();
        }
    }
}

[[noreturn]] void BleLock::outgoingMessageTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    MessageBase *responseMessage;
    while (true) {
        if (xQueueReceive(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) == pdTRUE) {
            Serial.printf(" BleLock::responseMessageTask msg: %s %s \n", responseMessage->destinationAddress.c_str(),
                          responseMessage->type.c_str());
            if (bleLock->uniqueCharacteristics.find(responseMessage->destinationAddress.c_str()) != bleLock->uniqueCharacteristics.end()) {
                BLECharacteristic *characteristic = bleLock->uniqueCharacteristics[responseMessage->destinationAddress.c_str()];
                characteristic->setValue(responseMessage->serialize().c_str());
                characteristic->notify();
            }
            delete responseMessage;  // Free the allocated memory
            bleLock->resumeAdvertising();
        }
    }
}

