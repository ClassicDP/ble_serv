#include <Arduino.h>
#include "WiFiManager.h"
#include "TemperatureMonitor.h"
#include "BleLock.h"
#include "MessageBase.h"
#include "ReqRes.h"
#include "esp_bt.h"
#include "ArduinoLog.h"

WiFiManager wifiManager;
BleLock lock("BleLock");

// Custom log prefix function with colors and timestamps
void logPrefix(Print* _logOutput, int logLevel) {
    // ANSI escape codes for colors
    const char* colorReset = "\x1B[0m";
    const char* colorFatal = "\x1B[31m";    // Red
    const char* colorError = "\x1B[91m";    // Light Red
    const char* colorWarning = "\x1B[93m";  // Yellow
    const char* colorNotice = "\x1B[94m";   // Light Blue
    const char* colorTrace = "\x1B[92m";    // Green
    const char* colorVerbose = "\x1B[96m";  // Light Cyan

    switch (logLevel) {
        case 0: _logOutput->print("S: "); break;  // Silent
        case 1: _logOutput->print(colorFatal); _logOutput->print("F: "); break;  // Fatal
        case 2: _logOutput->print(colorError); _logOutput->print("E: "); break;  // Error
        case 3: _logOutput->print(colorWarning); _logOutput->print("W: "); break;  // Warning
        case 4: _logOutput->print(colorNotice); _logOutput->print("N: "); break;  // Notice
        case 5: _logOutput->print(colorTrace); _logOutput->print("T: "); break;  // Trace
        case 6: _logOutput->print(colorVerbose); _logOutput->print("V: "); break;  // Verbose
        default: _logOutput->print("?: "); break; // Unknown
    }

    // Print timestamp
    _logOutput->print(millis());
    _logOutput->print(": ");
}

void logSuffix(Print* _logOutput, int logLevel) {
    // ANSI escape codes for colors
    const char* colorReset = "\x1B[0m";
    _logOutput->print(colorReset); // Reset color
    _logOutput->println(); // Add newline
}




void setup() {
    Serial.begin(115200);
    while (!Serial);

    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.setPrefix(&logPrefix);
    Log.setSuffix(&logSuffix);

    logColor(LColor::LightBlue, F("Start setup"));

    bool registerResOk = []() {
        MessageBase::registerConstructor("resOk", []() -> MessageBase* { return new ResOk(); });
        return true;
    }();
    bool registerReqRegKey = []() {
        MessageBase::registerConstructor("reqRegKey", []() -> MessageBase* { return new ReqRegKey(); });
        return true;
    }();

    if (!SPIFFS.begin(true)) {
        logColor(LColor::Red, F("An error has occurred while mounting SPIFFS"));
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

        char temperatureStr[10];
        dtostrf(temperature, 6, 2, temperatureStr);

        Log.notice(F("CPU Temperature: %s °C"), temperatureStr);
        lastTempCheck = millis();
    }
}

