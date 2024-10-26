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

    IntSAtringMap::insert ((MessageType)MessageTypeReg::resOk, "resOk");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::reqRegKey, "reqRegKey");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::OpenRequest, "OpenRequest");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::SecurityCheckRequestest, "SecurityCheckRequestest");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::OpenCommand, "OpenCommand");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::resKey, "resKey");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::HelloRequest, "HelloRequest");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::ReceivePublic, "ReceivePublic");

    IntSAtringMap::insert ((MessageType)MessageTypeReg::GetDeviceList, "GetDeviceList");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::AccessOnOff, "AccessOnOff");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::AccessOnOFFSingle, "AccessOnOFFSingle");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::AccessOnOFFMulty, "AccessOnOFFMulty");

    IntSAtringMap::insert ((MessageType)MessageTypeReg::ScanWiFi, "ScanWiFi");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::ScanWiFiResult, "ScanWiFiResult");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::LoginWWiFi, "LoginWWiFi");
    IntSAtringMap::insert ((MessageType)MessageTypeReg::GetWiFiStatus, "GetWiFiStatus");


    bool registerResOk = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::resOk, []() -> MessageBase * { return new ResOk(); });
        return true;
    }();
    bool registerReqRegKey = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::reqRegKey, []() -> MessageBase * { return new ReqRegKey(); });
        return true;
    }();
    
    bool registerResKey = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::resKey, []() -> MessageBase * { return new ResKey(); });
        return true;
    }();

    bool registerOpenReq = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::OpenRequest, []() -> MessageBase * { return new OpenRequest(); });
        return true;
    }();
    bool registerOpenCmd = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::OpenCommand, []() -> MessageBase * { return new OpenCommand(); });
        return true;
    }();
    bool registerSecurituCheck = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::SecurityCheckRequestest, []() -> MessageBase * { return new SecurityCheckRequestest(); });
        return true;
    }();

    bool registerHelloRequest = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::HelloRequest, []() -> MessageBase * { return new HelloRequest(); });
        return true;
    }();
    bool registerReceivePublic = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::ReceivePublic, []() -> MessageBase * { return new ReceivePublic(); });
        return true;
    }();



    bool registerGetDeviceList = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::GetDeviceList, []() -> MessageBase * { return new GetDeviceList(); });
        return true;
    }();
    bool registerAccessOnOff = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::AccessOnOff, []() -> MessageBase * { return new AccessOnOff(); });
        return true;
    }();
    bool registeRAccessOnOFFSingle = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::AccessOnOFFSingle, []() -> MessageBase * { return new AccessOnOFFSingle(); });
        return true;
    }();
    bool registerAccessOnOFFMulty = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::AccessOnOFFMulty, []() -> MessageBase * { return new AccessOnOFFMulty(); });
        return true;
    }();



    bool registerScanWiFi = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::ScanWiFi, []() -> MessageBase * { return new ScanWiFiMessage(); });
        return true;
    }();
    bool registerScanWiFiResult = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::ScanWiFiResult, []() -> MessageBase * { return new ScanWiFiResultMessage(); });
        return true;
    }();
    bool registerLoginWWiFi = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::LoginWWiFi, []() -> MessageBase * { return new LoginWWiFiMessage(); });
        return true;
    }();
    bool registerGetWiFiStatus = []() {
        MessageBase::registerConstructor((MessageType)MessageTypeReg::GetWiFiStatus, []() -> MessageBase * { return new GetWiFiStatusMessage(); });
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

void scanWiFi ()
{
    wifiManager.scanWiFi();
}

void SetWiFiPass (String ssid, String pass)
{
    wifiManager.setProperties (ssid,pass);
}

bool isWiFiConnected ()
{
    return wifiManager.getIsConnected();
}