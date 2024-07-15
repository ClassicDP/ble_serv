#ifndef REGRES_H
#define REGRES_H

#include "MessageBase.h"
#include "BleLock.h"

#define MessageMaxDelay 0xefffffff

class ResOk : public MessageBase {
public:
    bool status{};

    ResOk() {
        type = MessageType::resOk;
    }

    explicit ResOk(bool status) : status(status) {
        type = MessageType::resOk;
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

class ResKey : public MessageBase {
public:
    bool status{};
    std::string key{};

    ResKey() {
        type = MessageType::resKey;
    }

    explicit ResKey(bool status, std::string newKey) : status(status), key(newKey) {
        type = MessageType::resKey;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["status"] = status;
        doc["key"] = SecureConnection::str2hex(key);
        Serial.printf("Serialized status: %d  key:%s\n", status, key.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        status = doc["status"];
        key = SecureConnection::hex2str(doc["key"]);
        Serial.printf("Deserialized status: %d  key:%s\n", status, key.c_str());
    }
    MessageBase *processRequest(void *context) override {
        //isOkRes = true;            
        return nullptr;
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
            lock->secureConnection.generateAESKey (destinationAddress);
            key = std::string((char*)lock->secureConnection.aesKeys.find (destinationAddress)->second.data(),
                lock->secureConnection.aesKeys.find (destinationAddress)->second.size());
            //lock->awaitingKeys.insert(key);
            xSemaphoreGive(lock->bleMutex);
        }
        /*
        auto res = new ResOk();
        res->destinationAddress = key;
        res->sourceAddress = key;
        */
        auto res = new ResKey();
        res->destinationAddress = key;
        res->sourceAddress = key;
        res->status = 0;
        res->key = key;
        return res;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["key"] = SecureConnection::str2hex( key);
        Serial.printf("Serialized key: %s\n", key.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        key = SecureConnection::hex2str(doc["key"]);
        Serial.printf("Deserialized key: %s\n", key.c_str());
    }
};





class OpenCommand : public MessageBase {
public:
    std::string randomField;

    OpenCommand() {
        type = MessageType::OpenCommand;
        requestUUID = generateUUID();
    }

    void setRandomField(std::string randomFieldVal)
    {
        randomField = randomFieldVal;
    }
    /*
    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLock *>(context);
        if (xSemaphoreTake(lock->bleMutex, portMAX_DELAY) == pdTRUE) {
            //lock->awaitingKeys.insert(key);
            xSemaphoreGive(lock->bleMutex);
        }
        auto res = new ResOk();
        res->destinationAddress = sourceAddress;
        res->sourceAddress = destinationAddress;
        res->status = true;
        return res;
    }*/

    std::string getEncryptedCommand ()
    {
        return randomField;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["randomField"] = SecureConnection::str2hex( randomField);
        Log.notice("Serialized randomField: %s\n", SecureConnection::str2hex( randomField).c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        randomField = SecureConnection::hex2str( doc["randomField"]);
        Log.notice("Deserialized randomField: %s\n", randomField.c_str());
    }
};

class SecurityCheckRequestest : public MessageBase {
public:
    std::string randomField;

    SecurityCheckRequestest() {
        type = MessageType::SecurityCheckRequestest;
        requestUUID = generateUUID();
    }

    void setRandomField(std::string randomFieldVal)
    {
        randomField = randomFieldVal;
    }
    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLock *>(context);
        //if (xSemaphoreTake(lock->bleMutex, portMAX_DELAY) == pdTRUE) {
        //    lock->awaitingKeys.insert(key);
        //    xSemaphoreGive(lock->bleMutex);
        //}
        bool result =- false;
        std::string resultStr = lock->secureConnection.encryptMessageAES(randomField,"UUID");
        //result = (lock->temporaryField == resultStr);
        auto res = new OpenCommand();
        //res->destinationAddress = key;
        //res->sourceAddress = key;
        res->setRandomField (resultStr);
        return res;
    }

    std::string getEncryptedCommand (BleLock *lock)
    {
        return lock->secureConnection.encryptMessageAES(randomField,"UUID");;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["randomField"] = randomField;
        Log.notice("Serialized randomField: %s\n", randomField.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        randomField = doc["randomField"];
        Log.notice("Deserialized randomField: %s\n", randomField.c_str());
    }
};



class OpenRequest : public MessageBase {
public:
    std::string key;
    std::string randomField;

    OpenRequest() {
        type = MessageType::OpenRequest;
    }

    void setRandomField(std::string randomFieldVal)
    {
        randomField = randomFieldVal;
    }

    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLock *>(context);

        std::string randomField = lock->secureConnection.generateRandomField();

        // Создаем запрос для проверки безопасности
        SecurityCheckRequestest* securityCheckRequest = new SecurityCheckRequestest();
        securityCheckRequest->sourceAddress = destinationAddress;
        securityCheckRequest->destinationAddress = sourceAddress;
        //securityCheckRequest->type = MessageType::SecurityCheck;
        securityCheckRequest->setRandomField(randomField);

        // Отправляем запрос на проверку безопасности и ждем ответ
        MessageBase* securityCheckResponse = lock->request(securityCheckRequest, sourceAddress, MessageMaxDelay);
        delete securityCheckRequest;

        logColor(LColor::Green, F("CHECK_ANSWER: %s"), securityCheckResponse->serialize().c_str());
        if (securityCheckResponse && securityCheckResponse->type == MessageType::OpenCommand) 
        {
            // Расшифровываем команду открытия и проверяем рандомное поле
            std::string decryptedCommand = lock->secureConnection.decryptMessageAES(((OpenCommand*)securityCheckResponse)->getEncryptedCommand(), sourceAddress);
            if (decryptedCommand == randomField) {
                // Отправляем ответ об успешном открытии
                ResOk* successResponse = new ResOk();
                successResponse->sourceAddress = destinationAddress;
                successResponse->destinationAddress = sourceAddress;
                successResponse->status = true;
                //successResponse->type = MessageType::ResOk;

                //lock->request(successResponse, sourceAddress, MessageMaxDelay/* таймаут */);
                //delete successResponse;
                //delete securityCheckResponse;
                
                //delete request;

                Log.verbose(F("Замок открыт успешно"));
                return successResponse; // Успешное завершение
            } else {
                
                ResOk* successResponse = new ResOk();
                successResponse->sourceAddress = destinationAddress;
                successResponse->destinationAddress = sourceAddress;
                successResponse->status = false;
                Log.error(F("Ошибка проверки безопасности"));
                return successResponse; // Успешное завершение
            }
        } else {
            Log.error(F("Не удалось получить ответ на проверку безопасности"));
        }

        if (securityCheckResponse) delete securityCheckResponse;

        return nullptr;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["key"] = key;
        doc["randomField"] = randomField;
        Log.notice("Serialized OpenRequest: %s\n", randomField.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        key = doc["key"];
        randomField = doc["randomField"];
        Log.notice("Deserialized OpenRequest: %s\n", randomField.c_str());
    }
};

#endif

