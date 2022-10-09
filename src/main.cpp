#include <Arduino.h>
#include <WiFi.h>
#include <mDNS.h>

#include <WebServer.h>
#include <uri/UriBraces.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <Wire.h>
#include "SHTSensor.h"
SHTSensor sht;

#include <sstream>

using namespace std;

// #define DEBUG true // <-- uncomment to get serial logs
#include "log.h"

// wifi credentials
#define NETWORK_SSID  "The LAN before time"
#define NETWORK_PASS  "password"
#define HOSTNAME      "sauron"

// misc config
#define SCAN_TIME     10          // BLE scan time, in seconds
#define BLE_FILTER    "THS_"      // only attempt parsing data from sensors with this name prefix
#define LOW_MEM_LIMIT 10000       // min free bytes
#define WDT_TIMEOUT   120         // max time since last reading (in seconds)
#define SHT_NAME      "THS_LOCAL" // sensor name for the onboard THS sensor

/*
  this is our global store of sensor values

  data = {
    THS_OFFICE: {
      temperature: 22.5, // degrees, c
      humidity: 80.0,    // percent, rel humi
      battery: 65.0      // percent, charge remaining
    },
    ...
  }
*/
std::map<string,std::map<string,float>> data;

int lastUpdate = 0; // uptime at which we last recorded data

int uptime() { // uptime in seconds
  return int(millis() / 1000);
}

bool lowMemory() { // are we running out of memory?
  return heap_caps_get_free_size(MALLOC_CAP_8BIT) < LOW_MEM_LIMIT;
}

bool bleStuck() { // is the bluetooth scanner stuck again?
  return (uptime() - lastUpdate) > WDT_TIMEOUT;
}

bool disconnected() { // did we lose wifi?
  return WiFi.status() != WL_CONNECTED;
}

bool led(void) { // reads LED state

  return digitalRead(LED_BUILTIN) == HIGH;
}

bool led(bool state) { // writes LED state
  digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
  return led();
}

// records a sensor value in memory
void record(string device, string attr, float value) {
  // gtfo if the value is the same
  if (data[device][attr] == value) return;

  // feed the watchdog timer
  lastUpdate = uptime();

  // write the value
  data[device][attr] = value;
}

// callbacks for when BLE finds a devince
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName() && advertisedDevice.haveServiceData() && !advertisedDevice.getName().find(BLE_FILTER)) {
      try {
        string sensorName = advertisedDevice.getName();
        string strServiceData = advertisedDevice.getServiceData(0);

        logf("\n\nAdvertised Device: %s\n", sensorName.c_str());

        uint8_t cServiceData[100];
        char charServiceData[100];
        strServiceData.copy((char *)cServiceData, strServiceData.length(), 0);
        for (int i=0;i<strServiceData.length();i++) {
          sprintf(&charServiceData[i*2], "%02x", cServiceData[i]);
        }

        stringstream payload;
        payload << "fe95" << charServiceData;
        logf("Payload: %s\n", payload.str().c_str());
        logf("Payload length: %d\n", strServiceData.length());

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
            logf("ATC Temperature: %s c\n", String((float)temperature/10,1).c_str());
            record(sensorName, "temperature", (float)temperature/10);

            sprintf(charValue, "%02X", cServiceData[8]);
            humidity = strtol(charValue, 0, 16);
            logf("ATC Humidity: %s %%\n", String((float)humidity,1).c_str());
            record(sensorName, "humidity", (float)humidity);

            sprintf(charValue, "%02X", cServiceData[9]);
            battery = strtol(charValue, 0, 16);
            logf("ATC Battery: %s %%\n", String((float)battery,0).c_str());
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
                logf2("HUMIDITY_EVENT: %s, %lu\n", charValue, humidity);
                record(sensorName, "humidity", (float)humidity/10);

                break;

              // battery data - this seems to be every other Xiaomi advertisement
              case 0x0A:
                sprintf(charValue, "%02X", cServiceData[14]);
                battery = strtol(charValue, 0, 16);
                logf("Battery: %s %%\n", String((float)battery,0).c_str());
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

// scan for BLE sensors
void bleScan() {
  log("\nScanning for BLE devices...");
  BLEDevice::getScan()->stop(); // stop any in-progreess scans (not sure if needed)
  BLEDevice::getScan()->start(SCAN_TIME); // scan for SCAN_TIME seconds
  log("done\n");
}

// read local SHT sensor
void readSHT() {
  log("\nReading local SHT sensor...");
  if (!sht.readSample()) return;
  logf("SHT Temperature: %s c\n", String(sht.getTemperature(),1).c_str());
  record(SHT_NAME,"temperature",sht.getTemperature());
  logf("SHT Humidity: %s %%\n", String(sht.getHumidity(),1).c_str());
  record(SHT_NAME,"humidity",sht.getHumidity());
}

// Main sensor read loop - read all the things
void readLoop(void * pvParameters) {
  logf("BLE Loop running on core %d\n", xPortGetCoreID());

  while (1) {
    if (bleStuck()) ESP.restart();
    if (lowMemory()) ESP.restart();
    // scan for BLE devices
    bleScan();
    // read local sensor
    readSHT();
  }
}

// Prometheus device metrics
string deviceMetrics() {
  stringstream ret;

  // report the free mempory on the ESP32 device
  char mem[256];
  snprintf(mem, sizeof mem, "%zu", heap_caps_get_free_size(MALLOC_CAP_8BIT));
  ret << "# HELP free_memory Free memory in the ESP32.\n# TYPE free_memory gauge\n";
  ret << "free_memory " << (string)mem << '\n';

  // report uptime (time since last reboot) in s
  ret << "# HELP uptime Uptime in seconds.\n# TYPE uptime counter\n";
  ret << "uptime " << uptime() << '\n';

  #ifdef DEBUG
  // report lag (time since last reboot) in s
  ret << "# HELP lag Time since last reading in seconds.\n# TYPE lag gauge\n";
  ret << "lag " << uptime() - lastUpdate << '\n';
  #endif

  return ret.str();
}

// Prometheus sensor metrics
string sensorMetrics() {
  stringstream ret;

  // not sure if these are needed by prometheus ¯\_(ツ)_/¯
  ret << "# HELP temperature Temperature of the sensor in degrees Celcius.\n# TYPE temperature gauge\n";
  ret << "# HELP humidity Relative humidity of the sensor as a percentage.\n# TYPE humidity gauge\n";
  ret << "# HELP battery Battery state of charge as a percentage.\n# TYPE battery gauge\n";

  std::map<string, std::map<string, float>>::iterator itOuter;
  std::map<string, float>::iterator itInner;

  for (itOuter=data.begin(); itOuter!=data.end(); ++itOuter) {
    for (itInner=itOuter->second.begin(); itInner!=itOuter->second.end(); ++itInner) {
      ret << itInner->first << "{sensor=\"" << itOuter->first << "\"} " << itInner->second << '\n';
    }
  }

  return ret.str();
}

TaskHandle_t ReadTask;

// init webserver on port 80
WebServer webserver(80);

void setup(void) {
  // set the internal LED as an output - not sure why?
  pinMode(LED_BUILTIN, OUTPUT);
  // turn the LED off (it defaults to on)
  led(false);

  // init i2c
  Wire.begin();

  // init logging interface over serial port, if we're debugging
  #ifdef DEBUG
  Serial.begin(115200);
  delay(1000); // let serial console "settle"
  #endif

  if (sht.init()) {
      log("SHT init(): success\n");
      sht.setAccuracy(SHTSensor::SHT_ACCURACY_MEDIUM); // only supported by SHT3x
  } else {
      log("SHT init(): failed - no SHT sensor installed?\n");
  }

  // init the BLE device
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan(); // find or create new scan
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setActiveScan(true); //active scan uses more power, but get results faster
  scan->setInterval(0xA0);
  scan->setWindow(0x99);

  // init the wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(NETWORK_SSID, NETWORK_PASS);

  // Wait for the wifi connection
  log("\n"); // a vertical space
  while (disconnected()) {
    // print 1 dot every half second while we're trying to connect
    delay(500);
    log(".");
  }
  logf2("\nConnected to %s\nIP address: %s\n", NETWORK_SSID, WiFi.localIP().toString().c_str());

  // this advertises the device locally at "sauron.local" (or whatever you set as your HOSTNAME)
  mdns_init();
  mdns_hostname_set(HOSTNAME); // set hostname
  mdns_instance_name_set(HOSTNAME); // set default instance - I have no idea what this does
  logf("MDNS responder started at http://%s.local\n", HOSTNAME);

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

    message << deviceMetrics();
    message << sensorMetrics();

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
  log("HTTP server started\n");

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
