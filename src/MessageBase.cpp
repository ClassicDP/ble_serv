#include "MessageBase.h"
#include <sstream>
#include "Arduino.h"

std::unordered_map<std::string, MessageBase::Constructor> MessageBase::constructors;

void MessageBase::registerConstructor(const std::string& type, Constructor constructor) {
    constructors[type] = constructor;
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
    doc["type"] = type;
    serializeExtraFields(doc);
    return doc.dump(); // Use dump() to convert JSON to string
}

void MessageBase::deserialize(const std::string& input) {
    json doc = json::parse(input);
    sourceAddress = doc["sourceAddress"];
    destinationAddress = doc["destinationAddress"];
    type = doc["type"];
    deserializeExtraFields(doc);
}
