#include "MessageBase.h"
#include "BleLock.h"

class ResOk : public MessageBase {
public:
    bool status{};

    ResOk() {
        type = "resOk";
    }

    explicit ResOk(bool status) : status(status) {
        type = "resOk";
    }

protected:
    void serializeExtraFields(JsonDocument &doc) const override {
        doc["status"] = status;
    }

    void deserializeExtraFields(const JsonDocument &doc) override {
        status = doc["status"].as<bool>();
    }
};



class ReqRegKey : public MessageBase {
public:
    std::string key;

    ReqRegKey() {
        type = "reqRegKey";
    }


    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLock *>( context);
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
    void serializeExtraFields(JsonDocument &doc) const override {
        doc["key"] = key;
    }

    void deserializeExtraFields(const JsonDocument &doc) override {
        key = doc["key"].as<std::string>();
    }

};




