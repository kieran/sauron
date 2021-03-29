<p align="center"><img src="https://user-images.githubusercontent.com/3444/112892697-a9583300-90a7-11eb-8f26-14f10f1d072e.gif" alt="👁️"/></p>

# 👁️ Sauron

Firmware for an **esp32 d1 mini** which scans for Xiaomi BLE Temperature and humidity sensors, feeding that data to an MQTT server.

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


## Next steps

I'm currently publishing this data to an MQTT service on my Mac Mini - the plan is to store the data long term. Not sure what I want to use for storage yet, but might take a stab at [clickhouse](https://clickhouse.tech) or [Prometheus](https://prometheus.io/), probably visualizing in [Grafana](https://grafana.com)

Would also love to make these sensors available in HomeKit, either directly or via [homebridge](https://homebridge.io)

I've also included a basic webserver, not sure how annoying it would be to store the data / visualization locally on the esp32. Something else to try!
