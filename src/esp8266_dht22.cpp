#include <WiFiClient.h>
#include <WiFiManager.h>
#include "DHT.h"

//Thingspeak setup
char thingspeakApiKey[17];
char thingSpeakUpdateInterval[11] = "120"; //in seconds

//Thingspeak connection settings
#define thingSpeakAddress "api.thingspeak.com"
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
            "&field3=" + String(heatIndex) +
            "&field4=" + String(ESP.getVcc() / 1000.0);

    client.println("POST " + String(thingSpeakUpdateJsonEndpoint) + " HTTP/1.1");
    client.println("Host: " + String(thingSpeakAddress));
    client.println("Connection: close");
    client.println("X-THINGSPEAKAPIKEY: " + String(thingspeakApiKey));
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: " + String(postString.length()));
    client.print("\r\n\r\n");
    client.print(postString);

    boolean result = false;

    // Read all the lines of the reply from server and print them to Serial
    while (client.available()) {
        String line = client.readStringUntil('\r');
        if (line.indexOf("Status: ") > -1) {
            if (line.indexOf("200 OK") > -1) {
                result = true;
                break;
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

  WiFiManager wifiManager;

  //Custom parameters
  WiFiManagerParameter thingSpeakKey("key", "Thingspeak key", thingspeakApiKey, 17);
  WiFiManagerParameter updateInterval("updateInterval", "Update interval", thingSpeakUpdateInterval, 11);

  wifiManager.addParameter(&thingSpeakKey);
  wifiManager.addParameter(&updateInterval);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(240);

  if (!wifiManager.autoConnect("esp8266", "esp8266")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.deepSleep(10 * 60 * 1000 * 1000, WAKE_RF_DEFAULT);
  }

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(thingspeakApiKey, thingSpeakKey.getValue());
  strcpy(thingSpeakUpdateInterval, updateInterval.getValue());
}

void loop() {
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

        postData(t, h, hic);
    } else {
        Serial.println();
        Serial.println("Failed to read from DHT sensor!");
    }

    // deep sleep between measurements.
    // for lower power consumptions
    //Serial.println("Going to deep sleep for " + String(thingSpeakUpdateInterval / (1000 * 1000)) + " sec.");
    //Need to connect GPIO16 to RST otherwise deep sleep will not work
    ESP.deepSleep(atol(thingSpeakUpdateInterval) * 1000 * 1000, WAKE_RF_DEFAULT);
}
