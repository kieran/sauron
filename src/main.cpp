#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <mDNS.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <sstream>

using namespace std;

#define NETWORK_SSID  "The LAN before time"
#define NETWORK_PASS  "password"
#define HOSTNAME      "sauron"
#define MQTT_SERVER   "10.0.1.126" // the IP of your local MQTT server
#define SCAN_TIME     10 // seconds
#define BLE_FILTER    "THS_"
#define DEBUG         false


std::map<string,std::map<string,float>> data;

// records a sensor value in memory
void record(string device, string attr, float value) {
  data[device][attr] = value;
}

/*
BLE stuff
*/
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName() && advertisedDevice.haveServiceData() && !advertisedDevice.getName().find(BLE_FILTER)) {
      string sensorName = advertisedDevice.getName();
      string strServiceData = advertisedDevice.getServiceData(0);

      if (DEBUG) Serial.printf("\n\nAdvertised Device: %s\n", sensorName.c_str());

      uint8_t cServiceData[100];
      char charServiceData[100];
      strServiceData.copy((char *)cServiceData, strServiceData.length(), 0);
      for (int i=0;i<strServiceData.length();i++) {
        sprintf(&charServiceData[i*2], "%02x", cServiceData[i]);
      }

      stringstream payload;
      payload << "fe95" << charServiceData;
      if (DEBUG) Serial.printf("Payload: %s\n", payload.str().c_str());
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
          record(sensorName, "temperature", (float)temperature/10);

          sprintf(charValue, "%02X", cServiceData[8]);
          humidity = strtol(charValue, 0, 16);
          if (DEBUG) Serial.printf("ATC Humidity: %s %%\n", String((float)humidity,1).c_str());
          record(sensorName, "humidity", (float)humidity);

          sprintf(charValue, "%02X", cServiceData[9]);
          battery = strtol(charValue, 0, 16);
          if (DEBUG) Serial.printf("ATC Battery: %s %%\n", String((float)battery,0).c_str());
          record(sensorName, "battery", (float)battery);

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
              record(sensorName, "temperature", (float)temperature/10);

              break;

            // humi only? Never see this either
            case 0x06:
              sprintf(charValue, "%02X%02X", cServiceData[15], cServiceData[14]);
              humidity = strtol(charValue, 0, 16);
              if (DEBUG) Serial.printf("HUMIDITY_EVENT: %s, %lu\n", charValue, humidity);
              record(sensorName, "humidity", (float)humidity/10);

              break;

            // battery data - this seems to be every other Xiaomi advertisement
            case 0x0A:
              sprintf(charValue, "%02X", cServiceData[14]);
              battery = strtol(charValue, 0, 16);
              if (DEBUG) Serial.printf("Battery: %s %%\n", String((float)battery,0).c_str());
              record(sensorName, "battery", (float)battery);

              break;

            // temp + humi data - this seems to be every other Xiaomi advertisement
            case 0x0D:
              sprintf(charValue, "%02X%02X", cServiceData[15], cServiceData[14]);
              temperature = strtol(charValue, 0, 16);
              record(sensorName, "temperature", (float)temperature/10);

              sprintf(charValue, "%02X%02X", cServiceData[17], cServiceData[16]);
              humidity = strtol(charValue, 0, 16);
              record(sensorName, "humidity", floor((float)humidity/10));

              break;
          }
          break;
      }
    }
  }
};

BLEScan* scan;
BLEScanResults foundDevices;

void bleScan() {
  if (DEBUG) Serial.print("\nScanning for BLE devices...");
  scan = BLEDevice::getScan(); //create new scan
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setActiveScan(true); //active scan uses more power, but get results faster
  scan->setInterval(0xA0);
  scan->setWindow(0x30);
  foundDevices = scan->start(SCAN_TIME);
  if (DEBUG) Serial.print("done\n");
}

void bleLoop(void * pvParameters){
  if (DEBUG) Serial.printf("BLE Loop running on core %d\n", xPortGetCoreID());

  for(;;){
    try {
      bleScan();
      // delay(SCAN_TIME * 1000);
    } catch (int myNum) {
      if (DEBUG) Serial.println("\nError scanning");
    }
  }
}

TaskHandle_t BleTask;

// init webserver on port 80
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

  Serial.printf("\nConnected to %s\nIP address: %s\n", NETWORK_SSID, WiFi.localIP().toString().c_str());

  // this advertises the device locally at "sauron.local"
  mdns_init();
  //set hostname
  mdns_hostname_set(HOSTNAME);
  //set default instance
  mdns_instance_name_set(HOSTNAME);
  Serial.printf("MDNS responder started at http://%s.local\n", HOSTNAME);

  //
  // define webserver routes
  //
  webserver.on("/", []() {
    webserver.send(200, "text/plain", "hello from esp32!");
  });

  webserver.on("/metrics", []() {
    led(true);
    stringstream message;

    // not sure if these are needed by prometheus ¯\_(ツ)_/¯
    message << "# HELP temperature Temperature of the sensor in degrees Celcius.\n# TYPE temperature gauge\n";
    message << "# HELP humidity Relative humidity of the sensor as a percentage.\n# TYPE humidity gauge\n";
    message << "# HELP battery Battery state of charge as a percentage.\n# TYPE battery gauge\n";

    std::map<string, std::map<string, float>>::iterator itOuter;
    std::map<string, float>::iterator itInner;

    for(itOuter=data.begin(); itOuter!=data.end(); ++itOuter){
      for(itInner=itOuter->second.begin(); itInner!=itOuter->second.end(); ++itInner){
        message << itInner->first << "{sensor=\"" << itOuter->first << "\"} " << itInner->second << '\n';
      }
    }

    webserver.send(200, "text/plain", message.str().c_str());
    led(false);
  });

  webserver.onNotFound([]() {
    webserver.send(404, "text/plain", "Not found");
  });


  //
  // Start the web server
  //
  webserver.begin();
  Serial.println("HTTP server started");

  //
  // Pin the Bluetooth Scanning loop to the other core
  //
  xTaskCreatePinnedToCore(
    bleLoop,     /* Task function. */
    "BleTask",   /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &BleTask,    /* Task handle to keep track of created task */
    0            /* pin task to core 0 */
  );
}

void loop() {
  webserver.handleClient();
}
