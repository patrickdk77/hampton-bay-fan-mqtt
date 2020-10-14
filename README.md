# Hampton Bay/Fanimation/DawnSun Fan MQTT

## Features
Added support for Hampton Bay A25-TX028 rf remotes made by dawnsun
Added support for fanimation 6speed fan remote controls
All 3 can operate at the same time
Modified rc-switch to handle the hamptonbay 24bit codes and the fanimation 12bit codes
Added support to restore current fan state from mqtt retain storage on startup
Fixed inverted bits for fanimation
Added sleep delay

## Overview
ESP8266 project enabling MQTT control for a Hampton Bay fan with a wireless receiver. Wireless communication is performed with a CC1101 wireless transceiver operating at 303 MHz.

This will also monitor for Hampton Bay RF signals so the state will stay in sync even if the original remote is used to control the fan.

Fan control is not limited to a single dip switch setting, so up to 16 fans can be controlled with one ESP8266.

## Dependencies
This project uses the following libraries that are available through the Arduino IDE
* [SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) by LSatan
* [rc-switch](https://github.com/sui77/rc-switch) by sui77
* [PubSubClient](https://pubsubclient.knolleary.net/) by Nick O'Leary

## Hardware
* ESP8266 development board (Tested with a NodeMCU v2 and a D1 Mini)
* CC1101 wireless transceiver
  * Wiring info can be found in the [SmartRC-CC1101-Driver-Lib readme](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib#wiring)

## Setup
### Configuration
Change the `WIFI_*` and `MQTT_*` definitions in the sketch to match your network settings before uploading to the ESP.
### MQTT
By default, the state/command topics will be
* Fan on/off (payload `ON` or `OFF`)
  * `stat/hamptonbay/<fan_id>/fan`
  * `cmnd/hamptonbay/<fan_id>/fan`
* Fan speed (payload `low`, `medium`, or `high`; the fanimation 6speed fans can also use I,II,III,IV,V,VI to set any of the six speeds, but will only report 3 speeds back via mqtt as homeassistant only supports 3 speeds)
  * `stat/hamptonbay/<fan_id>/speed`
  * `cmnd/hamptonbay/<fan_id>/speed`
* Light on/off (payload `ON` or `OFF`)
  * `stat/hamptonbay/<fan_id>/light`
  * `cmnd/hamptonbay/<fan_id>/light`

`fan_id` is a 4-digit binary number determined by the dip switch settings on the transmitter/receiver where up = 1 and down = 0. For example, the dip setting:

|1|2|3|4|
|-|-|-|-|
|↑|↓|↓|↓|

...corresponds to a fan ID of `1000`

### Home Assistant
To use this in Home Assistant as an MQTT Fan and MQTT Light, I'm using this config
```yaml
fan:
- platform: mqtt
  name: "Bedroom Fan"
  availability_topic: "tele/rf-fans/LWT"
  payload_available: "Online"
  payload_not_available: "Offline"
  state_topic: "stat/hamptonbay/1000/fan"
  command_topic: "cmnd/hamptonbay/1000/fan"
  speed_state_topic: "stat/hamptonbay/1000/speed"
  speed_command_topic: "cmnd/hamptonbay/1000/speed"
  speeds:
    - low
    - medium
    - high

light:
- platform: mqtt
  name: "Bedroom Fan Light"
  availability_topic: "tele/rf-fans/LWT"
  payload_available: "Online"
  payload_not_available: "Offline"
  state_topic: "stat/hamptonbay/1000/light"
  command_topic: "cmnd/hamptonbay/1000/light"
```
