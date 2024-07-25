#include <Arduino.h>
#include "WiFiManager.h"
#include "TemperatureMonitor.h"
#include "BleLockAndKey.h"
#include "ReqRes.h"
//#include "esp_bt.h"

WiFiManager wifiManager;
BleLockBase * lock;
std::string LocName = "BleLock";

void setup() {

    bool registerResOk = []() {
        MessageBase::registerConstructor(MessageType::resOk, []() -> MessageBase * { return new ResOk(); });
        return true;
    }();
    bool registerReqRegKey = []() {
        MessageBase::registerConstructor(MessageType::reqRegKey, []() -> MessageBase * { return new ReqRegKey(); });
        return true;
    }();
    
    bool registerResKey = []() {
        MessageBase::registerConstructor(MessageType::resKey, []() -> MessageBase * { return new ResKey(); });
        return true;
    }();

    bool registerOpenReq = []() {
        MessageBase::registerConstructor(MessageType::OpenRequest, []() -> MessageBase * { return new OpenRequest(); });
        return true;
    }();
    bool registerOpenCmd = []() {
        MessageBase::registerConstructor(MessageType::OpenCommand, []() -> MessageBase * { return new OpenCommand(); });
        return true;
    }();
    bool registerSecurituCheck = []() {
        MessageBase::registerConstructor(MessageType::SecurityCheckRequestest, []() -> MessageBase * { return new SecurityCheckRequestest(); });
        return true;
    }();

    bool registerHelloRequest = []() {
        MessageBase::registerConstructor(MessageType::HelloRequest, []() -> MessageBase * { return new HelloRequest(); });
        return true;
    }();
    bool registerReceivePublic = []() {
        MessageBase::registerConstructor(MessageType::ReceivePublic, []() -> MessageBase * { return new ReceivePublic(); });
        return true;
    }();


    if (!SPIFFS.begin(true)) {
        logColor(LColor::Red, F("An error has occurred while mounting SPIFFS"));
        return;
    }
    lock = createAndInitLock(true, LocName);
    wifiManager.begin();
    TemperatureMonitor::begin();
}

void loop() {
    wifiManager.loop();
    static unsigned long lastTempCheck = 0;
    if (millis() - lastTempCheck >= 10000) {
        float temperature = TemperatureMonitor::getTemperature();

        char temperatureStr[10];
        dtostrf(temperature, 6, 2, temperatureStr);

        logColor(LColor::Green, F("CPU Temperature: %s °C"), temperatureStr);

        lastTempCheck = millis();
    }
}
