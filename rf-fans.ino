#include "rf-fans.h"

// fanimation is 6 speed, map the speeds into 3 for homeassistant to not freak out, but retain all locally
const char *fanStateTable[] = {
  "off", "high", "high", "medium", "medium", "low", "low"
};

RCSwitch mySwitch = RCSwitch();
WiFiClient espClient;
PubSubClient client(espClient);

WiFiServer TelnetServer(8266);

// The ID returned from the RF code appears to be inversed and reversed for hamptonbay
//   e.g. a dip setting of on off off off (1000) yields 1110
// Convert between IDs from MQTT from dip switch settings and what is used in the RF codes
const byte dipToRfIds[16] = {
    [ 0] = 0, [ 1] = 8, [ 2] = 4, [ 3] = 12,
    [ 4] = 2, [ 5] = 10, [ 6] =  6, [ 7] = 14,
    [ 8] = 1, [ 9] = 9, [10] = 5, [11] = 13,
    [12] = 3, [13] = 11, [14] =  7, [15] = 15,
};

const char *idStrings[16] = {
    [ 0] = "0000", [ 1] = "0001", [ 2] = "0010", [ 3] = "0011",
    [ 4] = "0100", [ 5] = "0101", [ 6] = "0110", [ 7] = "0111",
    [ 8] = "1000", [ 9] = "1001", [10] = "1010", [11] = "1011",
    [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
};

char idchars[] = "01";

static unsigned long reconnectDelay=0;
static unsigned long setupDelay=0;
static boolean readMQTT=true;

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setAutoReconnect(true);
  WiFi.setAutoConnect(true);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

#ifdef HAMPTONBAY
  hamptonbayMQTT(topic,payload,length);
#endif
#ifdef HAMPTONBAY2
  hamptonbay2MQTT(topic,payload,length);
#endif
#ifdef FANIMATION
  fanimationMQTT(topic,payload,length);
#endif
}

void reconnectMQTT() {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASS, STATUS_TOPIC, 0, true, "Offline")) {
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
      setupDelay=millis()+5000;
      readMQTT=false;
  } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
    }
}

void SleepDelay(uint32_t mseconds) {
  if (mseconds) {
    for (; mseconds>0; mseconds--) {
      delay(1);
      if (Serial.available()) { break; }  // We need to service serial buffer ASAP as otherwise we get uart buffer overrun
      if (mySwitch.available()) { break; }
    }
  } else {
    delay(0);
  }
}

void setup() {
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
  if(sizeof(OTA_PASS)>0)
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
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void loop() {
  unsigned long t = millis();
  if (setupDelay>0 && setupDelay<t) {
#ifdef HAMPTONBAY
    hamptonbaySetupEnd();
#endif
#ifdef HAMPTONBAY2
    hamptonbay2SetupEnd();
#endif
#ifdef FANIMATION
    fanimationSetupEnd();
#endif
    setupDelay=0;
  }

  // Handle received transmissions
  if (mySwitch.available()) {
    int long value =  mySwitch.getReceivedValue();        // save received Value
    int prot = mySwitch.getReceivedProtocol();     // save received Protocol
    int bits = mySwitch.getReceivedBitlength();     // save received Bitlength

    Serial.print(prot);
    Serial.print(" - ");
    Serial.print(value);
    Serial.print(" - ");
    Serial.print(bits);
    Serial.print("  :  ");
    for(int b=bits; b>0; b--) {
      Serial.print(bitRead(value,b-1));
    }
    Serial.println();
    
#ifdef HAMPTONBAY
    hamptonbayRF(value,prot,bits);
#endif
#ifdef HAMPTONBAY2
    hamptonbay2RF(value,prot,bits);
#endif
#ifdef FANIMATION
    fanimationRF(value,prot,bits);
#endif

    mySwitch.resetAvailable();
  }
  
  if (!client.connected()) {
    if(reconnectDelay<t) {
      reconnectMQTT();
      reconnectDelay=millis()+5000;
    }
  } else {
    client.loop();
  }

  ArduinoOTA.handle();
  
  unsigned long looptime = millis()-t;
  if(looptime<SLEEP_DELAY)
    SleepDelay(SLEEP_DELAY-looptime);
  
}
