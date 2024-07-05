#include "MessageBase.h"
#include <sstream>
#include <utility>
#include <random>
#include "Arduino.h"

std::unordered_map<std::string, MessageBase::Constructor> MessageBase::constructors;

void MessageBase::registerConstructor(const MessageType& type, Constructor constructor) {
    constructors[ToString(type)] = std::move(constructor);
}

MessageBase* MessageBase::createInstance(const std::string& input) {
    Serial.println("Try parsing");
    try {
        json doc = json::parse(input);

        // Проверка наличия поля "type"
        if (!doc.contains("type") || !doc["type"].is_string()) {
            Serial.println("Invalid message: Missing or incorrect 'type' field.");
            return nullptr;
        }

        auto it = constructors.find(doc["type"]);
        if (it != constructors.end()) {
            MessageBase* instance = it->second();
            instance->deserialize(input);
            return instance;
        } else {
            Serial.println("Unknown message type.");
            return nullptr;
        }
    } catch (json::parse_error& e) {
        Serial.printf("Failed to parse JSON: %s\n", e.what());
        return nullptr;
    } catch (std::exception& e) {
        Serial.printf("Exception: %s\n", e.what());
        return nullptr;
    } catch (...) {
        Serial.println("Unknown error occurred.");
        return nullptr;
    }
}

std::string MessageBase::serialize() {
    json doc;
    doc["sourceAddress"] = sourceAddress;
    doc["destinationAddress"] = destinationAddress;
    doc["type"] = ToString(type);
    doc["requestUUID"] = requestUUID; // Serialize the request UUID

    serializeExtraFields(doc);
    return doc.dump();
}


void MessageBase::deserialize(const std::string& input) {
    auto doc = json::parse(input);
    sourceAddress = doc["sourceAddress"];
    destinationAddress = doc["destinationAddress"];
    type = FromString(doc["type"]);
    requestUUID = doc["requestUUID"]; // Deserialize the request UUID

    deserializeExtraFields(doc);
}

std::string MessageBase::generateUUID() {
    static std::random_device rd;
    static std::mt19937 generator(rd());
    static std::uniform_int_distribution<uint32_t> distribution(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << distribution(generator);
    return oss.str();
}
