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

// Регистрация конструктора
bool registerResOk = []() {
    MessageBase::registerConstructor("resOk", []() -> MessageBase * { return new ResOk(); });
    return true;
}();

class ReqRegKey : public MessageBase {
public:
    String key;

    ReqRegKey() {
        type = "reqRegKey";
    }


    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLock *>( context);
        if (xSemaphoreTake(lock->bleMutex, portMAX_DELAY) == pdTRUE) {
            lock->awaitingKeys.insert((new String(key))->c_str());
            xSemaphoreGive(lock->bleMutex);
        }
        return new ResOk();
    }


protected:
    void serializeExtraFields(JsonDocument &doc) const override {
        doc["key"] = key;
    }

    void deserializeExtraFields(const JsonDocument &doc) override {
        key = doc["key"].as<String>();
    }

};

// Регистрация конструктора
bool registerReqRegKey = []() {
    MessageBase::registerConstructor("reqRegKey", []() -> MessageBase * { return new ReqRegKey(); });
    return true;
}();


