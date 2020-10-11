#include "rf-fans.h"

// fanimation is 6 speed, map the speeds into 3 for homeassistant to not freak out, but retain all locally
const char *fanStateTable[] = {
  "off", "high", "high", "medium", "medium", "low", "low"
};

RCSwitch mySwitch = RCSwitch();
WiFiClient espClient;
PubSubClient client(espClient);

// The ID returned from the RF code appears to be inversed and reversed
//   e.g. a dip setting of on off off off (1000) yields 1110
// Convert between IDs from MQTT from dip switch settings and what is used in the RF codes
const byte dipToRfIds[16] = {
    [ 0] = 15, [ 1] = 7, [ 2] = 11, [ 3] = 3,
    [ 4] = 13, [ 5] = 5, [ 6] =  9, [ 7] = 1,
    [ 8] = 14, [ 9] = 6, [10] = 10, [11] = 2,
    [12] = 12, [13] = 4, [14] =  8, [15] = 0,
};
const char *idStrings[16] = {
    [ 0] = "0000", [ 1] = "0001", [ 2] = "0010", [ 3] = "0011",
    [ 4] = "0100", [ 5] = "0101", [ 6] = "0110", [ 7] = "0111",
    [ 8] = "1000", [ 9] = "1001", [10] = "1010", [11] = "1011",
    [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
};
char idchars[] = "01";

static unsigned long reconnectDelay=0;
static unsigned long lastMQTT=0;
static unsigned long lastRF=0;

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
      hamptonbayMQTTSub();
#endif
#ifdef HAMPTONBAY2
      hamptonbay2MQTTSub();
#endif
#ifdef FANIMATION
      fanimationMQTTSub();
#endif
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
    }
}

void setup() {
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
  mySwitch.enableReceive(RX_PIN);

  setup_wifi();
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);
}

void loop() {
  unsigned long t = millis();
  if (!client.connected()) {
    if(reconnectDelay<t) {
      reconnectMQTT();
      reconnectDelay=millis()+5000;
    }
  } else if(lastMQTT<t) {
    client.loop();
    lastMQTT=t+55;
  }

  if(lastRF<t) {
    lastRF=t+50;
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
  }
}
