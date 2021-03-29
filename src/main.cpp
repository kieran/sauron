#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <mDNS.h>
#include <PubSubClient.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <sstream>

#define NETWORK_SSID  "The LAN before time"
#define NETWORK_PASS  "password"
#define HOSTNAME      "sauron"
#define MQTT_SERVER   "10.0.1.126" // the IP of your local MQTT server
#define SCAN_TIME     10 // seconds
#define BLE_FILTER    "THS_"
#define DEBUG         false

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// creates a topic name for a given sensor / attr pair
std::string sensorTopic(std::string device, std::string attr) {
  return "sensors/" + device + "/" + attr;
}

// publishes a sensor value to the MQTT server
void publish(std::string device, std::string attr, float value) {
  char val_string[8];
  dtostrf(value, 1, 2, val_string);
  Serial.printf("publishing data to %s: %s\n", sensorTopic(device, attr).c_str(), val_string);
  mqttClient.publish(sensorTopic(device, attr).c_str(), val_string);
}

/*
BLE stuff
*/
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName() && advertisedDevice.haveServiceData() && !advertisedDevice.getName().find(BLE_FILTER)) {
      std::string sensorName = advertisedDevice.getName();
      std::string strServiceData = advertisedDevice.getServiceData(0);

      Serial.printf("\n\nAdvertised Device: %s\n", sensorName.c_str());

      uint8_t cServiceData[100];
      char charServiceData[100];
      strServiceData.copy((char *)cServiceData, strServiceData.length(), 0);
      for (int i=0;i<strServiceData.length();i++) {
        sprintf(&charServiceData[i*2], "%02x", cServiceData[i]);
      }

      std::stringstream ss;
      ss << "fe95" << charServiceData;
      Serial.print("Payload:");
      Serial.println(ss.str().c_str());
      if (DEBUG) Serial.printf("Payload length: %d\n", strServiceData.length());

      unsigned long temperature, humidity, battery;
      char charValue[5] = {0,};

      // there are 3 formats these things broadcast, depending on the firmware you use
      // ATC is the original "custom firmware" - it's the one I prefer
      // there's also a custom format from the PVVX firmware: https://github.com/pvvx/ATC_MiThermometer
      // and finally the broadcast format for the stock Xiaomi firmware
      switch (strServiceData.length()) {

        // ATC format
        case 13:
          sprintf(charValue, "%02X%02X", cServiceData[6], cServiceData[7]);
          temperature = strtol(charValue, 0, 16);
          if (DEBUG) Serial.printf("ATC Temperature: %s c\n", String((float)temperature/10,1).c_str());
          publish(sensorName, "temperature", (float)temperature/10);

          sprintf(charValue, "%02X", cServiceData[8]);
          humidity = strtol(charValue, 0, 16);
          if (DEBUG) Serial.printf("ATC Humidity: %s %%\n", String((float)humidity,1).c_str());
          publish(sensorName, "humidity", (float)humidity);

          sprintf(charValue, "%02X", cServiceData[9]);
          battery = strtol(charValue, 0, 16);
          if (DEBUG) Serial.printf("ATC Battery: %s %%\n", String((float)battery,0).c_str());
          publish(sensorName, "battery", (float)battery);

          break;

        // pvvx format
        case 15:
          // TODO - implement https://github.com/pvvx/ATC_MiThermometer#bluetooth-advertising-formats
          break;

        // Original Xiaomi format
        default: // 18

          switch (cServiceData[11]) {
            // temp only? Never see this
            case 0x04:
              sprintf(charValue, "%02X%02X", cServiceData[15], cServiceData[14]);
              temperature = strtol(charValue, 0, 16);
              publish(sensorName, "temperature", (float)temperature/10);

              break;

            // humi only? Never see this either
            case 0x06:
              sprintf(charValue, "%02X%02X", cServiceData[15], cServiceData[14]);
              humidity = strtol(charValue, 0, 16);
              if (DEBUG) Serial.printf("HUMIDITY_EVENT: %s, %lu\n", charValue, humidity);
              publish(sensorName, "humidity", (float)humidity/10);

              break;

            // battery data - this seems to be every other Xiaomi advertisement
            case 0x0A:
              sprintf(charValue, "%02X", cServiceData[14]);
              battery = strtol(charValue, 0, 16);
              if (DEBUG) Serial.printf("Battery: %s %%\n", String((float)battery,0).c_str());
              publish(sensorName, "battery", (float)battery);

              break;

            // temp + humi data - this seems to be every other Xiaomi advertisement
            case 0x0D:
              sprintf(charValue, "%02X%02X", cServiceData[15], cServiceData[14]);
              temperature = strtol(charValue, 0, 16);
              publish(sensorName, "temperature", (float)temperature/10);

              sprintf(charValue, "%02X%02X", cServiceData[17], cServiceData[16]);
              humidity = strtol(charValue, 0, 16);
              publish(sensorName, "humidity", (float)humidity/10);

              break;
          }
          break;
      }
    }
  }
};

void bleScan() {
  BLEScan* scan = BLEDevice::getScan(); //create new scan
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setActiveScan(true); //active scan uses more power, but get results faster
  scan->setInterval(0xA0);
  scan->setWindow(0x30);
  BLEScanResults foundDevices = scan->start(SCAN_TIME);
}


/*
webserver stuff
*/

WebServer webserver(80);

// reads LED state
bool led(void){
  return digitalRead(LED_BUILTIN) == HIGH;
}

// writes LED state
bool led(bool state){
  digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
  return led();
}

// LED state as a String
String s_led(void){
  return led() ? "ON" : "OFF";
}

// route: /
void handleRoot() {
  led(true);
  webserver.send(200, "text/plain", "hello from esp32!");
  led(false);
}

// route: 404
void handleNotFound() {
  led(true);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += webserver.uri();
  message += "\nMethod: ";
  message += (webserver.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webserver.args();
  message += "\n";
  for (uint8_t i = 0; i < webserver.args(); i++) {
    message += " " + webserver.argName(i) + ": " + webserver.arg(i) + "\n";
  }
  webserver.send(404, "text/plain", message);
  led(false);
}

void setup(void) {

  // set the internal LED as an output - not sure why?
  pinMode(LED_BUILTIN, OUTPUT);

  // turn the LED off (it defaults to on)
  led(false);

  // init logging interface over serial port
  Serial.begin(115200);

  // init the BLE device
  BLEDevice::init("");

  // init the wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(NETWORK_SSID, NETWORK_PASS);

  // Wait for the wifi connection
  Serial.println(""); // a vertical space
  while (WiFi.status() != WL_CONNECTED) {
    // print 1 dot every half second while we're trying to connect
    delay(500);
    Serial.print(".");
  }

  // wifi is connected - print debug info
  Serial.println(""); // a vertical space
  // really wish we could do these in one println each,
  // but it freaks out about mixing types ¯\_(ツ)_/¯
  Serial.printf("Connected to %s\nIP address: %s\n", NETWORK_SSID, WiFi.localIP().toString().c_str());

  // init kona MQTT
  mqttClient.setServer(MQTT_SERVER, 1883);

  // this advertises the device locally at "sauron.local"
  mdns_init();
  //set hostname
  mdns_hostname_set(HOSTNAME);
  //set default instance
  mdns_instance_name_set("Sauron");
  Serial.printf("MDNS responder started at http://%s.local\n", HOSTNAME);

  //
  // define webserver routes
  //
  webserver.on("/", handleRoot);

  // looks like `[](){...}` is c++ speak for a lambda function
  webserver.on("/on", []() {
    led(true);
    webserver.send(200, "text/plain", "LED is now " + s_led());
  });

  webserver.on("/off", []() {
    led(false);
    webserver.send(200, "text/plain", "LED is now " + s_led());
  });

  webserver.on("/toggle", []() {
    // toggle the LED
    led(!led());
    // send a message to the browser including the new state
    webserver.send(200, "text/plain", "TOGGLE: LED is now " + s_led());
  });

  webserver.onNotFound(handleNotFound);

  //
  // Start the web server
  //
  webserver.begin();
  Serial.println("HTTP server started");
}

void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("connected");
      // Subscribe
      // mqttClient.subscribe("esp32/output");
    } else {
      Serial.printf("failed, rc=%d try again in 5 seconds", mqttClient.state());
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  if (!mqttClient.connected()) mqtt_reconnect();
  mqttClient.loop();

  webserver.handleClient();
  // MDNS.update(); // can't find the equiv in mdns library, do I need to do this?

  bleScan();
}
