#ifndef MESSAGEBASE_H
#define MESSAGEBASE_H

#include <unordered_map>
#include <functional>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

class MessageBase {
public:
    std::string sourceAddress;
    std::string destinationAddress;
    std::string type;

    MessageBase() = default;

    virtual std::string serialize();
    virtual MessageBase* processRequest(void* context) { return nullptr; } // Virtual method for processing requests
    virtual ~MessageBase() = default;

    using Constructor = std::function<MessageBase*()>;
    static void registerConstructor(const std::string& type, Constructor constructor);
    static MessageBase* createInstance(const std::string& input);

protected:
    virtual void serializeExtraFields(json& doc) = 0;
    virtual void deserializeExtraFields(const json& doc) = 0;

private:
    static std::unordered_map<std::string, Constructor> constructors;
    void deserialize(const std::string& input);

};

#endif // MESSAGEBASE_H
