#include <Arduino.h>
#include "WiFiManager.h"
#include "TemperatureMonitor.h"
#include "BleLock.h"
#include "MessageBase.h"
#include "ReqRes.h"
#include "esp_bt.h"
#include "ArduinoLog.h"
#include "SecureConnection.h"

// Создаем глобальный объект SecureConnection
SecureConnection secureConnection;

WiFiManager wifiManager;
BleLock lock("BleLock");

// Custom log prefix function with colors and timestamps
void logPrefix(Print *_logOutput, int logLevel) {
    const char *colorReset = "\x1B[0m";
    const char *colorFatal = "\x1B[31m";    // Red
    const char *colorError = "\x1B[91m";    // Light Red
    const char *colorWarning = "\x1B[93m";  // Yellow
    const char *colorNotice = "\x1B[94m";   // Light Blue
    const char *colorTrace = "\x1B[92m";    // Green
    const char *colorVerbose = "\x1B[96m";  // Light Cyan

    switch (logLevel) {
        case 0: _logOutput->print("S: "); break;
        case 1: _logOutput->print(colorFatal); _logOutput->print("F: "); break;
        case 2: _logOutput->print(colorError); _logOutput->print("E: "); break;
        case 3: _logOutput->print(colorWarning); _logOutput->print("W: "); break;
        case 4: _logOutput->print(colorNotice); _logOutput->print("N: "); break;
        case 5: _logOutput->print(colorTrace); _logOutput->print("T: "); break;
        case 6: _logOutput->print(colorVerbose); _logOutput->print("V: "); break;
        default: _logOutput->print("?: "); break;
    }

    _logOutput->print(millis());
    _logOutput->print(": ");
}

void logSuffix(Print *_logOutput, int logLevel) {
    const char *colorReset = "\x1B[0m";
    _logOutput->print(colorReset); // Reset color
    _logOutput->println(); // Add newline
}

void setup2() {
    std::string uuid = "device-uuid";

    // Генерация ключей RSA
    unsigned long start = millis();
    secureConnection.generateRSAKeys(uuid);
    unsigned long end = millis();
    Serial.print("RSA Key Generation Time: ");
    Serial.print(end - start);
    Serial.println(" ms");

    // Генерация AES ключа
    start = millis();
    secureConnection.generateAESKey(uuid);
    end = millis();
    Serial.print("AES Key Generation Time: ");
    Serial.print(end - start);
    Serial.println(" ms");

    // Шифрование и дешифрование сообщения AES
    std::string message = "Hello, world!";
    start = millis();
    std::string encryptedMessage = secureConnection.encryptMessageAES(message, uuid);
    end = millis();
    Serial.print("AES Encryption Time: ");
    Serial.print(end - start);
    Serial.println(" ms");

    start = millis();
    std::string decryptedMessage = secureConnection.decryptMessageAES(encryptedMessage, uuid);
    end = millis();
    Serial.print("AES Decryption Time: ");
    Serial.print(end - start);
    Serial.println(" ms");
    Serial.print("Decrypted Message: ");
    Serial.println(decryptedMessage.c_str());

    // Генерация хеша публичного ключа
    std::string publicKeyHash = secureConnection.generatePublicKeyHash(secureConnection.keys[uuid].first, 16);
    Serial.print("Public Key Hash: ");
    Serial.println(publicKeyHash.c_str());

    // Шифрование и дешифрование AES ключа с использованием RSA
    std::vector<uint8_t> aesKey = secureConnection.aesKeys[uuid];
    start = millis();
    std::string encryptedAESKey = secureConnection.encryptMessageRSA(aesKey, uuid);
    end = millis();
    Serial.print("RSA Encryption Time: ");
    Serial.print(end - start);
    Serial.println(" ms");

    start = millis();
    std::vector<uint8_t> decryptedAESKey = secureConnection.decryptMessageRSA(encryptedAESKey, uuid);
    end = millis();
    Serial.print("RSA Decryption Time: ");
    Serial.print(end - start);
    Serial.println(" ms");
    Serial.print("Decrypted AES Key: ");
    SecureConnection::printHex(decryptedAESKey);

    // Ensure the decrypted AES key is correct
    if (decryptedAESKey == aesKey) {
        Serial.println("AES Key decryption successful and correct.");
    } else {
        Serial.println("AES Key decryption failed or incorrect.");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.setPrefix(&logPrefix);
    Log.setSuffix(&logSuffix);

    logColor(LColor::LightBlue, F("Start setup"));

    setup2();  // Вызов setup2

    Serial.println("After setup2");  // Отладочное сообщение

    bool registerResOk = []() {
        MessageBase::registerConstructor(MessageType::resOk, []() -> MessageBase * { return new ResOk(); });
        return true;
    }();
    bool registerReqRegKey = []() {
        MessageBase::registerConstructor(MessageType::reqRegKey, []() -> MessageBase * { return new ReqRegKey(); });
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
