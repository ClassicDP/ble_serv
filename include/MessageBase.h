#ifndef MESSAGEBASE_H
#define MESSAGEBASE_H

#include <unordered_map>
#include <functional>
#include <string>
#include "json.hpp"

using json = nlohmann::json;
enum class MessageType {
    resOk,
    reqRegKey,
    OpenRequest, 
    SecurityCheckRequestest,
    OpenCommand,
    resKey, 
    HelloRequest,
    ReceivePublic
};

NLOHMANN_JSON_SERIALIZE_ENUM( MessageType, {
    {MessageType::resOk, "resOk"},
    {MessageType::reqRegKey, "reqRegKey"},
    {MessageType::OpenRequest, "OpenRequest"},
    {MessageType::SecurityCheckRequestest, "SecurityCheckRequestest"},
    {MessageType::OpenCommand, "OpenCommand"},
    {MessageType::resKey, "resKey"},
    {MessageType::HelloRequest, "HelloRequest"},
    {MessageType::ReceivePublic, "ReceivePublic"}
})


class MessageBase {
public:
    std::string sourceAddress;
    std::string destinationAddress;
    MessageType type;
    std::string requestUUID;

    MessageBase() = default;

    virtual std::string serialize();
    virtual MessageBase* processRequest(void* context) { return nullptr; } // Virtual method for processing requests
    virtual ~MessageBase() = default;

    using Constructor = std::function<MessageBase*()>;
    static MessageBase* createInstance(const std::string& input);

    static void registerConstructor(const MessageType &type, Constructor constructor);

    std::string generateUUID();

protected:
    virtual void serializeExtraFields(json& doc) {};
    virtual void deserializeExtraFields(const json& doc) {};

private:
    static std::unordered_map<MessageType, Constructor> constructors;
    void deserialize(const std::string& input);

};

#endif // MESSAGEBASE_H
