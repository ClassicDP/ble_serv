#include "MessageBase.h"
#include <sstream>

std::unordered_map<std::string, MessageBase::Constructor> MessageBase::constructors;

void MessageBase::registerConstructor(const std::string& type, Constructor constructor) {
    constructors[type] = constructor;
}

MessageBase* MessageBase::createInstance(const std::string& input) {
    json doc = json::parse(input);
    auto it = constructors.find(doc["type"]);
    if (it != constructors.end()) {
        MessageBase* instance = it->second();
        instance->deserialize(input);
        return instance;
    }
    return nullptr;
}

std::string MessageBase::serialize() {
    json doc;
    doc["sourceAddress"] = sourceAddress;
    doc["destinationAddress"] = destinationAddress;
    doc["type"] = type;
    serializeExtraFields(doc);
    std::ostringstream os;
    os << doc;
    return os.str();
}

void MessageBase::deserialize(const std::string& input) {
    json doc = json::parse(input);
    sourceAddress = doc["sourceAddress"];
    destinationAddress = doc["destinationAddress"];
    type = doc["type"];
    deserializeExtraFields(doc);
}
