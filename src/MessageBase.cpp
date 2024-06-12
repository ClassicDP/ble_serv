// MessageBase.cpp
#include "MessageBase.h"
#include <iostream>

std::unordered_map<std::string, MessageBase::Constructor> MessageBase::constructors;

void MessageBase::registerConstructor(const std::string &type, Constructor constructor) {
    constructors[type] = std::move(constructor);
}

MessageBase* MessageBase::createInstance(const std::string &input) {
    std::cout << "createInstance" << std::endl;
    json doc = json::parse(input);
    if (!doc.contains("type")) {
        std::cout << "Key 'type' not found" << std::endl;
        return nullptr;
    }
    std::string type = doc["type"];
    std::cout << "Type: " << type << std::endl;
    auto it = constructors.find(type);
    if (it != constructors.end()) {
        MessageBase* instance = it->second();
        instance->deserialize(input);
        return instance;
    } else {
        std::cout << "Constructor not found for type" << std::endl;
    }
    return nullptr;
}

std::string MessageBase::serialize() const {
    json doc;
    doc["type"] = type;
    doc["sourceAddress"] = sourceAddress;
    doc["destinationAddress"] = destinationAddress;
    return doc.dump();
}

void MessageBase::deserialize(const std::string &input) {
    json doc = json::parse(input);
    type = doc["type"];
    sourceAddress = doc["sourceAddress"];
    destinationAddress = doc["destinationAddress"];
}


