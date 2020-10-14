#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "RCSwitch.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "config.h"
#ifdef HAMPTONBAY
  #include "hamptonbay.h"
#endif
#ifdef HAMPTONBAY2
  #include "hamptonbay2.h"
#endif
#ifdef FANIMATION
  #include "fanimation.h"
#endif

#define STATUS_TOPIC "tele/" MQTT_CLIENT_NAME "/LWT"

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
#define RX_FREQ 303.870 // Middle ground, transmitters are very wide branded and picks up all of them

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

#define NO_RF_REPEAT_TIME 300

#define SLEEP_DELAY 50

struct fan
{
  bool powerState;
  bool fade;
  bool directionState;
  bool lightState;
  bool fanState;
  uint8_t fanSpeed;
};

extern RCSwitch mySwitch;
extern WiFiClient espClient;
extern PubSubClient client;

extern const char *fanStateTable[];
extern const byte dipToRfIds[16];
extern const char *idStrings[16];
extern char idchars[];
