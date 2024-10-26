#include "WiFiManager.h"

WiFiManager::WiFiManager() : server(80) {}

void WiFiManager::begin() {

    preferences.begin("WiFiManager", false);
    connectToSavedNetwork();

    if (WiFi.status() != WL_CONNECTED) {
        connectToSavedNetwork();
    }
    delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        startAPMode();
    }

    server.on("/", HTTP_GET, std::bind(&WiFiManager::handleRoot, this));
    server.on("/scan", HTTP_GET, std::bind(&WiFiManager::handleScan, this));
    server.on("/connect", HTTP_POST, std::bind(&WiFiManager::handleConnect, this));
    server.on("/style.css", HTTP_GET, std::bind(&WiFiManager::handleStyle, this));
    server.on("/status", HTTP_GET, std::bind(&WiFiManager::handleStatus, this));
    server.begin();
}

void WiFiManager::loop() {
    server.handleClient();
}

void WiFiManager::connectToSavedNetwork() {
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");

    if (ssid != "") {
        WiFi.begin(ssid.c_str(), password.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Connected to saved network");
            return;
        }
    }

    Serial.println("No saved network found or failed to connect");
}

void WiFiManager::startAPMode() {
    WiFi.softAP("ESP32-Setup", "password");
    Serial.println("Started AP mode");
}

void WiFiManager::handleRoot() {
    String html = loadFile("/index.html");
    server.send(200, "text/html", html);
}

void WiFiManager::handleScan() {
    int n = WiFi.scanNetworks();
    String networks = "{ \"networks\": [";

    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            networks += ",";
        }
        networks += "{";
        networks += "\"ssid\": \"" + WiFi.SSID(i) + "\",";
        networks += "\"rssi\": " + String(WiFi.RSSI(i));
        networks += "}";
    }

    networks += "] }";
    server.send(200, "application/json", networks);
}

void WiFiManager::handleConnect() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String password = server.arg("password");

        WiFi.begin(ssid.c_str(), password.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            preferences.putString("ssid", ssid);
            preferences.putString("password", password);
            String response = "{\"status\":\"connected\"               ,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
            server.send(200, "application/json", response);
        } else {
            server.send(200, "application/json", "{\"status\":\"failed\"}");
        }
    }
}

void WiFiManager::handleStyle() {
    String css = loadFile("/style.css");
    server.send(200, "text/css", css);
}

void WiFiManager::handleStatus() {
    String status = "{";
    status += "\"connected\": " + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    status += "\"rssi\": " + String(WiFi.RSSI());
    status += "}";
    server.send(200, "application/json", status);
}

String WiFiManager::loadFile(const char* path) {
    File file = SPIFFS.open(path, "r");
    if (!file) {
        return String("File not found");
    }

    String content = file.readString();
    file.close();
    return content;
}

///
struct netList
{
    String ssid;
    int32_t rssi;
    int chanel;
    bool isProtected;
    netList *next;
};

static netList *scanRes = nullptr;

void cleanScanList ()
{
    while (scanRes)
    {
        netList *tmp =scanRes->next;
        delete scanRes;
        scanRes = tmp;      
    }
}

void appendToScanList (    String ssid, int32_t rssi, int chanel, bool isProtected)
{
    netList * tmp = new netList;
    tmp->ssid = ssid;
    tmp->rssi = rssi;
    tmp->chanel = chanel;
    tmp->isProtected = isProtected;
    tmp->next = nullptr;
    if (scanRes)
    {
        netList * it = scanRes;
        while (it->next)
            it=it->next; 
        it->next = tmp;
    }
    else
        scanRes = tmp;
}

void WiFiManager::scanWiFi ()
{
//  Wireless::scanner = true;
  //Wireless::
  auto mode =  WiFi.getMode();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  cleanScanList();
  int num  = WiFi.scanNetworks ();
  Serial.println ("WIFi - ssid num - %d"), num;;
  if (num>0)
  {
    for (int i= 0; i < num; i++)
    {
      Serial.println (WiFi.SSID(i));
      appendToScanList (WiFi.SSID(i),WiFi.RSSI(i),WiFi.channel(i),WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      /****
      Serial.print(WiFi.SSID(i)); // Имя сети (SSID)
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i)); // Уровень сигнала (RSSI)
      Serial.print(" dBm) ");
      Serial.print(" Канал: ");
      Serial.print(WiFi.channel(i)); // Канал сети
      Serial.print(" Шифрование: ");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Открытая" : "Защищённая");    }
      **/
    }
  }
}

void WiFiManager::setProperties (String ssid, String pass)
{
    preferences.putString("ssid", ssid);
    preferences.putString("password", pass);

    connectToSavedNetwork ();
}

/*********/
static netList * iterator = nullptr;;

bool ListWiFiStart()
{
    iterator = scanRes;
    return scanRes != nullptr;
}

bool ListWiFiNext(String &ssid, int32_t &rssi, int &chanel, bool &isProtected)
{
    ssid = iterator->ssid;
    rssi = iterator->rssi;
    chanel = iterator->chanel;
    isProtected = iterator->isProtected;
    iterator=iterator->next;
    return iterator != nullptr;
}
