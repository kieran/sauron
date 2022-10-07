#include <Arduino.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
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
#define SCAN_TIME     10 // seconds
#define BLE_FILTER    "THS_"
#define LOW_MEM_LIMIT 10000 // bytes
#define WDT_TIMEOUT   120 // seconds
#define DEBUG         false

/*
  data[sensorName][attribute] = value

  this is our global cache of values
*/
std::map<string,std::map<string,float>> data;

float roundTo(float value, int prec) {
  float pow_10 = pow(10.0f, (float)prec);
  return round(value * pow_10) / pow_10;
}

// records a sensor value in memory
void record(string device, string attr, float value) {
  data[device][attr] = value;
}

/*
BLE stuff
*/
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName() && advertisedDevice.haveServiceData()) { // && !advertisedDevice.getName().find(BLE_FILTER)
      try {
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

        signed long temperature;
        unsigned long humidity, battery;
        char charValue[5] = {0,};

        // there are 3 formats these things broadcast, depending on the firmware you use
        // ATC is the original "custom firmware" - it's the one I prefer
        // there's also a custom format from the PVVX firmware: https://github.com/pvvx/ATC_MiThermometer
        // and finally the broadcast format for the stock Xiaomi firmware
        switch (strServiceData.length()) {

          
          case 13: // ATC format
            sprintf(charValue, "%02X%02X", cServiceData[6], cServiceData[7]);
            temperature = strtol(charValue, 0, 16);
            if (temperature >= 0x8000) temperature -= 0xFFFF; // handle negative numbers
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
      } catch (...) {
        // oops
      }
    }
  }
};

void bleScan() {
  if (DEBUG) Serial.print("\nScanning for BLE devices...");
  BLEDevice::getScan()->stop(); // stop any in-progreess scans (not sure if needed)
  BLEDevice::getScan()->start(SCAN_TIME); // scan for SCAN_TIME seconds
  if (DEBUG) Serial.print("done\n");
}

bool lowMemory() {
  return heap_caps_get_free_size(MALLOC_CAP_8BIT) < LOW_MEM_LIMIT;
}

bool disconnected() {
  return WiFi.status() != WL_CONNECTED;
}

void readLoop(void * pvParameters){
  if (DEBUG) Serial.printf("BLE Loop running on core %d\n", xPortGetCoreID());

  // enable panic so ESP32 restarts
  esp_task_wdt_init(WDT_TIMEOUT, true);
  // subscribe this task to the watchdog timer
  esp_task_wdt_add(NULL);

  for(;;){
    if (lowMemory()) ESP.restart();
    // scan for BLE devices
    bleScan();
    // feed the watchdog timer
    esp_task_wdt_reset();
  }
}

TaskHandle_t ReadTask;

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
  BLEScan* scan = BLEDevice::getScan(); // find or create new scan
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setActiveScan(true); //active scan uses more power, but get results faster
  scan->setInterval(0xA0);
  scan->setWindow(0x30);

  // init the wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(NETWORK_SSID, NETWORK_PASS);

  // Wait for the wifi connection
  Serial.println(""); // a vertical space
  while (disconnected()) {
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

  // /sensors/THS_OFFICE
  // responds with JSON data, as expected by github.com/ingowalther/homebridge-advanced-http-temperature-humidity
  webserver.on(UriBraces("/sensors/{}"), []() {
    String sensor = webserver.pathArg(0);
    bool prev = false;
    led(true);
    stringstream message;
    message << "{";
    if (data[sensor.c_str()]["temperature"]) {
      if (prev) message << ",";
      message << "\n  \"temperature\": " << data[sensor.c_str()]["temperature"];
      prev = true;
    }
    if (data[sensor.c_str()]["humidity"]) {
      if (prev) message << ",";
      message << "\n  \"humidity\": " << data[sensor.c_str()]["humidity"];
      prev = true;
    }
    if (data[sensor.c_str()]["battery"]) {
      if (prev) message << ",";
      message << "\n  \"battery\": " << data[sensor.c_str()]["battery"];
    }
    message << "\n}\n";
    webserver.send(200, "text/plain", message.str().c_str());
    led(false);
  });

  // responds with all data in a format prometheus expects
  webserver.on("/metrics", []() {
    led(true);
    stringstream message;

    // report the free mempory on the ESP32 device
    char mem[256];
    snprintf(mem, sizeof mem, "%zu", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    message << "# HELP free_memory Free memory in the ESP32.\n# TYPE free_memory gauge\n";
    message << "free_memory " << (string)mem << '\n';

    // report uptime (time since last reboot) in s
    message << "# HELP uptime Uptime in seconds.\n# TYPE uptime counter\n";
    message << "uptime " << int(millis() / 1000) << '\n';

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

  // Pin the sensor loop to the other core (0)
  xTaskCreatePinnedToCore(
    readLoop,    /* Task function. */
    "ReadTask",  /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &ReadTask,   /* Task handle to keep track of created task */
    0            /* pin task to core 0 */
  );
}

void loop() {
  if (disconnected()) ESP.restart();
  webserver.handleClient();
}
