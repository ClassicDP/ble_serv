#include "MessageBase.h"
#include "BleLock.h"

class ResOk : public MessageBase {
public:
    bool status{};

    ResOk() {
        type = MessageType::ResOk;
    }

    explicit ResOk(bool status) : status(status) {
        type = MessageType::ResOk;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["status"] = status;
        Serial.printf("Serialized status: %d\n", status);
    }

    void deserializeExtraFields(const json &doc) override {
        status = doc["status"];
        Serial.printf("Deserialized status: %d\n", status);
    }
};



class ReqRegKey : public MessageBase {
public:
    std::string key;

    ReqRegKey() {
        type = MessageType::reqRegKey;
    }

    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLock *>(context);
        if (xSemaphoreTake(lock->bleMutex, portMAX_DELAY) == pdTRUE) {
            lock->awaitingKeys.insert(key);
            xSemaphoreGive(lock->bleMutex);
        }
        auto res = new ResOk();
        res->destinationAddress = key;
        res->sourceAddress = key;
        return res;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["key"] = key;
        Serial.printf("Serialized key: %s\n", key.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        key = doc["key"];
        Serial.printf("Deserialized key: %s\n", key.c_str());
    }
};
