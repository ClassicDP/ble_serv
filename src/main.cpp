#include <Arduino.h>
#include "WiFiManager.h"
#include "TemperatureMonitor.h"
#include "BleLock.h"
#include "MessageBase.h"
#include "ReqRes.h"
#include "esp_bt.h"

WiFiManager wifiManager;
BleLock lock("BleLock");

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("Start setup");

    // Регистрация конструктора
    bool registerResOk = []() {
        MessageBase::registerConstructor("resOk", []() -> MessageBase * { return new ResOk(); });
        return true;
    }();
    // Регистрация конструктора
    bool registerReqRegKey = []() {
        MessageBase::registerConstructor("reqRegKey", []() -> MessageBase * { return new ReqRegKey(); });
        return true;
    }();
    if (!SPIFFS.begin(true)) {
        Serial.println("An error has occurred while mounting SPIFFS");
        return;
    }
    wifiManager.begin();
    TemperatureMonitor::begin();
    lock.setup();

}

void loop() {
    wifiManager.loop();
    static unsigned long lastTempCheck = 0;
    if (millis() - lastTempCheck >= 10000) {
        float temperature = TemperatureMonitor::getTemperature();
        Serial.printf("CPU Temperature: %.2f °C\n", temperature);
        lastTempCheck = millis();
    }

    delay(100);
}
