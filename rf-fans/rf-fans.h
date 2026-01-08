#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "RCSwitch.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#include "config.h"
#ifdef HAMPTONBAY
  #include "hamptonbay.h"
#endif
#ifdef HAMPTONBAY3
  #include "hamptonbay3.h"
#endif
#ifdef HAMPTONBAY4
  #include "hamptonbay4.h"
#endif
#ifdef HAMPTONBAY2
  #include "hamptonbay2.h"
#endif
#ifdef FANIMATION
  #include "fanimation.h"
#endif

#ifdef STATUS_LED
  #include "EasyLed.h"
#endif

#ifdef DHT_SENSOR
  #include <Adafruit_Sensor.h>
  #include <DHT.h>
  #include <DHT_U.h>
#endif

#define TELE_TOPIC "tele/"
#define CMND_TOPIC "cmnd/"
#define STAT_TOPIC "stat/"

#define STATUS_TOPIC TELE_TOPIC MQTT_CLIENT_NAME "/LWT"

// Set receive and transmit pin numbers (GDO0 and GDO2)
#ifdef ESP32 // for esp32! Receiver on GPIO pin 4. Transmit on GPIO pin 2.
  #define RX_PIN 4 
  #define TX_PIN 2
#elif ESP8266  // for esp8266! Receiver on pin 4 = D2. Transmit on pin 5 = D1.
  #define RX_PIN 4
  #define TX_PIN 5
#else // for Arduino! Receiver on interrupt 0 => that is pin #2. Transmit on pin 6.
  #define RX_PIN 0
  #define TX_PIN 6
#endif 

// Set CC1101 frequency
#ifndef RX_REEQ
  #define RX_FREQ 303.870 // Middle ground, transmitters are very wide branded and picks up all of them
#endif

// Define fan states
#define FAN_HI  1
#define FAN_MED 3
#define FAN_LOW 5

#define FAN_VI  1
#define FAN_V   2
#define FAN_IV  3
#define FAN_III 4
#define FAN_II  5
#define FAN_I   6

#define FAN_PCT_HI 100
#define FAN_PCT_MED 67
#define FAN_PCT_LOW 33
#define FAN_PCT_OFF  0
#define FAN_PCT_VI 100
#define FAN_PCT_V   83
#define FAN_PCT_IV  66
#define FAN_PCT_III 50
#define FAN_PCT_II  33
#define FAN_PCT_I   16

#define FAN_PCT_OVER 5 // if asking for 35% it will round down to 33%

#define NO_RF_REPEAT_TIME 300

#define SLEEP_DELAY 50

#define HEAP_DELAY_SECONDS 60
#define MQTT_REBOOT_SECONDS 2000

struct fan
{
  bool powerState;
  bool fade;
  bool directionState;
  bool lightState;
  bool light2State;
  bool fanState;
  uint8_t fanSpeed;
  uint8_t lightBrightness;
};

extern RCSwitch mySwitch;
extern WiFiClient espClient;
extern PubSubClient client;

extern const char *fanStateTable[];
extern const char *fanFullStateTable[];
extern const byte dipToRfIds[16];
extern const char *idStrings[16];
extern char idchars[];
extern char outTopic[100];
extern char outPercent[100];

#ifdef DOORBELL1
#ifndef DOORBELL_INT
#define DOORBELL_INT 1
#endif
#endif

#ifdef DOORBELL2
#ifndef DOORBELL_INT
#define DOORBELL_INT 1
#endif
#endif

#ifdef DOORBELL3
#ifndef DOORBELL_INT
#define DOORBELL_INT 1
#endif
#endif
