#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <EEPROM.h>

#include "DHT.h"

//Wi-Fi setup
//will be replaced with values form EEPROM
String ssid =  "";
String password = "";
//max time to mark connect attempt as timeout
#define wifiConnectTimeout 25000
//time before reconnect attempt in case of invalid previous connection
#define wifiFailedAttempt 10000

//Soft AP setup
String scannedWifiNetworks;
//access point ssid
#define apSSID "esp8266_dht22_1"
//settings page could be accessed by requesting http://esp8266/
#define dnsName "esp8266"

//Thingspeak setup
//will be replaced with values form EEPROM
String thingspeakApiKey; //WCS2YMWLH97A4GOS
//Update data for the specific channel not frequently than once per 15 sec
unsigned long thingSpeakUpdateInterval;
#define thingSpeakAddress "api.thingspeak.com"
#define thingSpeakUpdateJsonEndpoint "/update.json"
#define thingSpeakHttpPort 80

//DHT22
#define DHTTYPE DHT22
#define DHTPIN 13
DHT dht(DHTPIN, DHTTYPE);

//EEPROM consts
#define ssidSize 32
#define passSize 32
#define apiKeySize 16
#define longTypeSize 4

//wifi client
WiFiClient client;
//wifi server
WiFiServer server(80);
//DNS responder
MDNSResponder mdns;

//wifi connection status
#define wifiConnectionNotConnected 0
#define wifiConnectionOK 10
#define wifiConnectionFailed 20
#define wifiConnectionTimeout 30
byte wifiConnectionStatus = wifiConnectionNotConnected;


// === EEPROM section
String readStringFromEEPROM(unsigned int offset, unsigned int length) {
    String str = "";

    for (int i = offset; i < offset + length; i++) {
        str += char(EEPROM.read(i));
    }

    return str;
}

void writeStringToEEPROM(String value, unsigned int offset, unsigned int maxLength) {
    byte length = maxLength < value.length() ? maxLength : value.length();

    Serial.println();

    for (int i = 0; i < length; i++) {
        Serial.println("Wrote: " + String(value[i]) + " on address: " + String(offset + i));

        //Disabled for debugging purpose
        //EEPROM.write(offset + i, value[i]);
    }
}

unsigned long readLongFromEEPROM(unsigned int offset) {

    unsigned long result = 0;

    for (int i = offset; i < offset + longTypeSize; i++) {
        result = (result << 8) | EEPROM.read(i);
    }

    return result;
}

void writeLongToEEPROM(unsigned long value, unsigned int offset) {
    byte b;

    Serial.println();

    for (int i = 0; i < longTypeSize; i++) {
        b = ((value >> 8 * (longTypeSize - (i + 1))) & 0xFF);
        Serial.println("Wrote: " + String(b) + " on address: " + String(offset + i));

        //Disabled for debugging purpose
        //EEPROM.write(offset + i, b);
    }
}

void readDataFromEEPROM() {
    Serial.println();
    Serial.println("Reading data from EEPROM");

    ssid = readStringFromEEPROM(0, ssidSize);
    Serial.println("SSID: " + ssid);

    password = readStringFromEEPROM(ssidSize, passSize);
    Serial.print("PASS: " + password);

    thingspeakApiKey = readStringFromEEPROM(ssidSize + passSize, apiKeySize);
    Serial.print("API Key: " + thingspeakApiKey);

    thingSpeakUpdateInterval = readLongFromEEPROM(ssidSize + passSize + apiKeySize);
    Serial.print("Update interval: " + thingSpeakUpdateInterval);
}

void clearEEPROM() {
    Serial.println("clearing EEPROM");
    for (int i = 0; i < (ssidSize + passSize + apiKeySize + longTypeSize); i++) {
        Serial.println("Clear EEPROM on address: " + String(i));

        //disabled for debugging purposes
        //EEPROM.write(i, 0);
    }
}

void writeDataToEEPROM(String ssid, String password, String apiKey, unsigned long updateInterval) {
    clearEEPROM();

    Serial.println("writing eeprom ssid:");
    writeStringToEEPROM(ssid, 0, ssidSize);
    Serial.println("writing eeprom pass:");
    writeStringToEEPROM(password, ssidSize, passSize);
    Serial.println("writing eeprom apiKey:");
    writeStringToEEPROM(apiKey, passSize + ssidSize, apiKeySize);
    Serial.println("writing eeprom updateInterval:");
    writeLongToEEPROM(updateInterval, passSize + ssidSize + apiKeySize);

    //disabled for debugging purposes
    //EEPROM.commit();
}

// === wifi section
byte connectToAp() {
    byte status = WiFi.status();

    if (status == WL_DISCONNECTED || status == WL_CONNECTION_LOST) {

        WiFi.mode(WIFI_STA); //connect to other AP mode
        //WiFi.config(ip, dns, gateway);
        WiFi.begin(ssid.c_str(), password.c_str());

        Serial.println();
        unsigned long startTime = millis();
        while ((WiFi.status() == WL_DISCONNECTED) && (millis() - startTime < wifiConnectTimeout)) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            Serial.println("WiFi connected");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            return wifiConnectionOK;
        } else if (WiFi.status() == WL_DISCONNECTED) {
            Serial.println();
            Serial.println("WiFi connection timeout");
            return wifiConnectionTimeout;
        } else {
            Serial.println();
            Serial.println("WiFi connection failed");
            Serial.println("Reason: " + String(WiFi.status()));
            return wifiConnectionFailed;
        }
    }

    return wifiConnectionOK;
}

String getWiFiNetworks() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("Scanning for Wi-Fi networks");
    int n = WiFi.scanNetworks();

    String st = "<ul><li>no networks found</li></ul>";

    if (n > 0) {
        Serial.println(String(n) + " networks found");

        st = "<ul>";
        for (int i = 0; i < n; i++) {
            // Print SSID and RSSI for each network found
            st += "<li>" + String(i + 1) + ": " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + ")";
            st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
            st += "</li>";
        }

        st += "</ul>";
    }

    Serial.println(st);

    return st;
}

// == Web server stuff

String getHome() {
    IPAddress ip = WiFi.softAPIP();
    String ipStr = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/html\r\n\r\n";
    response += "<!DOCTYPE HTML>\r\n";
    response += "<html>Hello from ESP8266 at " + ipStr;
    response += "<body><p>" + scannedWifiNetworks + "</p>";
    response += "<form method='get' action='setup'>";
    response += "<label>SSID: </label><input name='ssid' length='" + String(ssidSize) + "'>";
    response += "<label>Password: </label><input name='pass' length='" + String(passSize) + "'>";
    response += "<label>Thingspeak API key: </label><input name='apiKey' length='" + String(apiKeySize) + "'>";
    response += "<label>Update interval (msec): </label><input name='interval' length='20' value='50'>";
    response += "<input type='submit'>";
    response += "</form>";
    response += "</body></html>\r\n\r\n";
        
    Serial.println("Serving /");

    return response;
}

String getSetup(String path) {
    // /setup?ssid=blahhhh&pass=poooo&apiKey=WERADFAD&interval=50

    String ssid = path.substring(path.indexOf('='), path.indexOf('&'));
    Serial.println("SSID: " + ssid);

    path = path.substring(path.indexOf('&'));

    String password = path.substring(path.indexOf('='), path.indexOf('&'));
    Serial.println("PASS: " + password);

    path = path.substring(path.indexOf('&'));

    String thingspeakApiKey = path.substring(path.indexOf('='), path.indexOf('&'));
    Serial.println("API Key: " + thingspeakApiKey);

    unsigned long thingSpeakUpdateInterval = atol(path.substring(path.indexOf('=')).c_str());
    Serial.println("Update interval: " + String(thingSpeakUpdateInterval));

    writeDataToEEPROM(ssid, password, thingspeakApiKey, thingSpeakUpdateInterval);

    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/html\r\n\r\n";
    response += "<!DOCTYPE HTML>\r\n";
    response += "<html>Hello from ESP8266 <p> saved to EEPROM...";
    response += " reset to boot into new wifi</p></html>\r\n\r\n";

    Serial.println("Serving /setup");
    return response;
}

String getNotFound() {
    String response = "HTTP/1.1 404 Not Found\r\n\r\n";
    Serial.println("Sending 404");

    return response;
}

String getBadRequest() {
    String response = "HTTP/1.1 400 Bad Request\r\n\r\n";
    Serial.println("Sending 400");
    return response;
}

void serveAsHttp() {
    // Check for any mDNS queries and send responses
    mdns.update();

    // Check if a client has connected
    WiFiClient client = server.available();
    if (!client) {
        return;
    }

    Serial.println();
    Serial.println("New client found.");

    // Wait for data from client to become available
    while(client.connected() && !client.available()){
        delay(1);
    }

    // First line of HTTP request looks like "GET /path HTTP/1.1"
    String request = client.readStringUntil('\r');

    // Get request path
    int addr_start = request.indexOf(' ');
    int addr_end = request.indexOf(' ', addr_start + 1);
    if (addr_start == -1 || addr_end == -1) {
        Serial.println("Invalid request: " + request);
        client.print(getBadRequest());
        return;
    }

    String path = request.substring(addr_start + 1, addr_end);
    Serial.print("Request path: " + path);

    //stop receiving data from client
    client.flush();


    //Response to request
    String response;

    if (request == "/") {
        response = getHome();
    } else if ( request.startsWith("/setup?ssid=") ) {
        response = getSetup(path);
    } else {
        response = getNotFound();
    }

    client.print(response);
    return;
}

void launchWebServer() {

    //Start mDNS
    if (!mdns.begin(dnsName, WiFi.softAPIP())) {
        Serial.println("Error setting up mDNS responder!");
        while(true) {
            delay(1000);
        }
    }

    Serial.println("mDNS responder started");

    // Start the server
    server.begin();
    Serial.println("Server started");

    while(true) {
        serveAsHttp();
        delay(0); //yield
    }
}

void setupAP() {

    scannedWifiNetworks = getWiFiNetworks();
    delay(100);

    WiFi.softAP(String(apSSID).c_str());
    Serial.println("Connected in soft AP mode with ssid: " + String(apSSID));
    Serial.println("IP: " + WiFi.softAPIP());

    //Start HTTP server
    launchWebServer();
}

// == DHTxx section
void printSensorData(float temperature, float humidity, float heatIndex) {
    Serial.println();
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" *C ");
    Serial.print("Heat index: ");
    Serial.print(heatIndex);
    Serial.print(" *C ");
}

// == Thingspeak section
boolean postData(float temperature, float humidity, float heatIndex) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Can not send data - WiFi not connected");
        return false;
    }

    if (!client.connect(thingSpeakAddress, thingSpeakHttpPort)) {
        Serial.println("Can not send data - connection failed");
        return false;
    }

    String postString =
            "field1=" + String(temperature) +
            "&field2=" + String(humidity) +
            "&field3=" + String(heatIndex);

    client.println("POST " + String(thingSpeakUpdateJsonEndpoint) + " HTTP/1.1");
    client.println("Host: " + String(thingSpeakAddress));
    client.println("Connection: close");
    client.println("X-THINGSPEAKAPIKEY: " + String(thingspeakApiKey));
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: " + String(postString.length()));
    client.print("\r\n\r\n");
    client.print(postString);

    delay(500);

    boolean result = false;

    // Read all the lines of the reply from server and print them to Serial
    while (client.available()) {
        String line = client.readStringUntil('\r');
        if (line.indexOf("Status: ") > -1) {
            Serial.println(line);
            if (line.indexOf("200 OK") > -1) {
                result = true;
            }
        }
    }

    Serial.println();
    Serial.println("closing connection");

    return result;
}

// == generic setup and loop section
void setup() {
    Serial.begin(115200);
    dht.begin();

    EEPROM.begin(512);
    delay(10);
    readDataFromEEPROM();

    if (ssid.length() > 0) {
        wifiConnectionStatus = connectToAp();
    }

    if (wifiConnectionStatus != wifiConnectionOK) {
        setupAP();
    }
}

void loop() {
    while (connectToAp() != wifiConnectionOK) {
        Serial.println("Connection to Wi-Fi failed. Next attempt will be in " +
        String(wifiFailedAttempt / 1000) +
        " sec. Press reset to go AP setup if there are a lot of failed attempts");
        delay(wifiFailedAttempt);
    }

    //delay before DHT measurements
    //do not need this since connection to AP will take more than two seconds
    //delay(2000);

    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float t = dht.readTemperature();

    // Check if any reads failed and exit early (to try again).
    if (!isnan(h) && !isnan(t)) {
        // Compute heat index in Celsius (isFahreheit = false)
        float hic = dht.computeHeatIndex(t, h, false);

        printSensorData(t, h, hic);

        //setup wifi
        //in loop to re init connection if necessary
        //do not need this since connection will be started in setup section
        //setupWifi();

        postData(t, h, hic);
    } else {
        Serial.println();
        Serial.println("Failed to read from DHT sensor!");
    }

    //to reduce power consumption
    //could be replaced with deep sleep
    //but still not enough to work for a long time from battery
    WiFi.disconnect();

    // deep sleep between measurements.
    // for lower power consumptions
    //Serial.println("Going to deep sleep for " + String(thingSpeakUpdateInterval / (1000 * 1000)) + " sec.");
    //Need to connect GPIO16 to RST otherwise deep sleep will not work
    //ESP.deepSleep(thingSpeakUpdateInterval, WAKE_RF_DEFAULT);

    delay(thingSpeakUpdateInterval);
}



