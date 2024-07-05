#include "BleLock.h"


// Callbacks Implementation
PublicCharacteristicCallbacks::PublicCharacteristicCallbacks(BleLock *lock) : lock(lock) {}

void PublicCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) {
    NimBLECharacteristicCallbacks::onRead(pCharacteristic, desc);
    auto mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::Green, F("PublicCharacteristicCallbacks::onRead called from %s"), mac.c_str());
    lock->handlePublicCharacteristicRead(pCharacteristic, mac);
}

void printCharacteristics(NimBLEService* pService) {
    Serial.println("Listing characteristics:");

    std::vector<NimBLECharacteristic*> characteristics = pService->getCharacteristics();
    for (auto& characteristic : characteristics) {
        Serial.print("Characteristic UUID: ");
        Serial.println(characteristic->getUUID().toString().c_str());

        Serial.print("Properties: ");
        uint32_t properties = characteristic->getProperties();
        if (properties & NIMBLE_PROPERTY::READ) {
            Serial.print("READ ");
        }
        if (properties & NIMBLE_PROPERTY::WRITE) {
            Serial.print("WRITE ");
        }
        if (properties & NIMBLE_PROPERTY::NOTIFY) {
            Serial.print("NOTIFY ");
        }
        if (properties & NIMBLE_PROPERTY::INDICATE) {
            Serial.print("INDICATE ");
        }
        Serial.println();
    }
}

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



UniqueCharacteristicCallbacks::UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid)
        : lock(lock), uuid(std::move(uuid)) {}

void UniqueCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic) {
    logColor(LColor::Yellow, F("UniqueCharacteristicCallbacks::onWrite called"));

    std::string receivedMessage = pCharacteristic->getValue();
    Log.verbose(F("Received message: %s"), receivedMessage.c_str());

    // Allocate memory for the received message and copy the string
    auto *receivedMessageStr = new std::string(receivedMessage);

    // Send the message to the JSON parsing queue
    if (xQueueSend(lock->jsonParsingQueue, &receivedMessageStr, portMAX_DELAY) != pdPASS) {
        Log.error(F("Failed to send message to JSON parsing queue"));
        delete receivedMessageStr;
    }
}

MessageBase* BleLock::request(MessageBase* requestMessage, const std::string& destAddr, uint32_t timeout) const {
    requestMessage->sourceAddress = macAddress; // Use the stored MAC address
    requestMessage->destinationAddress = destAddr;
    requestMessage->requestUUID = requestMessage->generateUUID(); // Generate a new UUID for the request

    if (xQueueSend(outgoingQueue, &requestMessage, portMAX_DELAY) != pdPASS) {
        Log.error(F("Failed to send request to the outgoing queue"));
        return nullptr;
    }

    uint32_t startTime = xTaskGetTickCount();
    std::string* receivedMessage;

    while (true) {
        uint32_t elapsed = xTaskGetTickCount() - startTime;
        if (elapsed >= pdMS_TO_TICKS(timeout)) {
            // Timeout reached
            return nullptr;
        }

        // Peek at the queue to see if there is a message
        if (xQueuePeek(responseQueue, &receivedMessage, pdMS_TO_TICKS(timeout) - elapsed) == pdTRUE) {
            // Create an instance of MessageBase from the received message
            MessageBase* instance = MessageBase::createInstance(*receivedMessage);

            // Check if the source address and requestUUID match
            if (instance->sourceAddress == destAddr && instance->requestUUID == requestMessage->requestUUID) {
                // Remove the item from the queue after confirming the source address and requestUUID match
                xQueueReceive(responseQueue, &receivedMessage, 0);
                delete receivedMessage; // Delete the received message pointer
                return instance;
            }
            delete instance;
        }
    }

    return nullptr; // This should never be reached, but it's here to satisfy the compiler
}


ServerCallbacks::ServerCallbacks(BleLock *lock) : lock(lock) {}

void ServerCallbacks::onConnect(NimBLEServer *pServer, ble_gap_conn_desc* desc) {

    std::string mac = NimBLEAddress(desc->peer_ota_addr).toString();
    Log.verbose(F("Device connected (mac=%s)"), mac.c_str());

    printCharacteristics(pServer->getServiceByUUID("ABCD"));
}

void ServerCallbacks::onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc* desc) {
    std::string mac = NimBLEAddress(desc->peer_ota_addr).toString();
    Log.verbose(F("Device disconnected (mac=%s)"), mac.c_str());
}

BleLock::BleLock(std::string lockName)
        : lockName(std::move(lockName)), pServer(nullptr), pService(nullptr), pPublicCharacteristic(nullptr),
          autoincrement(0) {
    memoryFilename = "/ble_lock_memory.json";
    Log.verbose(F("xQueueCreate "));
    characteristicCreationQueue = xQueueCreate(10, sizeof(CreateCharacteristicCmd *)); // Queue to hold char* pointers
    outgoingQueue = xQueueCreate(10, sizeof(MessageBase *));
    responseQueue = xQueueCreate(10, sizeof(std::string *));
    Log.verbose(F("initializeMutex "));
    initializeMutex();
    Log.verbose(F("done "));
}

void BleLock::setup() {
    Log.verbose(F("Starting BLE setup..."));

    BLEDevice::init(lockName);
    Log.verbose(F("BLEDevice::init completed"));

    // Get the MAC address and store it
    macAddress = BLEDevice::getAddress().toString();
    Log.verbose(F("Device MAC address: %s"), macAddress.c_str());

    pServer = BLEDevice::createServer();
    if (!pServer) {
        Log.error(F("Failed to create BLE server"));
        return;
    }
    Log.verbose(F("BLE server created"));

    pServer->setCallbacks(new ServerCallbacks(this));
    Log.verbose(F("Server callbacks set"));

    pService = pServer->createService("ABCD");
    if (!pService) {
        Log.error(F("Failed to create service"));
        return;
    }
    Log.verbose(F("Service created"));

    pPublicCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x1234), // Example UUID
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    if (!pPublicCharacteristic) {
        Log.error(F("Failed to create public characteristic"));
        return;
    }
    Log.verbose(F("Public characteristic created"));

    pPublicCharacteristic->setCallbacks(new PublicCharacteristicCallbacks(this));
    Log.verbose(F("Public characteristic callbacks set"));

    pService->start();
    Log.verbose(F("Service started"));

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!pAdvertising) {
        Log.error(F("Failed to get advertising"));
        return;
    }
    pAdvertising->addServiceUUID("ABCD");
    pAdvertising->addServiceUUID(pService->getUUID()); // Advertise the service UUID
    for (const auto &pair : uniqueCharacteristics) {
        pAdvertising->addServiceUUID(pair.first); // Advertise each characteristic UUID
    }
    Log.verbose(F("Advertising started"));
    pAdvertising->start();

    Log.verbose(F("BLE setup complete"));

    xTaskCreate(characteristicCreationTask, "CharacteristicCreationTask", 8192, this, 1, nullptr);
    Log.verbose(F("CharacteristicCreationTask created"));
    xTaskCreate(outgoingMessageTask, "OutgoingMessageTask", 8192, this, 1, nullptr);
    Log.verbose(F("OutgoingMessageTask created"));

    // Create the JSON parsing queue
    jsonParsingQueue = xQueueCreate(10, sizeof(std::string *));
    if (jsonParsingQueue == nullptr) {
        Log.error(F("Failed to create JSON parsing queue"));
        return;
    }

    // Create the JSON parsing task
    xTaskCreate(jsonParsingTask, "JsonParsingTask", 8192, this, 1, nullptr);
    Log.verbose(F("JsonParsingTask created"));

    loadCharacteristicsFromMemory();
}


QueueHandle_t BleLock::getOutgoingQueueHandle() const {
    return outgoingQueue;
}

void BleLock::initializeMutex() {
    bleMutex = xSemaphoreCreateMutex();
    if (bleMutex == nullptr) {
        Log.error(F("Failed to create the mutex"));
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
    return {uuid};
}

void BleLock::handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string& mac) {
    if (pairedDevices.find(mac) != pairedDevices.end()) {
        // If device is already paired, provide existing UUID
        std::string existingUUID = pairedDevices[mac];
        pCharacteristic->setValue(existingUUID);
        resumeAdvertising();
        logColor(LColor::Green, F("Device already paired, provided existing UUID: %s"), existingUUID.c_str());
        auto cmd = new CreateCharacteristicCmd{existingUUID, pCharacteristic};
        if (xQueueSend(characteristicCreationQueue, &cmd, portMAX_DELAY) != pdPASS) {
            Log.error(F("Failed to send UUID to characteristicCreationQueue"));
            delete cmd;
        }
        return;
    }

    std::string newUUID = generateUUID();
    Log.verbose(F("Generated new UUID: %s"), newUUID.c_str());
    pCharacteristic->setValue(newUUID);
    awaitingKeys.insert(newUUID);
    pairedDevices[mac] = newUUID;
    resumeAdvertising();
    Log.verbose(F(" - resumeAdvertising"));
    auto cmd = new CreateCharacteristicCmd{newUUID, pCharacteristic};
    if (xQueueSend(characteristicCreationQueue, &cmd, portMAX_DELAY) != pdPASS) {
        Log.error(F("Failed to send UUID to characteristicCreationQueue"));
        delete cmd;
    }
}

void BleLock::loadCharacteristicsFromMemory() {
    Log.verbose(F("Attempting to mount SPIFFS..."));

    if (SPIFFS.begin(true)) {
        Log.verbose(F("SPIFFS mounted successfully."));
        File file = SPIFFS.open(memoryFilename.c_str(), FILE_READ);
        if (file) {
            Log.verbose(F("File opened successfully. Parsing JSON..."));

            json doc;
            try {
                doc = json::parse(file.readString().c_str());
            } catch (json::parse_error &e) {
                Log.error(F("Failed to parse JSON: %s"), e.what());
                file.close();
                return;
            }

            autoincrement = doc.value("autoincrement", 0);
            Log.verbose(F("Autoincrement loaded: %d"), autoincrement);

            json characteristics = doc["characteristics"];
            for (auto &kv: characteristics.items()) {
                const std::string& uuid = kv.key();
                bool confirmed = kv.value().get<bool>();
                Log.verbose(F("Loading characteristic with UUID: %s, confirmed: %d"), uuid.c_str(), confirmed);

                if (!pService) {
                    Log.error(F("pService is null. Cannot create characteristic."));
                    continue;
                }

                NimBLECharacteristic *characteristic = pService->createCharacteristic(
                        NimBLEUUID::fromString(uuid),
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
                );

                if (!characteristic) {
                    Log.error(F("Failed to create characteristic with UUID: %s"), uuid.c_str());
                    continue;
                }

                characteristic->setCallbacks(new UniqueCharacteristicCallbacks(this, uuid));
                uniqueCharacteristics[uuid] = characteristic;
                confirmedCharacteristics[uuid] = confirmed;
            }

            json devices = doc["pairedDevices"];
            for (auto &kv: devices.items()) {
                const std::string& mac = kv.key();
                std::string uuid = kv.value();
                pairedDevices[mac] = uuid;
                Log.verbose(F("Loaded paired device with MAC: %s, UUID: %s"), mac.c_str(), uuid.c_str());
            }

            file.close();
            Log.verbose(F("All characteristics and paired devices loaded successfully."));
        } else {
            Log.error(F("Failed to open file. File may not exist."));
        }
    } else {
        Log.error(F("SPIFFS mount failed, attempting to format..."));
        if (SPIFFS.format()) {
            Log.verbose(F("SPIFFS formatted successfully."));
            if (SPIFFS.begin()) {
                Log.verbose(F("SPIFFS mounted successfully after formatting."));
            } else {
                Log.error(F("Failed to mount SPIFFS after formatting."));
            }
        } else {
            Log.error(F("SPIFFS formatting failed."));
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

    json devices;
    for (const auto &pair : pairedDevices) {
        devices[pair.first] = pair.second;
    }
    doc["pairedDevices"] = devices;

    File file = SPIFFS.open(memoryFilename.c_str(), FILE_WRITE);
    if (file) {
        file.print(doc.dump().c_str());
        Log.verbose(F("saving.. %s"), doc.dump().c_str());
        file.close();
    } else {
        Log.error(F("Failed to open file for writing."));
    }
}

void BleLock::resumeAdvertising() {
    Log.verbose(F("Attempting to resume advertising..."));

    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();

    // Очистите рекламные данные и добавьте сервисы
    pAdvertising->reset();
    pAdvertising->addServiceUUID("ABCD");

    for (const auto &pair : uniqueCharacteristics) {
        Log.verbose(F("Renewing advertising for %s"), pair.first.c_str());
        pAdvertising->addServiceUUID(pair.first);  // Рекламируйте каждый UUID характеристики
    }

    if (pAdvertising->start()) {
        Log.verbose(F("Advertising resumed"));
    } else {
        Log.error(F("Failed to resume advertising"));
    }
}

void BleLock::stopService() {
    Log.verbose(F("Attempting to stop Advertising..."));
    pServer->stopAdvertising();
    Log.verbose(F("Advertising stopped"));
}

void BleLock::startService() {
    Log.verbose(F("Attempting to start service..."));
    pService->start();
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    Log.verbose(F("Attempting to start Advertising..."));
    pAdvertising->start();
    Log.verbose(F("Each started"));
}

[[noreturn]] void BleLock::characteristicCreationTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    CreateCharacteristicCmd *cmd;

    while (true) {
        logColor(LColor::Green, F("characteristicCreationTask: Waiting to receive UUID from queue..."));

        if (xQueueReceive(bleLock->characteristicCreationQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            // Convert received char* to std::string
            std::string uuidStr(cmd->uuid);
            logColor(LColor::Green, F("BleLock::characteristicCreationTask uuid to create: %s"), uuidStr.c_str());

            // Lock the mutex for service operations
            Log.verbose(F("characteristicCreationTask: Waiting for Mutex"));
            xSemaphoreTake(bleLock->bleMutex, portMAX_DELAY);
            Log.verbose(F("characteristicCreationTask: Mutex lock"));

            NimBLECharacteristic *newCharacteristic = bleLock->pService->createCharacteristic(
                    NimBLEUUID::fromString(uuidStr),
                    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
            );
            Log.verbose(F(" - createCharacteristic"));
            printCharacteristics(bleLock->pService);

            newCharacteristic->setCallbacks(new UniqueCharacteristicCallbacks(bleLock, uuidStr));
            Log.verbose(F(" - setCallbacks"));

            bleLock->uniqueCharacteristics[uuidStr] = newCharacteristic;
            bleLock->confirmedCharacteristics[uuidStr] = false;

            // Free the allocated memory
            Log.verbose(F(" - uuid memory freed"));

            bleLock->saveCharacteristicsToMemory();
            Log.verbose(F(" - saveCharacteristicsToMemory"));

            bleLock->resumeAdvertising();

            // Unlock the mutex for service operations
            xSemaphoreGive(bleLock->bleMutex);
            Log.verbose(F("characteristicCreationTask: Mutex unlock"));
            delete cmd;
        }
    }
}

[[noreturn]] void BleLock::outgoingMessageTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    MessageBase *responseMessage;

    Log.verbose(F("Starting outgoingMessageTask..."));

    while (true) {
        Log.verbose(F("outgoingMessageTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) == pdTRUE) {
            Log.verbose(F("Message received from queue"));

            Log.verbose(F("BleLock::responseMessageTask msg: %s %s"), responseMessage->destinationAddress.c_str(),
                        ToString(responseMessage->type));

            // Lock the mutex for advertising and characteristic operations
            Log.verbose(F("outgoingMessageTask: Waiting for Mutex"));
            xSemaphoreTake(bleLock->bleMutex, portMAX_DELAY);
            Log.verbose(F("outgoingMessageTask: Mutex lock"));

            auto it = bleLock->uniqueCharacteristics.find(responseMessage->destinationAddress);
            if (it != bleLock->uniqueCharacteristics.end()) {
                Log.verbose(F("Destination address found in uniqueCharacteristics %s"),
                            responseMessage->destinationAddress.c_str());

                NimBLECharacteristic *characteristic = it->second;
                std::string serializedMessage = responseMessage->serialize();
                Log.verbose(F("Serialized message: %s"), serializedMessage.c_str());

                characteristic->setValue(serializedMessage);
                Log.verbose(F("Characteristic value set"));

                characteristic->notify();
                Log.verbose(F("Characteristic notified"));
            } else {
                Log.error(F("Destination address not found in uniqueCharacteristics"));
            }

            delete responseMessage;
            Log.verbose(F("Response message deleted"));

            bleLock->resumeAdvertising();
            Log.verbose(F("Advertising resumed"));

            // Unlock the mutex for advertising and characteristic operations
            xSemaphoreGive(bleLock->bleMutex);
            Log.verbose(F("outgoingMessageTask: Mutex unlock"));
        } else {
            Log.error(F("Failed to receive message from queue"));
        }
    }
}

[[noreturn]] void BleLock::jsonParsingTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    std::string *receivedMessageStr;

    while (true) {
        Log.verbose(F("jsonParsingTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->jsonParsingQueue, &receivedMessageStr, portMAX_DELAY) == pdTRUE) {
            Log.verbose(F("jsonParsingTask: Received message: %s"), receivedMessageStr->c_str());

            try {
                auto msg = MessageBase::createInstance(*receivedMessageStr);
                if (msg) {
                    Log.verbose(F("Received request from: %s with type: %s"), msg->sourceAddress.c_str(), ToString(msg->type));

                    MessageBase *responseMessage = msg->processRequest(bleLock);
                    delete msg;

                    if (responseMessage) {
                        Log.verbose(F("Sending response message to outgoing queue"));
                        if (xQueueSend(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) != pdPASS) {
                            Log.error(F("Failed to send response message to outgoing queue"));
                            delete responseMessage;
                        }
                    } else {
                        auto responseMessageStr = new std::string(*receivedMessageStr);
                        Log.verbose(F("Sending response message string to response queue"));
                        if (xQueueSend(bleLock->responseQueue, &responseMessageStr, portMAX_DELAY) != pdPASS) {
                            Log.error(F("Failed to send response message string to response queue"));
                            delete responseMessageStr;
                        }
                    }
                } else {
                    Log.error(F("Failed to create message instance"));
                }
            } catch (const json::parse_error &e) {
                Log.error(F("JSON parse error: %s"), e.what());
            } catch (const std::exception &e) {
                Log.error(F("Exception occurred: %s"), e.what());
            }

            // Free the allocated memory for the received message
            delete receivedMessageStr;
        }
    }
}
