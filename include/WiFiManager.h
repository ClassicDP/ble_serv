#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPIFFS.h>

class WiFiManager {
public:
    WiFiManager();
    void begin();
    void loop();
    String loadFile(const char* path);

private:
    void connectToSavedNetwork();
    void startAPMode();
    void handleRoot();
    void handleScan();
    void handleConnect();
    void handleStyle();
    void handleStatus();
    void handleToggleAP();

    WebServer server;
    DNSServer dnsServer;
    Preferences preferences;
    bool apMode = false;
};

#endif
