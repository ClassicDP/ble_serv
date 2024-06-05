#include <Arduino.h>
#include "WiFiManager.h"
#include "TemperatureMonitor.h"
#include "BleLock.h"
#include "MessageBase.h"

WiFiManager wifiManager;
BleLock lock("BleLock");

void setup() {
    Serial.begin(115200);
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
