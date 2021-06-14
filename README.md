<p align="center"><img src="https://user-images.githubusercontent.com/3444/112892697-a9583300-90a7-11eb-8f26-14f10f1d072e.gif" alt="👁️"/></p>

# 👁️ Sauron

Firmware for an **esp32 d1 mini** which scans for Xiaomi BLE Temperature and humidity sensors, feeding that data to downstream services.

Code is set up as a [PlatformIO](https://platformio.org) project - can be build & deployed to an esp32 directly from within VS Code.

> :warning: **I'm extremely new to C++ and Arduino**: I have virtually no idea what I'm doing. Would love some `*ptrs` though ;-)


## Backstory

I wanted to get familiar with programming esp32 and esp8266 devices, and also thought it would be nice to have a log of temp & humidity data at different points around my house.

I ordered some esp32 d1 minis from aliexpress (for ~$4/pc) and also 4 Xiaomi Mijia BLE sensors for about the same. They each took a few weeks to arrive, but I was in no hurry.


## The sensors

![The sensors](https://user-images.githubusercontent.com/3444/112892474-59796c00-90a7-11eb-8b7e-2baac52011b7.png)

[These little temperature & humidity sensors](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20210329142009&SearchText=xiaomi+mijia+temperature+humidity+2) are not only an incredible deal at about $4/pc, but they've already been hacked to pieces already by the DIY community. Custom firmware can be easily uploaded, giving you full control over the display & data format of these devices.

I ordered 4 of them for about $15 USD total and have placed them in various corners of my townhouse.

The way they work is actually kind of genius. Each one will advertise itself as a **Bluetooth Low Energy** device, but all the data these things provide is _encoded in the advertisement itself_! That means you can use them completely passively, without needing to pair them first.


## The SOC

<img src="https://user-images.githubusercontent.com/3444/112905602-add91780-90b8-11eb-9289-cd1a81ac15ce.png" alt="wemos d1 mini" align="right"/>

I first saw the esp8266 on an [everlanders video](https://www.youtube.com/watch?v=aS3BiYaEfiw) about automation on a self-built overlanding rig, and it seemed like a super valuable skill for either a future house 🏡   and/or sailboat ⛵  build.

The boards themselves are an extremely good value. For [somewhere in the $2-4 range](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20210329155255&SearchText=wemos+d1+mini+esp32) you basically get an Arduino with wifi, bluetooth (esp32 only) and a host of other features I'm not really that familiar with. There's really good shield 🛡️  support (think sensors, relays, displays, etc) and a huge community for supporting libraries and project ideas. They're also extremely easy to power, as most can be powered directly by any USB-A plug or adapter.


## "Hacking" the firmware

The first step is to replace the stock firmware with one of the custom options. I went with [this one by pvvx](https://github.com/pvvx/ATC_MiThermometer#flashing-or-updating-the-firmware-ota) as it gives you more control over the device.

The whole process takes place via a modern web browser & the web bluetooth api. You'll want a recent version of Chrome for this.

The advertising format I prefer is the ATC format, as it has good enough resolution for the sensor attributes and is fairly straightforward.

If you use the above firmware, make sure you select the either the **ATC advertising format** or the **Mi like** formats, as they're the only two I've bothered implementing so far.

## The advertising payload

All the data is encoded in the advertising payload. I find it's easiest to explain by example: Let's use the payload string `fe95a4c13835cd4900d6425a0bc5ee`

The payload can be parsed into pre-defined chunks, then each chunk decoded into a usable value:
```
2 bytes... id? not sure what this is
6 bytes mac address
2 bytes Temperature C (10x the actual value, for precision)
1 byte Humidity (%)
1 byte Battery Level (%)
2 bytes Battery (mV)
1 byte frame packet counter
```

Here's the example payload decoded:

```
payload:
fe95a4c13835cd4900d6425a0bc5ee

split into chunks:
fe95 a4c13835cd49 00d6 42 5a 0bc5 ee
```

Let's follow each chunk in the following table:

| step | id? | mac address | temperature | humidity | battery level | battery (mV) | message counter |
| --- | --- | ------------- | ------------- | ------------- | ------------- | ------------- | ------------- |
| **raw input** | fe95 | a4c13835cd49  | 00d6  | 42 | 5a | 0bc5 | ee |
| **Hex -> Int** |  |  | 0214 | 66 | 90 | 3013 | 238 |
| **t / 10** |  |  | 21.4  |  |  |  |  |  |  |
| **value** | fe95 | `a4:c1:38:35:cd:49`  | 21.4ºC | 66% | 90% | 3.013V | 238 |


## Publishing the data

### MQTT 

The data is publshed to an MQTT service on my Mac Mini.

This Mini also runs [homebridge](https://homebridge.io) which has [a plugin](https://www.npmjs.com/package/homebridge-mqttthing) that makes the data available in my Home app.

Only real-time data is reported here; no logs are kept.

![image](https://user-images.githubusercontent.com/3444/121961717-c52db580-cd35-11eb-9bf2-5d7d9ecb1630.png)


### Prometheus / Grafana

The data is also made available via a `/metrics` endpoint in a format that [Prometheus](https://prometheus.io/) (a time series metrics database) can read. Prometheus is a common source for a [Grafana](https://grafana.com) dashboard, which is sort of a swiss army bulldozer for displaying data.

![image](https://user-images.githubusercontent.com/3444/121961502-81d34700-cd35-11eb-9349-494aa7c63eea.png)

Prometheus and Grafana were easy to install on my Mac Mini via Homebrew:

```bash
brew install prometheus
brew services start prometheus

brew install grafana
brew services start grafana
```

A small caveat for prometheus is that it can't resolve local DNS, so we need to find the IP manually:

```bash
# find the IP of your local ESP32
ping sauron.local

# output:
# PING sauron.local (10.0.1.150): 56 data bytes
# ...
```

Then add a config to prometheus and restart:

```bash
# edit your prometheus config to include the IP of your ESP32
"${EDITOR:-nano}" $HOMEBREW_PREFIX/etc/prometheus.yml

# make changes (example below)

# restart prometheus
brew services restart prometheus
```

My `prometheus.yml` looks like this:
```yml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: "prometheus"
    static_configs:
    - targets: ["localhost:9090"]
  - job_name: "sensors"
    static_configs:
    - targets: ["10.0.1.150"]
```

Then you can log into grafana (default user/pass is `admin`/`asmin`) and start adding widgets!

<details>
 <summary>If you're interested, my local Grafana dashboard config:</summary>

  ```json
{
  "annotations": {
    "list": [
      {
        "builtIn": 1,
        "datasource": "-- Grafana --",
        "enable": true,
        "hide": true,
        "iconColor": "rgba(0, 211, 255, 1)",
        "name": "Annotations & Alerts",
        "type": "dashboard"
      }
    ]
  },
  "description": "",
  "editable": true,
  "gnetId": null,
  "graphTooltip": 0,
  "id": 4,
  "links": [],
  "panels": [
    {
      "datasource": null,
      "description": "",
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              {
                "color": "yellow",
                "value": null
              },
              {
                "color": "red",
                "value": 80
              }
            ]
          },
          "unit": "none"
        },
        "overrides": [
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "temp"
            },
            "properties": [
              {
                "id": "unit",
                "value": "celsius"
              }
            ]
          },
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "hum"
            },
            "properties": [
              {
                "id": "unit",
                "value": "percent"
              }
            ]
          }
        ]
      },
      "gridPos": {
        "h": 9,
        "w": 6,
        "x": 0,
        "y": 0
      },
      "id": 5,
      "options": {
        "colorMode": "value",
        "graphMode": "area",
        "justifyMode": "auto",
        "orientation": "horizontal",
        "reduceOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "fields": "",
          "values": false
        },
        "text": {},
        "textMode": "value"
      },
      "pluginVersion": "7.5.6",
      "targets": [
        {
          "exemplar": true,
          "expr": "temperature{sensor=\"THS_KITCHN\"}",
          "instant": false,
          "interval": "",
          "legendFormat": "temperature",
          "refId": "temp"
        },
        {
          "exemplar": true,
          "expr": "humidity{sensor=\"THS_KITCHN\"}",
          "hide": false,
          "interval": "",
          "legendFormat": "humidity",
          "refId": "hum"
        }
      ],
      "timeFrom": null,
      "timeShift": null,
      "title": "Kitchen",
      "type": "stat"
    },
    {
      "datasource": null,
      "description": "",
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              {
                "color": "green",
                "value": null
              },
              {
                "color": "red",
                "value": 80
              }
            ]
          },
          "unit": "short"
        },
        "overrides": [
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "temp"
            },
            "properties": [
              {
                "id": "unit",
                "value": "celsius"
              }
            ]
          },
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "hum"
            },
            "properties": [
              {
                "id": "unit",
                "value": "percent"
              }
            ]
          }
        ]
      },
      "gridPos": {
        "h": 9,
        "w": 6,
        "x": 6,
        "y": 0
      },
      "id": 6,
      "options": {
        "colorMode": "value",
        "graphMode": "area",
        "justifyMode": "auto",
        "orientation": "horizontal",
        "reduceOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "fields": "",
          "values": false
        },
        "text": {},
        "textMode": "value"
      },
      "pluginVersion": "7.5.6",
      "targets": [
        {
          "exemplar": true,
          "expr": "temperature{sensor=\"THS_LVROOM\"}",
          "instant": false,
          "interval": "",
          "legendFormat": "temperature",
          "refId": "temp"
        },
        {
          "exemplar": true,
          "expr": "humidity{sensor=\"THS_LVROOM\"}",
          "hide": false,
          "interval": "",
          "legendFormat": "humidity",
          "refId": "hum"
        }
      ],
      "timeFrom": null,
      "timeShift": null,
      "title": "Living room",
      "type": "stat"
    },
    {
      "datasource": null,
      "description": "",
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              {
                "color": "purple",
                "value": null
              },
              {
                "color": "red",
                "value": 80
              }
            ]
          },
          "unit": "none"
        },
        "overrides": [
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "temp"
            },
            "properties": [
              {
                "id": "unit",
                "value": "celsius"
              }
            ]
          },
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "hum"
            },
            "properties": [
              {
                "id": "unit",
                "value": "percent"
              }
            ]
          }
        ]
      },
      "gridPos": {
        "h": 9,
        "w": 6,
        "x": 12,
        "y": 0
      },
      "id": 7,
      "options": {
        "colorMode": "value",
        "graphMode": "area",
        "justifyMode": "auto",
        "orientation": "horizontal",
        "reduceOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "fields": "",
          "values": false
        },
        "text": {},
        "textMode": "value"
      },
      "pluginVersion": "7.5.6",
      "targets": [
        {
          "exemplar": true,
          "expr": "temperature{sensor=\"THS_BDROOM\"}",
          "instant": false,
          "interval": "",
          "legendFormat": "temperature",
          "refId": "temp"
        },
        {
          "exemplar": true,
          "expr": "humidity{sensor=\"THS_BDROOM\"}",
          "hide": false,
          "interval": "",
          "legendFormat": "humidity",
          "refId": "hum"
        }
      ],
      "timeFrom": null,
      "timeShift": null,
      "title": "Bedroom",
      "type": "stat"
    },
    {
      "datasource": null,
      "description": "",
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              {
                "color": "blue",
                "value": null
              },
              {
                "color": "red",
                "value": 80
              }
            ]
          },
          "unit": "short"
        },
        "overrides": [
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "temp"
            },
            "properties": [
              {
                "id": "unit",
                "value": "celsius"
              }
            ]
          },
          {
            "matcher": {
              "id": "byFrameRefID",
              "options": "hum"
            },
            "properties": [
              {
                "id": "unit",
                "value": "percent"
              }
            ]
          }
        ]
      },
      "gridPos": {
        "h": 9,
        "w": 6,
        "x": 18,
        "y": 0
      },
      "id": 2,
      "options": {
        "colorMode": "value",
        "graphMode": "area",
        "justifyMode": "auto",
        "orientation": "horizontal",
        "reduceOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "fields": "",
          "values": false
        },
        "text": {},
        "textMode": "value"
      },
      "pluginVersion": "7.5.6",
      "targets": [
        {
          "exemplar": true,
          "expr": "temperature{sensor=\"THS_OFFICE\"}",
          "instant": false,
          "interval": "",
          "legendFormat": "temperature",
          "refId": "temp"
        },
        {
          "exemplar": true,
          "expr": "humidity{sensor=\"THS_OFFICE\"}",
          "hide": false,
          "interval": "",
          "legendFormat": "humidity",
          "refId": "hum"
        }
      ],
      "timeFrom": null,
      "timeShift": null,
      "title": "Office",
      "type": "stat"
    },
    {
      "aliasColors": {},
      "bars": false,
      "dashLength": 10,
      "dashes": false,
      "datasource": null,
      "fieldConfig": {
        "defaults": {},
        "overrides": []
      },
      "fill": 1,
      "fillGradient": 0,
      "gridPos": {
        "h": 8,
        "w": 9,
        "x": 0,
        "y": 9
      },
      "hiddenSeries": false,
      "id": 9,
      "legend": {
        "avg": false,
        "current": false,
        "max": false,
        "min": false,
        "show": true,
        "total": false,
        "values": false
      },
      "lines": true,
      "linewidth": 1,
      "nullPointMode": "null",
      "options": {
        "alertThreshold": true
      },
      "percentage": false,
      "pluginVersion": "7.5.6",
      "pointradius": 2,
      "points": false,
      "renderer": "flot",
      "seriesOverrides": [],
      "spaceLength": 10,
      "stack": false,
      "steppedLine": false,
      "targets": [
        {
          "exemplar": true,
          "expr": "temperature{instance=\"10.0.1.150:80\"}",
          "interval": "",
          "legendFormat": "{{sensor}}",
          "refId": "A"
        }
      ],
      "thresholds": [],
      "timeFrom": null,
      "timeRegions": [],
      "timeShift": null,
      "title": "Temperature",
      "tooltip": {
        "shared": true,
        "sort": 0,
        "value_type": "individual"
      },
      "type": "graph",
      "xaxis": {
        "buckets": null,
        "mode": "time",
        "name": null,
        "show": true,
        "values": []
      },
      "yaxes": [
        {
          "$$hashKey": "object:373",
          "decimals": null,
          "format": "short",
          "label": "Celcius",
          "logBase": 1,
          "max": "30",
          "min": "15",
          "show": true
        },
        {
          "$$hashKey": "object:374",
          "format": "short",
          "label": null,
          "logBase": 1,
          "max": null,
          "min": null,
          "show": true
        }
      ],
      "yaxis": {
        "align": false,
        "alignLevel": null
      }
    },
    {
      "aliasColors": {},
      "bars": false,
      "dashLength": 10,
      "dashes": false,
      "datasource": null,
      "fieldConfig": {
        "defaults": {},
        "overrides": []
      },
      "fill": 1,
      "fillGradient": 0,
      "gridPos": {
        "h": 8,
        "w": 8,
        "x": 9,
        "y": 9
      },
      "hiddenSeries": false,
      "id": 10,
      "legend": {
        "avg": false,
        "current": false,
        "max": false,
        "min": false,
        "show": true,
        "total": false,
        "values": false
      },
      "lines": true,
      "linewidth": 1,
      "nullPointMode": "null",
      "options": {
        "alertThreshold": true
      },
      "percentage": false,
      "pluginVersion": "7.5.6",
      "pointradius": 2,
      "points": false,
      "renderer": "flot",
      "seriesOverrides": [],
      "spaceLength": 10,
      "stack": false,
      "steppedLine": false,
      "targets": [
        {
          "exemplar": true,
          "expr": "humidity{instance=\"10.0.1.150:80\"}",
          "interval": "",
          "legendFormat": "{{sensor}}",
          "refId": "A"
        }
      ],
      "thresholds": [],
      "timeFrom": null,
      "timeRegions": [],
      "timeShift": null,
      "title": "Humidity",
      "tooltip": {
        "shared": true,
        "sort": 0,
        "value_type": "individual"
      },
      "type": "graph",
      "xaxis": {
        "buckets": null,
        "mode": "time",
        "name": null,
        "show": true,
        "values": []
      },
      "yaxes": [
        {
          "$$hashKey": "object:373",
          "format": "short",
          "label": null,
          "logBase": 1,
          "max": null,
          "min": null,
          "show": true
        },
        {
          "$$hashKey": "object:374",
          "format": "short",
          "label": null,
          "logBase": 1,
          "max": null,
          "min": null,
          "show": true
        }
      ],
      "yaxis": {
        "align": false,
        "alignLevel": null
      }
    },
    {
      "datasource": null,
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "mappings": [],
          "thresholds": {
            "mode": "percentage",
            "steps": [
              {
                "color": "red",
                "value": null
              },
              {
                "color": "orange",
                "value": 10
              },
              {
                "color": "green",
                "value": 20
              }
            ]
          },
          "unit": "percent"
        },
        "overrides": []
      },
      "gridPos": {
        "h": 8,
        "w": 7,
        "x": 17,
        "y": 9
      },
      "id": 12,
      "options": {
        "reduceOptions": {
          "calcs": [
            "lastNotNull"
          ],
          "fields": "",
          "values": false
        },
        "showThresholdLabels": false,
        "showThresholdMarkers": true,
        "text": {}
      },
      "pluginVersion": "7.5.6",
      "targets": [
        {
          "exemplar": true,
          "expr": "battery{instance=\"10.0.1.150:80\"}",
          "interval": "",
          "legendFormat": "{{sensor}}",
          "refId": "A"
        }
      ],
      "timeFrom": null,
      "timeShift": null,
      "title": "Battery",
      "type": "gauge"
    },
    {
      "datasource": null,
      "description": "",
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "custom": {
            "align": null,
            "displayMode": "basic",
            "filterable": false
          },
          "mappings": [],
          "max": 30,
          "min": 15,
          "thresholds": {
            "mode": "absolute",
            "steps": [
              {
                "color": "green",
                "value": null
              },
              {
                "color": "red",
                "value": 25
              }
            ]
          },
          "unit": "celsius"
        },
        "overrides": [
          {
            "matcher": {
              "id": "byName",
              "options": "Value #Temperature (lastNotNull)"
            },
            "properties": [
              {
                "id": "unit",
                "value": "celsius"
              },
              {
                "id": "displayName",
                "value": "Temperature"
              }
            ]
          },
          {
            "matcher": {
              "id": "byName",
              "options": "Value #Humidity (lastNotNull)"
            },
            "properties": [
              {
                "id": "unit",
                "value": "percent"
              },
              {
                "id": "max",
                "value": 100
              },
              {
                "id": "min",
                "value": 0
              },
              {
                "id": "thresholds",
                "value": {
                  "mode": "absolute",
                  "steps": [
                    {
                      "color": "green",
                      "value": null
                    },
                    {
                      "color": "red",
                      "value": 80
                    }
                  ]
                }
              },
              {
                "id": "displayName",
                "value": "Humidity"
              }
            ]
          },
          {
            "matcher": {
              "id": "byName",
              "options": "sensor"
            },
            "properties": [
              {
                "id": "custom.displayMode",
                "value": "auto"
              }
            ]
          },
          {
            "matcher": {
              "id": "byName",
              "options": "Time (lastNotNull)"
            },
            "properties": [
              {
                "id": "custom.displayMode",
                "value": "auto"
              },
              {
                "id": "unit",
                "value": "dateTimeFromNow"
              },
              {
                "id": "displayName",
                "value": "last updated"
              }
            ]
          }
        ]
      },
      "gridPos": {
        "h": 6,
        "w": 24,
        "x": 0,
        "y": 17
      },
      "id": 14,
      "options": {
        "frameIndex": 0,
        "showHeader": true
      },
      "pluginVersion": "7.5.6",
      "targets": [
        {
          "exemplar": true,
          "expr": "temperature",
          "format": "table",
          "instant": false,
          "interval": "",
          "legendFormat": "temperature",
          "refId": "Temperature"
        },
        {
          "exemplar": true,
          "expr": "humidity",
          "format": "table",
          "hide": false,
          "instant": false,
          "interval": "",
          "legendFormat": "humidity",
          "refId": "Humidity"
        }
      ],
      "title": "Summary",
      "transformations": [
        {
          "id": "groupBy",
          "options": {
            "fields": {
              "Time": {
                "aggregations": [
                  "lastNotNull"
                ],
                "operation": "aggregate"
              },
              "Value #A": {
                "aggregations": [
                  "lastNotNull"
                ],
                "operation": "aggregate"
              },
              "Value #B": {
                "aggregations": [
                  "lastNotNull"
                ],
                "operation": "aggregate"
              },
              "Value #Humidity": {
                "aggregations": [
                  "lastNotNull"
                ],
                "operation": "aggregate"
              },
              "Value #Temperature": {
                "aggregations": [
                  "lastNotNull"
                ],
                "operation": "aggregate"
              },
              "__name__": {
                "aggregations": [],
                "operation": null
              },
              "sensor": {
                "aggregations": [],
                "operation": "groupby"
              }
            }
          }
        },
        {
          "id": "merge",
          "options": {}
        }
      ],
      "type": "table"
    }
  ],
  "refresh": "30s",
  "schemaVersion": 27,
  "style": "dark",
  "tags": [],
  "templating": {
    "list": []
  },
  "time": {
    "from": "now-24h",
    "to": "now"
  },
  "timepicker": {},
  "timezone": "",
  "title": "Massey",
  "uid": "QkZ9Px6Gz",
  "version": 15
}
```
 
</details>
