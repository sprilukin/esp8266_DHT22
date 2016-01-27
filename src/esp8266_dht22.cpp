#include <WiFiClient.h>
#include <WiFiManager.h>
#include "DHT.h"
#include <EEPROM.h>

//Thingspeak setup
String thingspeakApiKey = "";
unsigned int thingSpeakUpdateInterval = 120; //in seconds

//Thingspeak connection settings
//#define thingSpeakAddressUri "api.thingspeak.com"
byte thingSpeakAddressIp[] = {184, 106, 153, 149};
#define thingSpeakAddressIpAsString "184.106.153.149"
#define thingSpeakUpdateJsonEndpoint "/update.json"
#define thingSpeakHttpPort 80

//DHT22
#define DHTTYPE DHT22
#define DHTPIN 13
DHT dht(DHTPIN, DHTTYPE);

//to read voltage
ADC_MODE(ADC_VCC);

//WiFi
WiFiClient client;

unsigned int timingsMeasurement = 0;

//flag for saving data
bool shouldSaveConfig = false;


void readDataFromEEPROM() {
 char apiKey[17];
 byte address = 0;

 for (byte i = 0; i < 16; i++) {
    apiKey[i] = EEPROM.read(i);
 }

 apiKey[16] = '\0';

 thingspeakApiKey = String(apiKey);

 thingSpeakUpdateInterval = 0;

 for (byte i = 16; i < 20; i++) {
     thingSpeakUpdateInterval = thingSpeakUpdateInterval | (EEPROM.read(i) << (i - 16));
 }
}

void writeDataToEEPROM() {
 const char *apiKey = thingspeakApiKey.c_str();
 byte address = 0;

 for (byte i = 0; i < 16; i++) {
     EEPROM.write(i, apiKey[i]);
 }

 for (byte i = 16; i < 20; i++) {
     EEPROM.write(i, (thingSpeakUpdateInterval >> (i - 16)) & 255);
 }

 EEPROM.commit();
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
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

    if (!client.connect(thingSpeakAddressIp, thingSpeakHttpPort)) {
        Serial.println("Can not send data - connection failed");
        return false;
    }

    String postString =
            "field1=" + String(temperature) +
            "&field2=" + String(humidity) +
            "&field3=" + String(heatIndex) +
            "&field4=" + String(ESP.getVcc() / 1000.0);

    Serial.println();
    Serial.println("POST " + String(thingSpeakUpdateJsonEndpoint) + " HTTP/1.1");
    client.println("POST " + String(thingSpeakUpdateJsonEndpoint) + " HTTP/1.1");
    Serial.println("Host: " + String(thingSpeakAddressIpAsString));
    client.println("Host: " + String(thingSpeakAddressIpAsString));
    Serial.println("Connection: close");
    client.println("Connection: close");
    Serial.println("X-THINGSPEAKAPIKEY: " + String(thingspeakApiKey));
    client.println("X-THINGSPEAKAPIKEY: " + String(thingspeakApiKey));
    Serial.println("Content-Type: application/x-www-form-urlencoded");
    client.println("Content-Type: application/x-www-form-urlencoded");
    Serial.print("Content-Length: " + String(postString.length()));
    client.print("Content-Length: " + String(postString.length()));
    Serial.print("\r\n\r\n");
    client.print("\r\n\r\n");
    Serial.println(postString);
    client.print(postString);

    boolean result = false;

    delay(500);

    // Read all the lines of the reply from server and print them to Serial
    while (client.available() && !result) {
        String line = client.readStringUntil('\n');
        if (line.indexOf("Status: ") > -1) {
            if (line.indexOf("200 OK") > -1) {
                result = true;
            }
        }
    }

    Serial.println("Result:" + String(result ? "Ok" : "Failed"));

    return result;
}

// == generic setup and loop section
void setup() {
  Serial.begin(115200);
  dht.begin();
  EEPROM.begin(512);
  timingsMeasurement = millis();

  readDataFromEEPROM();

  Serial.println();
  Serial.println("Api key from EEPROM: " + String(thingspeakApiKey));
  Serial.println("Update interval from EEPROM: " + String(thingSpeakUpdateInterval));

  WiFiManager wifiManager;

  //Custom parameters
  WiFiManagerParameter thingSpeakKey("key", "Thingspeak key", thingspeakApiKey.c_str(), 17);
  WiFiManagerParameter updateInterval("updateInterval", "Update interval", String(thingSpeakUpdateInterval).c_str(), 12);

  wifiManager.addParameter(&thingSpeakKey);
  wifiManager.addParameter(&updateInterval);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(240);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect("esp8266", "esp8266")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.deepSleep(10 * 60 * 1000 * 1000, WAKE_RF_DEFAULT);
  }

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

    //save the custom parameters to EEPROM
    if (shouldSaveConfig) {
      //read updated parameters
      thingspeakApiKey = String(thingSpeakKey.getValue());
      thingSpeakUpdateInterval = atol(String(updateInterval.getValue()).c_str());

      writeDataToEEPROM();
    }

    EEPROM.end();
}

void loop() {
    //delay after initialization but before DHT measurements should be at least two seconds.
    timingsMeasurement = millis() - timingsMeasurement;
    Serial.println("Time to startup: " + String(timingsMeasurement) + " ms");
    if (timingsMeasurement < 2000) {
        delay(2000 - timingsMeasurement);
    }

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

        postData(t, h, hic);
    } else {
        Serial.println();
        Serial.println("Failed to read from DHT sensor!");
    }

    timingsMeasurement = millis() - timingsMeasurement;
    Serial.println("Full loop time: " + String(timingsMeasurement) + " ms");

    // deep sleep between measurements.
    // for lower power consumptions
    //Serial.println("Going to deep sleep for " + String(thingSpeakUpdateInterval / (1000 * 1000)) + " sec.");
    //Need to connect GPIO16 to RST otherwise deep sleep will not work
    ESP.deepSleep(thingSpeakUpdateInterval * 1000 * 1000, WAKE_RF_DEFAULT);
}
