#ifndef REGRES_H
#define REGRES_H

#include "MessageBase.h"
#include "BleLockAndKey.h"

#define MessageMaxDelay 0xefffffff
enum class MessageTypeReg {
    resOk,
    reqRegKey,
    OpenRequest, 
    SecurityCheckRequestest,
    OpenCommand,
    resKey, 
    HelloRequest,
    ReceivePublic
};


class ResOk : public MessageBase {
public:
    bool status{};

    ResOk() {
        type = (MessageType)MessageTypeReg::resOk;
    }

    explicit ResOk(bool status) : status(status) {
        type = (MessageType)MessageTypeReg::resOk;
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
        type = (MessageType)MessageTypeReg::resKey;
    }

    explicit ResKey(bool status, std::string newKey) : status(status), key(newKey) {
        type = (MessageType)MessageTypeReg::resKey;
    }

protected:
    void serializeExtraFields(json &doc) override {
        doc["status"] = status;
        doc["key"] = key;
        Serial.printf("Serialized status: %d  key:%s\n", status, key.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        status = doc["status"];
        key = doc["key"];
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
        type = (MessageType)MessageTypeReg::reqRegKey;
    }

    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLockServer *>(context);
        if (xSemaphoreTake(lock->mutex, portMAX_DELAY) == pdTRUE) {
            //lock->secureConnection.generateAESKey (sourceAddress);
            //key = lock->secureConnection.GetAESKey (sourceAddress);
            auto newKey = lock->secureConnection.decryptMessageRSA (key,sourceAddress );
            lock->secureConnection.aesKeys[sourceAddress] = newKey;
            xSemaphoreGive(lock->mutex);
        }
        /*
        auto res = new ResOk();
        res->destinationAddress = key;
        res->sourceAddress = key;
        */
        auto res = new ResKey();
        res->destinationAddress = sourceAddress;
        res->sourceAddress = destinationAddress;
        res->status = true;
        res->key = "";
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





class OpenCommand : public MessageBase {
public:
    std::string randomField;

    OpenCommand() {
        type = (MessageType)MessageTypeReg::OpenCommand;
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
        doc["randomField"] = randomField;
        Log.notice("Serialized randomField: %s\n", randomField.c_str());
    }

    void deserializeExtraFields(const json &doc) override {
        randomField = doc["randomField"];
        Log.notice("Deserialized randomField: %s\n", randomField.c_str());
    }
};

class SecurityCheckRequestest : public MessageBase {
public:
    std::string randomField;

    SecurityCheckRequestest() {
        type = (MessageType)MessageTypeReg::SecurityCheckRequestest;
        requestUUID = generateUUID();
    }

    void setRandomField(std::string randomFieldVal)
    {
        randomField = randomFieldVal;
    }
    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLockServer *>(context);
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

    std::string getEncryptedCommand (BleLockServer *lock)
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
        type = (MessageType)MessageTypeReg::OpenRequest;
    }

    void setRandomField(std::string randomFieldVal)
    {
        randomField = randomFieldVal;
    }

    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLockServer *>(context);

        std::string randomField = lock->secureConnection.generateRandomField();

        // Создаем запрос для проверки безопасности
        SecurityCheckRequestest* securityCheckRequest = new SecurityCheckRequestest();
        securityCheckRequest->sourceAddress = destinationAddress;
        securityCheckRequest->destinationAddress = sourceAddress;
        //securityCheckRequest->type = MessageType::SecurityCheck;
        securityCheckRequest->setRandomField(randomField);

        // Отправляем запрос на проверку безопасности и ждем ответ
        MessageBase* securityCheckResponse = lock->request(securityCheckRequest, sourceAddress, MessageMaxDelay);
        //logColor(LColor::Green, F("delete CHECK_Request"));
        //delete securityCheckRequest;

        logColor(LColor::Green, F("CHECK_ANSWER: %s"), securityCheckResponse->serialize().c_str());
        if (securityCheckResponse && securityCheckResponse->type == (MessageType)MessageTypeReg::OpenCommand) 
        {
            // Расшифровываем команду открытия и проверяем рандомное поле
            std::string decryptedCommand = lock->secureConnection.decryptMessageAES(((OpenCommand*)securityCheckResponse)->getEncryptedCommand(), sourceAddress);
            logColor (LColor::Yellow, F("decryptedCommand = <%s>   etalonField = <%s>"),decryptedCommand.c_str(),randomField.c_str());
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


////////////////////////
///////////////////////
////////////////////////
#define SERVER_PART

class ReceivePublic : public MessageBase {
public:
    std::string key;

    ReceivePublic() {
        type = (MessageType)MessageTypeReg::ReceivePublic;
    }

    explicit ReceivePublic(std::string newKey) :  key(newKey) {
        type = (MessageType)MessageTypeReg::ReceivePublic;
    }
protected:

    void serializeExtraFields(json &doc) override {
        doc["key"] = key;
        Serial.printf("Serialized key:lem=%d\n", key.length());
    }

    void deserializeExtraFields(const json &doc) override {
        key = doc["key"];
        Serial.printf("Deserialized key:len=%d\n", key.length());
    }
    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLockServer *>(context);
        return nullptr;
    }

};
// cliewnt handshake request
class HelloRequest : public MessageBase {
public:
    bool status{};
    std::string key;

    HelloRequest() {
        type = (MessageType)MessageTypeReg::HelloRequest;
    }

    explicit HelloRequest(bool status, std::string newKey) : status(status), key(newKey) {
        type = (MessageType)MessageTypeReg::HelloRequest;
    }
protected:
    void serializeExtraFields(json &doc) override {
        doc["status"] = status;
        doc["key"] = key;
        Serial.printf("Serialized status: %d  key:%s\n", status, key.c_str()?key.c_str():"");
    }

    void deserializeExtraFields(const json &doc) override {
        status = doc["status"];
        key = doc["key"];
        Serial.printf("Deserialized status: %d  key:%s\n", status, key.c_str()?key.c_str():"");
    }
    MessageBase *processRequest(void *context) override {
        auto lock = static_cast<BleLockServer *>(context);
        logColor(LColor::Yellow, F("HelloRequest processRequest status = %d"), status);

        if (status) //handshake suceeded  check key and send Ok
        {
                logColor (LColor::Green, F("check hash!"));
            bool bChkResult = false;
            auto keyPair = lock->secureConnection.keys.find(sourceAddress);
            if (keyPair!=lock->secureConnection.keys.end())
            {
                logColor (LColor::Green, F("Found keyPair!"));
                //keyPair->second.first;
                //keyPair->second.second;
                auto rawMessage = key;
                //int size = rawMessage.size();

                auto pubKey =  keyPair->second.first;
                auto hash = lock->secureConnection.generatePublicKeyHash (pubKey, 16);
                bool isSiteConfirmed = lock->confirm ();
                logColor(LColor::Yellow, F("%s <--> %s"), hash.c_str(), rawMessage.c_str());
                if (hash == rawMessage && isSiteConfirmed)
                    bChkResult = true;

                //std::string encMessage  = std::string ((char*)rawMessage.data(),rawMessage.size()); 
                //std::vector<uint8_t> encryptAESKey = lock->secureConnection.decryptMessageRSA (encMessage,sourceAddress);
                //lock->secureConnection.SetAESKey(sourceAddress, SecureConnection::vector2hex(encryptAESKey));
            }
            ResOk* res = new ResOk();
            res->destinationAddress = sourceAddress;
            res->sourceAddress = destinationAddress;
            res->status = bChkResult;
            res->requestUUID = requestUUID;
            return res;
        }
        else // sdend publick key
        {
            if (lock->secureConnection.keys.find(sourceAddress) == lock->secureConnection.keys.end())
            {
                logColor (LColor::Green, F("Gen key!"));
                lock->secureConnection.generateRSAKeys (sourceAddress);
                logColor (LColor::Green, F("Gen key - finished"));
            }
            logColor (LColor::Green, F("Save keys!"));
            lock->secureConnection.SaveRSAKeys();
            logColor (LColor::Green, F("Keys saved!"));

            ReceivePublic* res = new ReceivePublic;

            res->destinationAddress = sourceAddress;
            res->sourceAddress = destinationAddress;            
            res->key = SecureConnection::vector2hex(lock->secureConnection.keys[sourceAddress].first);
            res->requestUUID = requestUUID;
            return res;
        }
        return nullptr;
    }
};



#endif

