#include "BleLock.h"
#include <iostream>

// Callbacks Implementation
PublicCharacteristicCallbacks::PublicCharacteristicCallbacks(BleLock *lock) : lock(lock) {}

void PublicCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic) {
    Serial.println("PublicCharacteristicCallbacks::onRead called");
    lock->handlePublicCharacteristicRead(pCharacteristic);
}

UniqueCharacteristicCallbacks::UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid)
        : lock(lock), uuid(std::move(uuid)) {}

void UniqueCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic) {
    Serial.println("UniqueCharacteristicCallbacks::onWrite called");

    std::string receivedMessage = pCharacteristic->getValue();
    Serial.printf("Received message: %s\n", receivedMessage.c_str());

    auto msg = MessageBase::createInstance(receivedMessage);
    if (msg) {
        msg->sourceAddress = uuid;
        Serial.printf("Received request from: %s with type: %s\n", msg->sourceAddress.c_str(), msg->type.c_str());

        MessageBase *responseMessage = msg->processRequest(lock);
        delete msg;

        if (responseMessage) {
            Serial.println("Sending response message to outgoing queue");
            if (xQueueSend(lock->outgoingQueue, &responseMessage, portMAX_DELAY) != pdPASS) {
                Serial.println("Failed to send response message to outgoing queue");
                delete responseMessage;
            }
        } else {
            auto responseMessageStr = new std::string(receivedMessage);
            Serial.println("Sending response message string to response queue");
            if (xQueueSend(lock->responseQueue, &responseMessageStr, portMAX_DELAY) != pdPASS) {
                Serial.println("Failed to send response message string to response queue");
                delete responseMessageStr;
            }
        }
    } else {
        Serial.println("Failed to create message instance");
    }
}

MessageBase *BleLock::request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const {
    requestMessage->sourceAddress = "local_address";
    requestMessage->destinationAddress = destAddr;

    if (xQueueSend(outgoingQueue, &requestMessage, portMAX_DELAY) != pdPASS) {
        Serial.println("Failed to send request to the outgoing queue");
        return nullptr;
    }

    std::string *receivedMessage;
    if (xQueueReceive(responseQueue, &receivedMessage, pdMS_TO_TICKS(timeout)) == pdTRUE) {
        MessageBase *instance = MessageBase::createInstance(*receivedMessage);
        delete receivedMessage;
        return instance;
    }
    return nullptr;
}

void UniqueCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic) {
    Serial.println("UniqueCharacteristicCallbacks::onRead called");
    NimBLECharacteristicCallbacks::onRead(pCharacteristic);
}

ServerCallbacks::ServerCallbacks(BleLock *lock) : lock(lock) {}

void ServerCallbacks::onConnect(NimBLEServer *pServer) {
    Serial.println("Device connected");
}

void ServerCallbacks::onDisconnect(NimBLEServer *pServer) {
    Serial.println("Device disconnected");
    lock->resumeAdvertising();
}

BleLock::BleLock(std::string lockName)
        : lockName(std::move(lockName)), pServer(nullptr), pService(nullptr), pPublicCharacteristic(nullptr),
          autoincrement(0) {
    memoryFilename = "/ble_lock_memory.json";
    Serial.println("xQueueCreate ");
    characteristicCreationQueue = xQueueCreate(10, sizeof(char *)); // Queue to hold char* pointers
    outgoingQueue = xQueueCreate(10, sizeof(MessageBase *));
    responseQueue = xQueueCreate(10, sizeof(std::string *));
    Serial.println("initializeMutex ");
    initializeMutex();
    Serial.println("done ");
}

void BleLock::setup() {
    Serial.println("Starting BLE setup...");

    BLEDevice::init(lockName);
    Serial.println("BLEDevice::init completed");

    pServer = BLEDevice::createServer();
    if (!pServer) {
        Serial.println("Failed to create BLE server");
        return;
    }
    Serial.println("BLE server created");

    pServer->setCallbacks(new ServerCallbacks(this));
    Serial.println("Server callbacks set");

    pService = pServer->createService(BLEUUID((uint16_t) 0xABCD));
    if (!pService) {
        Serial.println("Failed to create service");
        return;
    }
    Serial.println("Service created");

    pPublicCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t) 0x1234), // Example UUID
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    if (!pPublicCharacteristic) {
        Serial.println("Failed to create public characteristic");
        return;
    }
    Serial.println("Public characteristic created");

    pPublicCharacteristic->setCallbacks(new PublicCharacteristicCallbacks(this));
    Serial.println("Public characteristic callbacks set");

    pService->start();
    Serial.println("Service started");

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!pAdvertising) {
        Serial.println("Failed to get advertising");
        return;
    }
    Serial.println("Advertising started");
    pAdvertising->start();

    Serial.println("BLE setup complete");

    xTaskCreate(characteristicCreationTask, "CharacteristicCreationTask", 8192, this, 1, nullptr);
    Serial.println("CharacteristicCreationTask created");
    xTaskCreate(outgoingMessageTask, "OutgoingMessageTask", 4096, this, 1, nullptr);
    Serial.println("OutgoingMessageTask created");
}

QueueHandle_t BleLock::getOutgoingQueueHandle() const {
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
    snprintf(uuid, sizeof(uuid),
             "%08x-%04x-%04x-%04x-%012x",
             esp_random(),
             (autoincrement++ & 0xFFFF),
             (esp_random() & 0x0FFF) | 0x4000,
             (esp_random() & 0x3FFF) | 0x8000,
             esp_random());
    saveCharacteristicsToMemory();
    return {uuid};
}

void BleLock::handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic) {
    std::string newUUID = generateUUID();
    Serial.printf("Generated new UUID: %s\n", newUUID.c_str());

    // Allocate memory for the UUID and copy the string
    char *uuidBuffer = new char[newUUID.length() + 1];
    strcpy(uuidBuffer, newUUID.c_str());

    if (xQueueSend(characteristicCreationQueue, &uuidBuffer, portMAX_DELAY) != pdPASS) {
        Serial.println("Failed to send UUID to characteristicCreationQueue");
        delete[] uuidBuffer; // Ensure memory is freed if sending fails
    }

    pCharacteristic->setValue(newUUID);
}

void BleLock::loadCharacteristicsFromMemory() {
    Serial.println("Attempting to mount SPIFFS...");

    if (SPIFFS.begin(true)) {
        Serial.println("SPIFFS mounted successfully.");
        File file = SPIFFS.open(memoryFilename.c_str(), FILE_READ);
        if (file) {
            Serial.println("File opened successfully. Parsing JSON...");

            json doc;
            try {
                doc = json::parse(file.readString().c_str());
            } catch (json::parse_error &e) {
                Serial.printf("Failed to parse JSON: %s\n", e.what());
                file.close();
                return;
            }

            autoincrement = doc.value("autoincrement", 0);
            Serial.printf("Autoincrement loaded: %d\n", autoincrement);

            json characteristics = doc["characteristics"];
            for (auto &kv: characteristics.items()) {
                std::string uuid = kv.key();
                bool confirmed = kv.value().get<bool>();
                Serial.printf("Loading characteristic with UUID: %s, confirmed: %d\n", uuid.c_str(), confirmed);

                if (!pService) {
                    Serial.println("pService is null. Cannot create characteristic.");
                    continue;
                }

                NimBLECharacteristic *characteristic = pService->createCharacteristic(
                        NimBLEUUID::fromString(uuid),
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
                );

                if (!characteristic) {
                    Serial.printf("Failed to create characteristic with UUID: %s\n", uuid.c_str());
                    continue;
                }

                characteristic->setCallbacks(new UniqueCharacteristicCallbacks(this, uuid));
                uniqueCharacteristics[uuid] = characteristic;
                confirmedCharacteristics[uuid] = confirmed;
            }

            file.close();
            Serial.println("All characteristics loaded successfully.");
        } else {
            Serial.println("Failed to open file. File may not exist.");
        }
    } else {
        Serial.println("SPIFFS mount failed, attempting to format...");
        if (SPIFFS.format()) {
            Serial.println("SPIFFS formatted successfully.");
            if (SPIFFS.begin()) {
                Serial.println("SPIFFS mounted successfully after formatting.");
            } else {
                Serial.println("Failed to mount SPIFFS after formatting.");
            }
        } else {
            Serial.println("SPIFFS formatting failed.");
        }
    }
}

void BleLock::saveCharacteristicsToMemory() {
    json doc;
    doc["autoincrement"] = autoincrement;

    json characteristics;
    for (const auto &pair: uniqueCharacteristics) {
        if (confirmedCharacteristics[pair.first]) {
            characteristics[pair.first] = true;
        }
    }
    doc["characteristics"] = characteristics;

    File file = SPIFFS.open(memoryFilename.c_str(), FILE_WRITE);
    if (file) {
        file.print(doc.dump().c_str());
        Serial.printf("saving.. %s \n", doc.dump().c_str());
        file.close();
    }
}

void BleLock::resumeAdvertising() {
    Serial.println("Attempting to resume advertising...");
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->start();
    Serial.println("Advertising resumed");
}

void BleLock::stopService() {
    Serial.println("Attempting to stop Advertising...");
    pServer->stopAdvertising();
    Serial.println("Advertising stopped");
}

void BleLock::startService() {
    Serial.println("Attempting to start service...");
    pService->start();
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    Serial.println("Attempting to start Advertising...");
    pAdvertising->start();
    Serial.println("Each started");
}



[[noreturn]] void BleLock::characteristicCreationTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    char *uuid;

    while (true) {
        Serial.println("characteristicCreationTask: Waiting to receive UUID from queue...");

        if (xQueueReceive(bleLock->characteristicCreationQueue, &uuid, portMAX_DELAY) == pdTRUE) {
            // Convert received char* to std::string
            std::string uuidStr(uuid);
            Serial.printf("BleLock::characteristicCreationTask uuid to create: %s\n", uuidStr.c_str());

            // Lock the mutex for service operations
            Serial.println("characteristicCreationTask: Waiting for Mutex");
            xSemaphoreTake(bleLock->bleMutex, portMAX_DELAY);
            Serial.println("characteristicCreationTask: Mutex lock");

//            bleLock->stopService();
//            Serial.println(" - stopService");

            NimBLECharacteristic *newCharacteristic = bleLock->pService->createCharacteristic(
                    NimBLEUUID::fromString(uuidStr),
                    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
            );
            Serial.println(" - createCharacteristic");

            newCharacteristic->setCallbacks(new UniqueCharacteristicCallbacks(bleLock, uuidStr));
            Serial.println(" - setCallbacks");

            bleLock->uniqueCharacteristics[uuidStr] = newCharacteristic;
            bleLock->confirmedCharacteristics[uuidStr] = false;

            // Free the allocated memory
            delete[] uuid;
            Serial.println(" - uuid memory freed");

            bleLock->saveCharacteristicsToMemory();
            Serial.println(" - saveCharacteristicsToMemory");

            bleLock->resumeAdvertising();
            Serial.println(" - resumeAdvertising");

            // Unlock the mutex for service operations
            xSemaphoreGive(bleLock->bleMutex);
            Serial.println("characteristicCreationTask: Mutex unlock");

        }
    }
}

[[noreturn]] void BleLock::outgoingMessageTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    MessageBase *responseMessage;

    Serial.println("Starting outgoingMessageTask...");

    while (true) {
        Serial.println("outgoingMessageTask: Waiting to receive message from queue...");

        if (xQueueReceive(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) == pdTRUE) {
            Serial.println("Message received from queue");

            Serial.printf("BleLock::responseMessageTask msg: %s %s\n", responseMessage->destinationAddress.c_str(),
                          responseMessage->type.c_str());

            // Lock the mutex for advertising and characteristic operations
            Serial.println("outgoingMessageTask: Waiting for Mutex");
            xSemaphoreTake(bleLock->bleMutex, portMAX_DELAY);
            Serial.println("outgoingMessageTask: Mutex lock");

            auto it = bleLock->uniqueCharacteristics.find(responseMessage->destinationAddress);
            if (it != bleLock->uniqueCharacteristics.end()) {
                Serial.printf("Destination address found in uniqueCharacteristics %s\n",
                              responseMessage->destinationAddress.c_str());

                NimBLECharacteristic *characteristic = it->second;
                std::string serializedMessage = responseMessage->serialize();
                Serial.printf("Serialized message: %s\n", serializedMessage.c_str());

                characteristic->setValue(serializedMessage);
                Serial.println("Characteristic value set");

                characteristic->notify();
                Serial.println("Characteristic notified");
            } else {
                Serial.println("Destination address not found in uniqueCharacteristics");
            }

            delete responseMessage;
            Serial.println("Response message deleted");

            bleLock->resumeAdvertising();
            Serial.println("Advertising resumed");

            // Unlock the mutex for advertising and characteristic operations
            xSemaphoreGive(bleLock->bleMutex);
            Serial.println("outgoingMessageTask: Mutex unlock");
        } else {
            Serial.println("Failed to receive message from queue");
        }
    }
}
