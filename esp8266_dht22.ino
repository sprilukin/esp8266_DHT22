#include <ESP8266WiFi.h>
#include "DHT.h"

//WiFi setup
#define ssid       "......."
#define password   "......."

//Thingspeak setup
#define thingspeakApiKey "................"
#define thingSpeakAddress "api.thingspeak.com"
#define thingSpeakUpdateJsonEndpoint "/update.json"
#define thingSpeakHttpPort 80
//Update data for the specific channel not frequently than 16 sec
#define thingSpeakUpdateInterval 16000
//Update data for the specific channel if it's the same not frequently than 5 mins
#define thingSpeakUpdateSameDataInterval 300000 //5 min

//DHT22
#define DHTTYPE DHT22
#define DHTPIN 13
DHT dht(DHTPIN, DHTTYPE);

//wifi client
WiFiClient client;
//byte lastClientStatus = WL_DISCONNECTED;
//#define deepSleepTime 120000000 //2 minutes
//#define wakeTime 120000 //2 minutes 

// === wifi section
void setupWifi() {
    byte status = WiFi.status();

    if (status == WL_DISCONNECTED || status == WL_CONNECTION_LOST) {

        //turnWifiInProgressLedState();

        WiFi.begin(ssid, password);

        while (WiFi.status() == WL_DISCONNECTED) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("");
            Serial.println("WiFi connected");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            //turnWifiOkLedState();
        } else {
            Serial.println("");
            Serial.println("WiFi connection failed");
            Serial.println("Reason: " + String(WiFi.status()));
            //turnWifiErrorLedState();
        }
    }
}

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

void setup() {
    Serial.begin(115200);
    dht.begin();
}

void loop() {
  //setup wifi
  //in loop to re init connection if necessary
  setupWifi();

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (!isnan(h) && !isnan(t)) {
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(t, h, false);
  
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.print(" *C ");
    Serial.print("Heat index: ");
    Serial.print(hic);
    Serial.print(" *C ");
    
    postData(t, h, hic);    
  } else {
    Serial.println("Failed to read from DHT sensor!");
  }

     // Wait a few seconds between measurements.
  delay(30000);
}
