#include <Arduino.h>
#include "WiFiManager.h"
#include "TemperatureMonitor.h"

void setup() {
    Serial.begin(115200);
    WiFiManager::begin();
    TemperatureMonitor::begin();
}

void loop() {
    WiFiManager::loop();

    static unsigned long lastTempCheck = 0;
    if (millis() - lastTempCheck >= 10000) {
        float temperature = TemperatureMonitor::getTemperature();
        Serial.printf("CPU Temperature: %.2f °C\n", temperature);
        lastTempCheck = millis();
    }

    delay(100);
}
