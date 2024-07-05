#ifndef MESSAGEBASE_H
#define MESSAGEBASE_H

#include <unordered_map>
#include <functional>
#include <string>
#include "json.hpp"

using json = nlohmann::json;
#define DECLARE_ENUM_WITH_STRING_CONVERSIONS(name, ...) \
    enum class name { __VA_ARGS__, COUNT }; \
    inline const char* ToString(name v) { \
        static constexpr const char* strings[] = { #__VA_ARGS__ }; \
        return strings[static_cast<int>(v)]; \
    } \
    inline name FromString(const std::string& str) { \
        static constexpr const char* strings[] = { #__VA_ARGS__ }; \
        for (int i = 0; i < static_cast<int>(name::COUNT); ++i) { \
            if (str == strings[i]) { \
                return static_cast<name>(i); \
            } \
        } \
        throw std::invalid_argument("Invalid enum value: " + str); \
    }

DECLARE_ENUM_WITH_STRING_CONVERSIONS(MessageType, ResOk, reqRegKey)


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
    virtual void serializeExtraFields(json& doc) = 0;
    virtual void deserializeExtraFields(const json& doc) = 0;

private:
    static std::unordered_map<std::string, Constructor> constructors;
    void deserialize(const std::string& input);

};

#endif // MESSAGEBASE_H
