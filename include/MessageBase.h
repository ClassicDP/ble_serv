#ifndef MESSAGEBASE_H
#define MESSAGEBASE_H
#include <ArduinoJson.h>
#include <unordered_map>
#include <functional>
#include <string>

class MessageBase {
public:
    String sourceAddress;
    String destinationAddress;
    String type;

    MessageBase() = default;

    virtual String serialize() const;
    virtual void deserialize(const String& input);
    virtual MessageBase* processRequest(void* context) { return nullptr; } // Виртуальный метод обработки запроса
    virtual ~MessageBase() = default;

    using Constructor = std::function<MessageBase*()>;
    static void registerConstructor(const std::string& type, Constructor constructor);
    static MessageBase* createInstance(const std::string& input);

protected:
    virtual void serializeExtraFields(JsonDocument& doc) const = 0;
    virtual void deserializeExtraFields(const JsonDocument& doc) = 0;

private:
    static std::unordered_map<std::string, Constructor> constructors;
};

#endif