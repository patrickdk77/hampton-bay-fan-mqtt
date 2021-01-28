#include <Arduino.h>
#include "rf-fans.h"

// fanimation is 6 speed, map the speeds into 3 for homeassistant to not freak out, but retain all locally
const char *fanStateTable[] = {
    "off", "high", "high", "medium", "medium", "low", "low"};

RCSwitch mySwitch = RCSwitch();
WiFiClient espClient;
PubSubClient client(espClient);

WiFiServer TelnetServer(8266);

// The ID returned from the RF code appears to be inversed and reversed for hamptonbay
//   e.g. a dip setting of on off off off (1000) yields 1110
// Convert between IDs from MQTT from dip switch settings and what is used in the RF codes
const byte dipToRfIds[16] = {
        [0] = 0, [1] = 8, [2] = 4, [3] = 12,
        [4] = 2, [5] = 10, [6] = 6, [7] = 14,
        [8] = 1, [9] = 9, [10] = 5, [11] = 13,
        [12] = 3, [13] = 11, [14] = 7, [15] = 15,
};

const char *idStrings[16] = {
        [0] = "0000", [1] = "0001", [2] = "0010", [3] = "0011",
        [4] = "0100", [5] = "0101", [6] = "0110", [7] = "0111",
        [8] = "1000", [9] = "1001", [10] = "1010", [11] = "1011",
        [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
};

char idchars[] = "01";

static unsigned long reconnectDelay = 0;
static unsigned long setupDelay = 0;
static boolean readMQTT = true;

#ifndef DOORBELL_COOLDOWN
#define DOORBELL_COOLDOWN 2000 // how many milliseconds before retrigger is allowed
#endif DOORBELL_COOLDOWN

#ifdef DOORBELL1
static char doorbell1 = 1;
static unsigned long doorbell1_milli = DOORBELL_COOLDOWN;
static unsigned long doorbell1_debug_milli = 0;
#endif
#ifdef DOORBELL2
static char doorbell2 = 1;
static unsigned long doorbell2_milli = DOORBELL_COOLDOWN;
#endif
#ifdef DOORBELL3
static char doorbell3 = 1;
static unsigned long doorbell3_milli = DOORBELL_COOLDOWN;
#endif

#ifdef DOORBELL_INT
volatile static byte doorbell = 0;

#ifdef DOORBELL1
ICACHE_RAM_ATTR void doorbell1_int()
{
  doorbell |= 0x01;
}
#endif

#ifdef DOORBELL2
ICACHE_RAM_ATTR void doorbell2_int()
{
  doorbell |= 0x02;
}
#endif

#ifdef DOORBELL3
ICACHE_RAM_ATTR void doorbell3_int()
{
  doorbell |= 0x04;
}
#endif
#endif

void setup_wifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.persistent(false);
  WiFi.disconnect();
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  WiFi.setAutoReconnect(true);
  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

#ifdef HAMPTONBAY
  hamptonbayMQTT(topic, payload, length);
#endif
#ifdef HAMPTONBAY2
  hamptonbay2MQTT(topic, payload, length);
#endif
#ifdef FANIMATION
  fanimationMQTT(topic, payload, length);
#endif
}

void reconnectMQTT()
{
  Serial.print("Attempting MQTT connection...");
  // Attempt to connect
  if (client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASS, STATUS_TOPIC, 0, true, "Offline"))
  {
    Serial.println("connected");
    // Once connected, publish an announcement...
    client.publish(STATUS_TOPIC, "Online", true);
// ... and resubscribe
#ifdef HAMPTONBAY
    hamptonbayMQTTSub(readMQTT);
#endif
#ifdef HAMPTONBAY2
    hamptonbay2MQTTSub(readMQTT);
#endif
#ifdef FANIMATION
    fanimationMQTTSub(readMQTT);
#endif
    setupDelay = millis() + 5000;
    readMQTT = false;
  }
  else
  {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
  }
}

void SleepDelay(uint32_t mseconds)
{
  if (mseconds)
  {
    for (; mseconds > 0; mseconds--)
    {
      delay(1);
      if (Serial.available())
      {
        break;
      } // We need to service serial buffer ASAP as otherwise we get uart buffer overrun
      if (mySwitch.available())
      {
        break;
      }
    }
  }
  else
  {
    delay(0);
  }
}

void setup()
{
  TelnetServer.begin();
  Serial.begin(115200);

#ifdef HAMPTONBAY
  hamptonbaySetup();
#endif
#ifdef HAMPTONBAY2
  hamptonbay2Setup();
#endif
#ifdef FANIMATION
  fanimationSetup();
#endif

  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(RX_FREQ);
  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  mySwitch.disableReceive();

  setup_wifi();
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);

  mySwitch.enableReceive(RX_PIN);

  ArduinoOTA.setHostname((const char *)HOSTNAME);
  if (sizeof(OTA_PASS) > 0)
    ArduinoOTA.setPassword((const char *)OTA_PASS);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
    Serial.println("Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();

#ifdef DOORBELL1
  pinMode(DOORBELL1, INPUT_PULLUP);
#ifdef DOORBELL_INT
  attachInterrupt(digitalPinToInterrupt(DOORBELL1), doorbell1_int, FALLING);
#endif
#endif
#ifdef DOORBELL2
  pinMode(DOORBELL2, INPUT_PULLUP);
#ifdef DOORBELL_INT
  attachInterrupt(digitalPinToInterrupt(DOORBELL2), doorbell2_int, FALLING);
#endif
#endif
}

void loop()
{
  unsigned long t = millis();
  if (setupDelay > 0 && setupDelay < t)
  {
#ifdef HAMPTONBAY
    hamptonbaySetupEnd();
#endif
#ifdef HAMPTONBAY2
    hamptonbay2SetupEnd();
#endif
#ifdef FANIMATION
    fanimationSetupEnd();
#endif
    setupDelay = 0;
  }

  // Handle received transmissions
  if (mySwitch.available())
  {
    int long value = mySwitch.getReceivedValue(); // save received Value
    int prot = mySwitch.getReceivedProtocol();    // save received Protocol
    int bits = mySwitch.getReceivedBitlength();   // save received Bitlength

    Serial.print(prot);
    Serial.print(" - ");
    Serial.print(value);
    Serial.print(" - ");
    Serial.print(bits);
    Serial.print("  :  ");
    for (int b = bits; b > 0; b--)
    {
      Serial.print(bitRead(value, b - 1));
    }
    Serial.println();

#ifdef HAMPTONBAY
    hamptonbayRF(value, prot, bits);
#endif
#ifdef HAMPTONBAY2
    hamptonbay2RF(value, prot, bits);
#endif
#ifdef FANIMATION
    fanimationRF(value, prot, bits);
#endif

    mySwitch.resetAvailable();
  }

  if (!client.connected())
  {
    if (reconnectDelay < t)
    {
      reconnectMQTT();
      reconnectDelay = millis() + 5000;
    }
  }
  else
  {
    client.loop();
  }

  ArduinoOTA.handle();

#ifdef DOORBELL1
  if (doorbell1 == 0)
  {
#ifdef DOORBELL_INT
    if ((doorbell & 0x01) == 0x01)
    {
      doorbell &= (~0x01);
#else
    if (digitalRead(DOORBELL1) == LOW)
    {
#endif
      if (doorbell1_milli == 0)
      {
        char outTopic[100];
        doorbell1_milli = t;
        sprintf(outTopic, "%sdoorbell/1/state", STAT_TOPIC);
        client.publish(outTopic, "ON", true);
      }
      doorbell1 = 1;
    }
  }
  else
  {
#ifdef DOORBELL_INT
    if ((doorbell & 0x01) == 0)
    {
#else
    if (digitalRead(DOORBELL1) == HIGH)
    {
#endif
      if ((t - doorbell1_milli) > DOORBELL_COOLDOWN)
      {
        char outTopic[100];
        doorbell1_milli = 0;
        doorbell1 = 0;
        sprintf(outTopic, "%sdoorbell/1/state", STAT_TOPIC);
        client.publish(outTopic, "OFF", true);
      }
    }
    else
    {
      doorbell1_milli = t;
#ifdef DOORBELL_INT
      doorbell &= (~0x01);
#endif
    }
  }
#endif

#ifdef DOORBELL2
  if (doorbell2 == 0)
  {
#ifdef DOORBELL_INT
    if ((doorbell & 0x02) == 0x02)
    {
      doorbell &= (~0x02);
#else
    if (digitalRead(DOORBELL2) == LOW)
    {
#endif
      if (doorbell2_milli == 0)
      {
        char outTopic[100];
        doorbell2_milli = t;
        sprintf(outTopic, "%sdoorbell/2/state", STAT_TOPIC);
        client.publish(outTopic, "ON", true);
      }
      doorbell2 = 1;
    }
  }
  else
  {
#ifdef DOORBELL_INT
    if ((doorbell & 0x02) == 0x00)
    {
#else
    if (digitalRead(DOORBELL2) == HIGH)
    {
#endif
      if ((t - doorbell2_milli) > DOORBELL_COOLDOWN)
      {
        char outTopic[100];
        doorbell2_milli = 0;
        doorbell2 = 0;
        sprintf(outTopic, "%sdoorbell/2/state", STAT_TOPIC);
        client.publish(outTopic, "OFF", true);
      }
    }
    else
    {
      doorbell2_milli = t;
#ifdef DOORBELL_INT
      doorbell &= (~0x02);
#endif
    }
  }
#endif

#ifdef DOORBELL3
  if (doorbell3 == 0)
  {
#ifdef DOORBELL_INT
    if ((doorbell & 0x04) == 0x04)
    {
      doorbell &= (~0x04);
#else
    if (digitalRead(DOORBELL3) == LOW)
    {
#endif
      if (doorbell3_milli == 0)
      {
        char outTopic[100];
        doorbell3_milli = t;
        sprintf(outTopic, "%sdoorbell/3/state", STAT_TOPIC);
        client.publish(outTopic, "ON", true);
      }
      doorbell3 = 1;
    }
  }
  else
  {
#ifdef DOORBELL_INT
    if ((doorbell & 0x04) == 0x00)
    {
#else
    if (digitalRead(DOORBELL3) == HIGH)
    {
#endif
      if ((t - doorbell3_milli) > DOORBELL_COOLDOWN)
      {
        char outTopic[100];
        doorbell3_milli = 0;
        doorbell3 = 0;
        sprintf(outTopic, "%sdoorbell/3/state", STAT_TOPIC);
        client.publish(outTopic, "OFF", true);
      }
    }
    else
    {
      doorbell3_milli = t;
#ifdef DOORBELL_INT
      doorbell &= (~0x04);
#endif
    }
  }
#endif

  unsigned long looptime = millis() - t;
  if (looptime < SLEEP_DELAY)
    SleepDelay(SLEEP_DELAY - looptime);
}
