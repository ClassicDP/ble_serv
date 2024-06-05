#include "MessageBase.h"
std::unordered_map<std::string, MessageBase::Constructor> MessageBase::constructors;

void MessageBase::registerConstructor(const std::string& type, Constructor constructor) {
    constructors[type] = std::move(constructor);
}

MessageBase* MessageBase::createInstance(const std::string& input) {
    JsonDocument doc;
    deserializeJson(doc, input);
    std::string type = doc["type"];

    auto it = constructors.find(type);
    if (it != constructors.end()) {
        MessageBase* instance = it->second();
        instance->deserialize(String(input.c_str()));
        return instance;
    }
    return nullptr;
}

String MessageBase::serialize() const {
    JsonDocument doc;
    doc["type"] = type;
    doc["sourceAddress"] = sourceAddress;
    doc["destinationAddress"] = destinationAddress;
    serializeExtraFields(doc);
    String output;
    serializeJson(doc, output);
    return output;
}

void MessageBase::deserialize(const String& input) {
    JsonDocument doc;
    deserializeJson(doc, input);
    type = doc["type"].as<String>();
    sourceAddress = doc["sourceAddress"].as<String>();
    destinationAddress = doc["destinationAddress"].as<String>();
    deserializeExtraFields(doc);
}