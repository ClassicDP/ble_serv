#include "BleLock.h"

// Callbacks Implementation
PublicCharacteristicCallbacks::PublicCharacteristicCallbacks(BleLock *lock) : lock(lock) {}

void PublicCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) {
    NimBLECharacteristicCallbacks::onRead(pCharacteristic, desc);
    auto mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::Green, F("PublicCharacteristicCallbacks::onRead called from %s"), mac.c_str());
    lock->handlePublicCharacteristicRead(pCharacteristic, mac);
}

void printCharacteristics(NimBLEService *pService) {
    Serial.println("Listing characteristics:");

    std::vector<NimBLECharacteristic *> characteristics = pService->getCharacteristics();
    for (auto &characteristic: characteristics) {
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

SemaphoreHandle_t logMutex = nullptr;

void initializeLogMutex() {
    if (logMutex == nullptr) {
        logMutex = xSemaphoreCreateMutex();
        if (logMutex == nullptr) {
            Serial.println(F("Failed to create log mutex"));
        }
    }
}

void logColor(LColor color, const __FlashStringHelper *format, ...) {
    std::string colorCode;
    if (logMutex == nullptr) {
        Serial.println(F("Log mutex is not initialized"));
        initializeLogMutex();
    }

    switch (color) {
        case LColor::Reset:
            colorCode = "\x1B[0m";
            break;
        case LColor::Red:
            colorCode = "\x1B[31m";
            break;
        case LColor::LightRed:
            colorCode = "\x1B[91m";
            break;
        case LColor::Yellow:
            colorCode = "\x1B[93m";
            break;
        case LColor::LightBlue:
            colorCode = "\x1B[94m";
            break;
        case LColor::Green:
            colorCode = "\x1B[92m";
            break;
        case LColor::LightCyan:
            colorCode = "\x1B[96m";
            break;
        default:
            colorCode = "\x1B[0m";
            break;
    }
    xSemaphoreTake(logMutex, portMAX_DELAY);

    Serial.print("\n");  // Add newline at the beginning
    Serial.print(millis());
    Serial.print("ms: ");
    Serial.print(colorCode.c_str());

    // Create a buffer to hold the formatted string
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf_P(buffer, sizeof(buffer), reinterpret_cast<const char *>(format), args);
    va_end(args);

    Serial.print(buffer);
    Serial.print("\x1B[0m");  // Reset color
    Serial.println();
    xSemaphoreGive(logMutex);
}

int lastMemoryLog = 0;

void logMemory(const std::string &prefix = "") {
    int currentMemory = esp_get_free_heap_size();
    int diff = currentMemory - lastMemoryLog;
    logColor(LColor::Yellow, F("%s Current free heap memory: %d bytes, change: %d bytes"), prefix.c_str(), currentMemory, diff);
    lastMemoryLog = currentMemory;
}

UniqueCharacteristicCallbacks::UniqueCharacteristicCallbacks(BleLock *lock, std::string uuid)
        : lock(lock), uuid(std::move(uuid)) {}

void UniqueCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) {
    logColor(LColor::Yellow, F("UniqueCharacteristicCallbacks::onWrite called from: %s"),
             NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    logMemory("onWrite");
    {
        std::string receivedMessage = pCharacteristic->getValue();
        logColor(LColor::LightBlue, F("Received message: %s"), receivedMessage.c_str());

        // Allocate memory for the received message and copy the string
        auto *receivedMessageStrAndMac = new std::tuple{new std::string(receivedMessage),
                                                        new std::string(NimBLEAddress(desc->peer_ota_addr).toString())};

        // Send the message to the JSON parsing queue
        if (xQueueSend(lock->incomingQueue, &receivedMessageStrAndMac, portMAX_DELAY) != pdPASS) {
            logColor(LColor::Red, F("Failed to send message to JSON parsing queue"));
            delete std::get<0>(*receivedMessageStrAndMac); // Delete the received message string
            delete std::get<1>(*receivedMessageStrAndMac); // Delete the MAC address string
            delete receivedMessageStrAndMac; // Delete the tuple itself
        }
    }
    logMemory("onWrite");
}

MessageBase *BleLock::request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const {
    requestMessage->sourceAddress = macAddress; // Use the stored MAC address
    requestMessage->destinationAddress = destAddr;
    requestMessage->requestUUID = requestMessage->generateUUID(); // Generate a new UUID for the request

    if (xQueueSend(outgoingQueue, &requestMessage, portMAX_DELAY) != pdPASS) {
        logColor(LColor::Red, F("Failed to send request to the outgoing queue"));
        return nullptr;
    }

    uint32_t startTime = xTaskGetTickCount();
    std::string *receivedMessage;

    while (true) {
        uint32_t elapsed = xTaskGetTickCount() - startTime;
        if (elapsed >= pdMS_TO_TICKS(timeout)) {
            // Timeout reached
            return nullptr;
        }

        // Peek at the queue to see if there is a message
        if (xQueuePeek(responseQueue, &receivedMessage, pdMS_TO_TICKS(timeout) - elapsed) == pdTRUE) {
            // Create an instance of MessageBase from the received message
            MessageBase *instance = MessageBase::createInstance(*receivedMessage);

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

void ServerCallbacks::onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) {
    std::string mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::LightBlue, F("Device connected (mac=%s)"), mac.c_str());

    printCharacteristics(pServer->getServiceByUUID("ABCD"));
}

void ServerCallbacks::onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) {
    std::string mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::LightBlue, F("Device disconnected (mac=%s)"), mac.c_str());
}

BleLock::BleLock(std::string lockName)
        : lockName(std::move(lockName)), pServer(nullptr), pService(nullptr), pPublicCharacteristic(nullptr),
          autoincrement(0) {
    memoryFilename = "/ble_lock_memory.json";
    logColor(LColor::LightBlue, F("xQueueCreate "));
    characteristicCreationQueue = xQueueCreate(10, sizeof(CreateCharacteristicCmd *)); // Queue to hold char* pointers
    outgoingQueue = xQueueCreate(10, sizeof(MessageBase *));
    responseQueue = xQueueCreate(10, sizeof(std::string *));
    logColor(LColor::LightBlue, F("initializeMutex "));
    initializeMutex();
    logColor(LColor::LightBlue, F("done "));
}

void BleLock::setup() {
    logColor(LColor::LightBlue, F("Starting BLE setup..."));

    BLEDevice::init(lockName);
    logColor(LColor::LightBlue, F("BLEDevice::init completed"));

    // Get the MAC address and store it
    macAddress = BLEDevice::getAddress().toString();
    logColor(LColor::LightBlue, F("Device MAC address: %s"), macAddress.c_str());

    pServer = BLEDevice::createServer();
    if (!pServer) {
        logColor(LColor::Red, F("Failed to create BLE server"));
        return;
    }
    logColor(LColor::LightBlue, F("BLE server created"));

    pServer->setCallbacks(new ServerCallbacks(this));
    logColor(LColor::LightBlue, F("Server callbacks set"));

    pService = pServer->createService("ABCD");
    if (!pService) {
        logColor(LColor::Red, F("Failed to create service"));
        return;
    }
    logColor(LColor::LightBlue, F("Service created"));

    pPublicCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t) 0x1234), // Example UUID
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    if (!pPublicCharacteristic) {
        logColor(LColor::Red, F("Failed to create public characteristic"));
        return;
    }
    logColor(LColor::LightBlue, F("Public characteristic created"));

    pPublicCharacteristic->setCallbacks(new PublicCharacteristicCallbacks(this));
    logColor(LColor::LightBlue, F("Public characteristic callbacks set"));

    pService->start();
    logColor(LColor::LightBlue, F("Service started"));

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!pAdvertising) {
        logColor(LColor::Red, F("Failed to get advertising"));
        return;
    }
    pAdvertising->addServiceUUID("ABCD");
    pAdvertising->addServiceUUID(pService->getUUID()); // Advertise the service UUID
    for (const auto &pair: uniqueCharacteristics) {
        pAdvertising->addServiceUUID(pair.first); // Advertise each characteristic UUID
    }
    logColor(LColor::LightBlue, F("Advertising started"));
    pAdvertising->start();

    logColor(LColor::LightBlue, F("BLE setup complete"));

    xTaskCreate(characteristicCreationTask, "CharacteristicCreationTask", 8192, this, 1, nullptr);
    logColor(LColor::LightBlue, F("CharacteristicCreationTask created"));
    xTaskCreate(outgoingMessageTask, "OutgoingMessageTask", 8192, this, 1, nullptr);
    logColor(LColor::LightBlue, F("OutgoingMessageTask created"));

    // Create the JSON parsing queue
    incomingQueue = xQueueCreate(10, sizeof(std::string *));
    if (incomingQueue == nullptr) {
        logColor(LColor::Red, F("Failed to create JSON parsing queue"));
        return;
    }

    // Create the JSON parsing task
    xTaskCreate(parsingIncomingTask, "JsonParsingTask", 8192, this, 1, nullptr);
    logColor(LColor::LightBlue, F("JsonParsingTask created"));

    loadCharacteristicsFromMemory();
}

QueueHandle_t BleLock::getOutgoingQueueHandle() const {
    return outgoingQueue;
}

void BleLock::initializeMutex() {
    bleMutex = xSemaphoreCreateMutex();
    if (bleMutex == nullptr) {
        logColor(LColor::Red, F("Failed to create the mutex"));
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

void BleLock::handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string &mac) {
    if (pairedDevices.find(mac) != pairedDevices.end()) {
        // If device is already paired, provide existing UUID
        std::string existingUUID = pairedDevices[mac];
        pCharacteristic->setValue(existingUUID);
        resumeAdvertising();
        logColor(LColor::Green, F("Device already paired, provided existing UUID: %s"), existingUUID.c_str());
        auto cmd = new CreateCharacteristicCmd{existingUUID, pCharacteristic};
        if (xQueueSend(characteristicCreationQueue, &cmd, portMAX_DELAY) != pdPASS) {
            logColor(LColor::Red, F("Failed to send UUID to characteristicCreationQueue"));
            delete cmd;
        }
        return;
    }

    std::string newUUID = generateUUID();
    logColor(LColor::LightBlue, F("Generated new UUID: %s"), newUUID.c_str());
    pCharacteristic->setValue(newUUID);
    awaitingKeys.insert(newUUID);
    pairedDevices[mac] = newUUID;
    resumeAdvertising();
    logColor(LColor::LightBlue, F(" - resumeAdvertising"));
    auto cmd = new CreateCharacteristicCmd{newUUID, pCharacteristic};
    if (xQueueSend(characteristicCreationQueue, &cmd, portMAX_DELAY) != pdPASS) {
        logColor(LColor::Red, F("Failed to send UUID to characteristicCreationQueue"));
        delete cmd;
    }
}

void BleLock::loadCharacteristicsFromMemory() {
    logColor(LColor::LightBlue, F("Attempting to mount SPIFFS..."));

    if (SPIFFS.begin(true)) {
        logColor(LColor::LightBlue, F("SPIFFS mounted successfully."));
        File file = SPIFFS.open(memoryFilename.c_str(), FILE_READ);
        if (file) {
            logColor(LColor::LightBlue, F("File opened successfully. Parsing JSON..."));

            json doc;
            try {
                doc = json::parse(file.readString().c_str());
            } catch (json::parse_error &e) {
                logColor(LColor::Red, F("Failed to parse JSON: %s"), e.what());
                file.close();
                return;
            }

            autoincrement = doc.value("autoincrement", 0);
            logColor(LColor::LightBlue, F("Autoincrement loaded: %d"), autoincrement);

            json characteristics = doc["characteristics"];
            for (auto &kv: characteristics.items()) {
                const std::string &uuid = kv.key();
                bool confirmed = kv.value().get<bool>();
                logColor(LColor::LightBlue, F("Loading characteristic with UUID: %s, confirmed: %d"), uuid.c_str(), confirmed);

                if (!pService) {
                    logColor(LColor::Red, F("pService is null. Cannot create characteristic."));
                    continue;
                }

                NimBLECharacteristic *characteristic = pService->createCharacteristic(
                        NimBLEUUID::fromString(uuid),
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
                );

                if (!characteristic) {
                    logColor(LColor::Red, F("Failed to create characteristic with UUID: %s"), uuid.c_str());
                    continue;
                }

                characteristic->setCallbacks(new UniqueCharacteristicCallbacks(this, uuid));
                uniqueCharacteristics[uuid] = characteristic;
                confirmedCharacteristics[uuid] = confirmed;
            }

            json devices = doc["pairedDevices"];
            for (auto &kv: devices.items()) {
                const std::string &mac = kv.key();
                std::string uuid = kv.value();
                pairedDevices[mac] = uuid;
                logColor(LColor::LightBlue, F("Loaded paired device with MAC: %s, UUID: %s"), mac.c_str(), uuid.c_str());
            }

            file.close();
            logColor(LColor::LightBlue, F("All characteristics and paired devices loaded successfully."));
        } else {
            logColor(LColor::Red, F("Failed to open file. File may not exist."));
        }
    } else {
        logColor(LColor::Red, F("SPIFFS mount failed, attempting to format..."));
        if (SPIFFS.format()) {
            logColor(LColor::LightBlue, F("SPIFFS formatted successfully."));
            if (SPIFFS.begin()) {
                logColor(LColor::LightBlue, F("SPIFFS mounted successfully after formatting."));
            } else {
                logColor(LColor::Red, F("Failed to mount SPIFFS after formatting."));
            }
        } else {
            logColor(LColor::Red, F("SPIFFS formatting failed."));
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
    for (const auto &pair: pairedDevices) {
        devices[pair.first] = pair.second;
    }
    doc["pairedDevices"] = devices;

    File file = SPIFFS.open(memoryFilename.c_str(), FILE_WRITE);
    if (file) {
        file.print(doc.dump().c_str());
        logColor(LColor::LightBlue, F("saving.. %s"), doc.dump().c_str());
        file.close();
    } else {
        logColor(LColor::Red, F("Failed to open file for writing."));
    }
}

void BleLock::resumeAdvertising() {
    logColor(LColor::LightBlue, F("Attempting to clear and restart advertising..."));
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();

    if (!pAdvertising) {
        logColor(LColor::Red, F("Failed to get advertising object"));
        return;
    }

    if (pAdvertising->isAdvertising()) {
        logColor(LColor::Yellow, F("Advertising is currently active. Stopping..."));
        pAdvertising->stop();
    }

//    logColor(LColor::LightBlue, F("Resetting advertising data..."));
//    pAdvertising->reset();
//
//
//
//    pAdvertising->addServiceUUID("ABCD");
//    for (const auto &pair : uniqueCharacteristics) {
//        logColor(LColor::LightBlue, F("Adding UUID %s to new advertising data"), pair.first.c_str());
//        pAdvertising->addServiceUUID(pair.first);
//    }

    if (pAdvertising->start()) {
        logColor(LColor::LightBlue, F("Advertising successfully restarted"));
    } else {
        logColor(LColor::Red, F("Failed to restart advertising"));
    }
}



void BleLock::stopService() {
    logColor(LColor::LightBlue, F("Attempting to stop Advertising..."));
    pServer->stopAdvertising();
    logColor(LColor::LightBlue, F("Advertising stopped"));
}

void BleLock::startService() {
    logColor(LColor::LightBlue, F("Attempting to start service..."));
    pService->start();
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    logColor(LColor::LightBlue, F("Attempting to start Advertising..."));
    pAdvertising->start();
    logColor(LColor::LightBlue, F("Each started"));
}

[[noreturn]] void BleLock::characteristicCreationTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    CreateCharacteristicCmd *cmd;

    while (true) {
        logColor(LColor::Green, F("characteristicCreationTask: Waiting to receive UUID from queue..."));

        if (xQueueReceive(bleLock->characteristicCreationQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            std::string uuidStr(cmd->uuid);
            logColor(LColor::Green, F("BleLock::characteristicCreationTask uuid to create: %s"), uuidStr.c_str());

            logMemory("characteristicCreationTask: Before waiting for Mutex");

            logColor(LColor::LightBlue, F("characteristicCreationTask: Waiting for Mutex"));
            xSemaphoreTake(bleLock->bleMutex, portMAX_DELAY);
            logColor(LColor::LightBlue, F("characteristicCreationTask: Mutex lock"));

            logMemory("characteristicCreationTask: After Mutex lock, before createCharacteristic");

            NimBLECharacteristic *newCharacteristic = bleLock->pService->createCharacteristic(
                    NimBLEUUID::fromString(uuidStr),
                    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
            );
            logColor(LColor::LightBlue, F(" - createCharacteristic"));
            printCharacteristics(bleLock->pService);

            newCharacteristic->setCallbacks(new UniqueCharacteristicCallbacks(bleLock, uuidStr));
            logColor(LColor::LightBlue, F(" - setCallbacks"));

            bleLock->uniqueCharacteristics[uuidStr] = newCharacteristic;
            bleLock->confirmedCharacteristics[uuidStr] = false;

            logColor(LColor::LightBlue, F(" - uuid memory freed"));

            bleLock->saveCharacteristicsToMemory();
            logColor(LColor::LightBlue, F(" - saveCharacteristicsToMemory"));

            bleLock->resumeAdvertising();

            xSemaphoreGive(bleLock->bleMutex);
            logColor(LColor::LightBlue, F("characteristicCreationTask: Mutex unlock"));

            logMemory("characteristicCreationTask: After characteristic creation");

            delete cmd;
        }
    }
}
[[noreturn]] void BleLock::outgoingMessageTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    MessageBase *responseMessage;

    logColor(LColor::LightBlue, F("Starting outgoingMessageTask..."));

    while (true) {
        logColor(LColor::LightBlue, F("outgoingMessageTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) == pdTRUE) {
            logMemory("outgoingMessageTask: Before processing message");

            logColor(LColor::LightBlue, F("Message received from queue"));

            logColor(LColor::LightBlue, F("BleLock::responseMessageTask msg: %s"), responseMessage->destinationAddress.c_str());

            logColor(LColor::LightBlue, F("outgoingMessageTask: Mutex lock"));

            xSemaphoreTake(bleLock->bleMutex, portMAX_DELAY); // добавлен вызов xSemaphoreTake

            auto it = bleLock->pairedDevices.find(responseMessage->destinationAddress);
            if (it != bleLock->pairedDevices.end()) {
                logColor(LColor::LightBlue, F("Destination address found in uniqueCharacteristics %s"), responseMessage->destinationAddress.c_str());

                auto characteristic = bleLock->uniqueCharacteristics[it->second];
                std::string serializedMessage = responseMessage->serialize();
                logColor(LColor::LightBlue, F("Serialized message: %s"), serializedMessage.c_str());

                logMemory("outgoingMessageTask: Before setValue");
                characteristic->setValue(serializedMessage);
                logMemory("outgoingMessageTask: After setValue");

                logColor(LColor::LightBlue, F("Characteristic value set"));

                logMemory("outgoingMessageTask: Before notify");
                characteristic->notify();
                logMemory("outgoingMessageTask: After notify");

                logColor(LColor::LightBlue, F("Characteristic notified"));
            } else {
                logColor(LColor::Red, F("Destination address not found in uniqueCharacteristics"));
            }

            delete responseMessage;
            logColor(LColor::LightBlue, F("Response message deleted"));

            logMemory("outgoingMessageTask: Before resumeAdvertising");
            bleLock->resumeAdvertising();
            logMemory("outgoingMessageTask: After resumeAdvertising");
            logColor(LColor::LightBlue, F("Advertising resumed"));

            xSemaphoreGive(bleLock->bleMutex);
            logColor(LColor::LightBlue, F("outgoingMessageTask: Mutex unlock"));

            logMemory("outgoingMessageTask: After processing message");
        } else {
            logColor(LColor::Red, F("Failed to receive message from queue"));
        }
    }
}

[[noreturn]] void BleLock::parsingIncomingTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLock *>(pvParameter);
    std::tuple<std::string *, std::string *> *receivedMessageStrAndMac;

    while (true) {
        logColor(LColor::LightBlue, F("parsingIncomingTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->incomingQueue, &receivedMessageStrAndMac, portMAX_DELAY) == pdTRUE) {
            logMemory("parsingIncomingTask: Before parsing message");
            auto receivedMessage = std::get<0>(*receivedMessageStrAndMac);
            auto address = std::get<1>(*receivedMessageStrAndMac);
            logColor(LColor::LightBlue, F("parsingIncomingTask: Received message: %s from mac: %s"), receivedMessage->c_str(), address->c_str());

            try {
                auto msg = MessageBase::createInstance(*receivedMessage);
                if (msg) {
                    msg->sourceAddress = *address;
                    logColor(LColor::LightBlue, F("Received request from: %s "), msg->sourceAddress.c_str());

                    MessageBase *responseMessage = msg->processRequest(bleLock);

                    if (responseMessage) {
                        logColor(LColor::LightBlue, F("Sending response message to outgoing queue"));
                        responseMessage->destinationAddress = msg->sourceAddress;
                        responseMessage->sourceAddress = msg->destinationAddress;
                        responseMessage->requestUUID = msg->requestUUID;
                        if (xQueueSend(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) != pdPASS) {
                            logColor(LColor::Red, F("Failed to send response message to outgoing queue"));
                            delete responseMessage;
                        }
                    } else {
                        auto responseMessageStr = new std::string(*receivedMessage);
                        logColor(LColor::LightBlue, F("Sending response message string to response queue"));
                        if (xQueueSend(bleLock->responseQueue, &responseMessageStr, portMAX_DELAY) != pdPASS) {
                            logColor(LColor::Red, F("Failed to send response message string to response queue"));
                            delete responseMessageStr;
                        }
                    }
                    delete msg; // Make sure to delete the msg after processing
                } else {
                    logColor(LColor::Red, F("Failed to create message instance"));
                }
            } catch (const json::parse_error &e) {
                logColor(LColor::Red, F("JSON parse error: %s"), e.what());
            } catch (const std::exception &e) {
                logColor(LColor::Red, F("Exception occurred: %s"), e.what());
            }
            logMemory("parsingIncomingTask: After parsing message");

            // Free the allocated memory for the received message
            delete receivedMessage;
            delete address;
            delete receivedMessageStrAndMac;
            logMemory("parsingIncomingTask: After freeing memory");
        }
    }
}
