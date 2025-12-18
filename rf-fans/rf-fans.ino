#include "rf-fans.h"

// fanimation is 6 speed, map the speeds into 3 for homeassistant to not freak out, but retain all locally
const char *fanStateTable[] = {
  "off", "high", "high", "medium", "medium", "low", "low"
};

const char *fanFullStateTable[] = {
  "off", "VI", "V", "IV", "III", "II", "I"
};

RCSwitch mySwitch = RCSwitch();
WiFiClient espClient;
PubSubClient client(espClient);

WiFiServer TelnetServer(8266);

#ifdef STATUS_LED
EasyLed led(STATUS_LED_PIN, EasyLed::ActiveLevel::High);
#endif

#ifdef DHT_SENSOR
DHT_Unified dht(DHT_SENSOR_PIN, DHT_TYPE);
unsigned long lastTempRead = 0;
char dhtTopic[100];
char outDhtValue[100];
#endif

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

char outTopic[100];
char outPercent[100];

#ifdef HEAP_DELAY_SECONDS
static long heapSize=0;
static unsigned long heapDelay=0;
#endif

static unsigned long reconnectReboot=0;
static unsigned long reconnectDelay=0;
static unsigned long setupDelay=0;
static boolean readMQTT=true;
static boolean ignorerf=false;

#ifndef DOORBELL_COOLDOWN
#define DOORBELL_COOLDOWN 2000 // how many milliseconds before retrigger is allowed
#endif

#ifdef DOORBELL1
static char doorbell1=1;
static unsigned long doorbell1_milli=DOORBELL_COOLDOWN;
#endif
#ifdef DOORBELL2
static char doorbell2=1;
static unsigned long doorbell2_milli=DOORBELL_COOLDOWN;
#endif
#ifdef DOORBELL3
static char doorbell3=1;
static unsigned long doorbell3_milli=DOORBELL_COOLDOWN;
#endif

#ifdef DOORBELL_INT
volatile static byte doorbell=0;

#ifdef DOORBELL1
ICACHE_RAM_ATTR void doorbell1_int() {
  doorbell|=0x01;
}
#endif

#ifdef DOORBELL2
ICACHE_RAM_ATTR void doorbell2_int() {
  doorbell|=0x02;
}
#endif

#ifdef DOORBELL3
ICACHE_RAM_ATTR void doorbell3_int() {
  doorbell|=0x04;
}
#endif
#endif

void setup_wifi() {
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

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    #ifdef STATUS_LED
    led.flash(2);
    #endif
  }

  WiFi.setAutoReconnect(true);
  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  char payloadChar[length + 1];
//  sprintf(payloadChar, "%s", payload);
//  payloadChar[length] = '\0';

  // Convert payload to lowercase
  for(unsigned i=0; payload[i] && i<length; i++) {
    payloadChar[i] = tolower(payload[i]);
  }
  payloadChar[length]='\0';

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.print(payloadChar);
  Serial.println();
  #ifdef STATUS_LED
  led.flash(3);
  #endif  
  
  if(strncmp(topic, CMND_TOPIC MQTT_CLIENT_NAME "/restart", sizeof(CMND_TOPIC MQTT_CLIENT_NAME "/restart")-1) == 0) {
    ESP.restart();
    return;
  } else 
  if(strncmp(topic, CMND_TOPIC MQTT_CLIENT_NAME "/reset", sizeof(CMND_TOPIC MQTT_CLIENT_NAME "/reset")-1) == 0) {
    ESP.reset();
    return;
  } else
  if(strncmp(topic, CMND_TOPIC MQTT_CLIENT_NAME "/ignorerf", sizeof(CMND_TOPIC MQTT_CLIENT_NAME "/ignorerf")-1) == 0) {
    if(strcmp(payloadChar,"on") == 0)
      ignorerf=true;
    else
      ignorerf=false;
    client.publish(STAT_TOPIC MQTT_CLIENT_NAME "/ignorerf", ignorerf ? "ON":"OFF", true);
    return;
  } else
  if(strncmp(topic, CMND_TOPIC MQTT_CLIENT_NAME "/txrcswitch", sizeof(CMND_TOPIC MQTT_CLIENT_NAME "/txrcswitch")-1) == 0) {
    unsigned long rfCode=0;
    unsigned proto=11;
    unsigned bits=0;
    unsigned repeats=7;
    float freq=303.000;
    unsigned n=0;
    char *s;

    // freq,repeats,proto,bits
    for(s=strtok(payloadChar,(const char*)','); s; s=strtok(NULL,(const char*)',')) {
      if(n==0)
        freq=atof(s);
      if(n==1)
        repeats=atoi(s);
      if(n==2)
        proto=atoi(s);
      if(n==3) {
        rfCode=0;
        for(bits=0;s[bits]!=0 && bits<32;bits++) {
          rfCode<<=1L;
          if(s[bits]=='1')
            rfCode|=0x1;
        }
      }
      n++;
    }

    if(freq>300.000 && freq<464.000 ) {
      mySwitch.disableReceive();         // Receiver off
      ELECHOUSE_cc1101.setMHZ(freq);
      ELECHOUSE_cc1101.SetTx();           // set Transmit on
      mySwitch.enableTransmit(TX_PIN);   // Transmit on
      mySwitch.setRepeatTransmit(repeats); // transmission repetitions.
      mySwitch.setProtocol(proto);        // send Received Protocol

      mySwitch.send(rfCode, bits);      // send 12 bit code
      mySwitch.disableTransmit();   // set Transmit off
      ELECHOUSE_cc1101.setMHZ(RX_FREQ);
      ELECHOUSE_cc1101.SetRx();      // set Receive on
      mySwitch.enableReceive(RX_PIN);   // Receiver on
      Serial.print("Sent command raw: ");
      Serial.print(freq);
      Serial.print(" - ");
      Serial.print(proto);
      Serial.print(" - ");
      Serial.print(rfCode);
      Serial.print(" - ");
      Serial.print(bits);
      Serial.print("  :  ");
      for(int b=bits; b>0; b--) {
        Serial.print(bitRead(rfCode,b-1));
      }
      Serial.println();
    }
    return;
  }
 
#ifdef HAMPTONBAY
  hamptonbayMQTT(topic,payloadChar,length);
#endif
#ifdef HAMPTONBAY2
  hamptonbay2MQTT(topic,payloadChar,length);
#endif
#ifdef HAMPTONBAY3
  hamptonbay3MQTT(topic,payloadChar,length);
#endif
#ifdef HAMPTONBAY4
  hamptonbay4MQTT(topic,payloadChar,length);
#endif
#ifdef FANIMATION
  fanimationMQTT(topic,payloadChar,length);
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
      client.subscribe(CMND_TOPIC MQTT_CLIENT_NAME "/#");
#ifdef HAMPTONBAY
      hamptonbayMQTTSub(readMQTT);
#endif
#ifdef HAMPTONBAY2
      hamptonbay2MQTTSub(readMQTT);
#endif
#ifdef HAMPTONBAY3
      hamptonbay3MQTTSub(readMQTT);
#endif
#ifdef HAMPTONBAY4
      hamptonbay4MQTTSub(readMQTT);
#endif
#ifdef FANIMATION
      fanimationMQTTSub(readMQTT);
#endif
      setupDelay=millis()+5000;
      readMQTT=false;
      reconnectDelay=0;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 18 seconds");
      #ifdef STATUS_LED
      led.flash(4);
      #endif  

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

    #ifdef STATUS_LED
    led.off();
    #endif   

#ifdef HAMPTONBAY
  hamptonbaySetup();
#endif
#ifdef HAMPTONBAY2
  hamptonbay2Setup();
#endif
#ifdef HAMPTONBAY3
  hamptonbay3Setup();
#endif
#ifdef HAMPTONBAY4
  hamptonbay4Setup();
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

  #ifdef DHT_SENSOR
  dht.begin();
  #endif

  ArduinoOTA.setHostname((const char *)HOSTNAME);
  if(sizeof(OTA_PASS)>0)
    ArduinoOTA.setPassword((const char *)OTA_PASS);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    #ifdef STATUS_LED
    led.flash(6);
    #endif      
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
    Serial.println("Rebooting...");
    #ifdef STATUS_LED
    led.flash(6);
    #endif      
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef STATUS_LED
    led.flash(7);
    #endif      
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

#ifdef DOORBELL1
  pinMode(DOORBELL1,INPUT_PULLUP);
#ifdef DOORBELL_INT
  attachInterrupt(digitalPinToInterrupt(DOORBELL1), doorbell1_int, FALLING);
#endif
#endif
#ifdef DOORBELL2
  pinMode(DOORBELL2,INPUT_PULLUP);
#ifdef DOORBELL_INT
  attachInterrupt(digitalPinToInterrupt(DOORBELL2), doorbell2_int, FALLING);
#endif
#endif
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
#ifdef HAMPTONBAY3
    hamptonbay3SetupEnd();
#endif
#ifdef HAMPTONBAY4
    hamptonbay4SetupEnd();
#endif
#ifdef FANIMATION
    fanimationSetupEnd();
#endif
    setupDelay=0;
  }

  // Handle received transmissions
  if (mySwitch.available()) {
    unsigned long rfCode =  mySwitch.getReceivedValue();        // save received Value
    unsigned proto = mySwitch.getReceivedProtocol();     // save received Protocol
    unsigned bits = mySwitch.getReceivedBitlength();     // save received Bitlength

    sprintf(outTopic, "%s%s/rfreceived", STAT_TOPIC, MQTT_CLIENT_NAME);
    sprintf(outPercent,"{proto:\"%u\", rfCode:\"%x%x\", bitlength:\"%u\"}",proto, (unsigned int)(rfCode>>16), (unsigned int)(rfCode&&0xffff), bits);
    client.publish(outTopic, outPercent, false);
    #ifdef STATUS_LED
    led.flash(1);
    #endif  

    Serial.print("Received RF Proto: ");
    Serial.print(proto);
    Serial.print(" - Code: ");
    Serial.print(rfCode);
    Serial.print(" - Bits: ");
    Serial.print(bits);
    Serial.print("  :  ");
    for(unsigned b=bits; b>0; b--) {
      Serial.print(bitRead(rfCode,b-1));
    }
    Serial.println();
    
#ifdef HAMPTONBAY
    if(!ignorerf) hamptonbayRF(rfCode,proto,bits);
#endif
#ifdef HAMPTONBAY2
    if(!ignorerf) hamptonbay2RF(rfCode,proto,bits);
#endif
#ifdef HAMPTONBAY3
    if(!ignorerf) hamptonbay3RF(rfCode,proto,bits);
#endif
#ifdef HAMPTONBAY4
    if(!ignorerf) hamptonbay4RF(rfCode,proto,bits);
#endif
#ifdef FANIMATION
    if(!ignorerf) fanimationRF(rfCode,proto,bits);
#endif

    mySwitch.resetAvailable();
  }
  //  else {
  //   Serial.println("RCSwitch not available!");
  // }
  
  if (!client.connected()) {
#ifdef MQTT_REBOOT_SECONDS
    if(reconnectReboot==0)
      reconnectReboot=t+(MQTT_REBOOT_SECONDS*1000);
#endif
    if(reconnectDelay<t) {
      reconnectMQTT();
      if(reconnectDelay>0)
        reconnectDelay=millis()+18000;
      else
        reconnectDelay=millis()+6000;
#ifdef MQTT_REBOOT_SECONDS
      if(reconnectReboot<t) { // disconnected from mqtt too long, reboot
        ESP.restart();
      }
#endif
    }
  } else {
    if(reconnectReboot>0)
      reconnectReboot=0;
    //Serial.println("Starting MQTT loop...");      
    client.loop();
    //Serial.println("Exiting MQTT loop...");      
  }

  ArduinoOTA.handle();
  
#ifdef HEAP_DELAY_SECONDS
  if(heapDelay<t) {
    long hs = ESP.getFreeHeap();
    heapDelay=t+(HEAP_DELAY_SECONDS*1000);
    if(heapSize != hs) {
      heapSize=hs;
      sprintf(outTopic, "%s%s/heapsize", STAT_TOPIC, MQTT_CLIENT_NAME);
      ltoa(heapSize,outPercent,10);
      client.publish(outTopic, outPercent, false);
    }
  }
#endif

#ifdef DOORBELL1
  if(doorbell1==0) {
#ifdef DOORBELL_INT
    if((doorbell&0x01)==0x01) {
      doorbell&=(~0x01);
#else
    if(digitalRead(DOORBELL1)==LOW) {
#endif
      if(doorbell1_milli==0) {
        doorbell1_milli=t;
        sprintf(outTopic, "%sdoorbell/1/state", STAT_TOPIC);
        client.publish(outTopic, "ON", true);
      }
      doorbell1=1;
    }
  } else {
#ifdef DOORBELL_INT
    if((doorbell&0x01)==0) {
#else
    if(digitalRead(DOORBELL1)==HIGH) {
#endif
      if((t-doorbell1_milli)>DOORBELL_COOLDOWN) {
        doorbell1_milli=0;
        doorbell1=0;
        sprintf(outTopic, "%sdoorbell/1/state", STAT_TOPIC);
        client.publish(outTopic, "OFF", true);
      }
    } else {
      doorbell1_milli=t;
#ifdef DOORBELL_INT
      doorbell&=(~0x01);
#endif
    }
  }
#endif

#ifdef DOORBELL2
  if(doorbell2==0) {
#ifdef DOORBELL_INT
    if((doorbell&0x02)==0x02) {
      doorbell&=(~0x02);
#else
    if(digitalRead(DOORBELL2)==LOW) {
#endif
      if(doorbell2_milli==0) {
        doorbell2_milli=t;
        sprintf(outTopic, "%sdoorbell/2/state", STAT_TOPIC);
        client.publish(outTopic, "ON", true);
      }
      doorbell2=1;
    }
  } else {
#ifdef DOORBELL_INT
    if((doorbell&0x02)==0x00) {
#else
    if(digitalRead(DOORBELL2)==HIGH) {
#endif
      if((t-doorbell2_milli)>DOORBELL_COOLDOWN) {
        doorbell2_milli=0;
        doorbell2=0;
        sprintf(outTopic, "%sdoorbell/2/state", STAT_TOPIC);
        client.publish(outTopic, "OFF", true);
      }
    } else {
      doorbell2_milli=t;
#ifdef DOORBELL_INT
      doorbell&=(~0x02);
#endif
    }
  }
#endif

#ifdef DOORBELL3
  if(doorbell3==0) {
#ifdef DOORBELL_INT
    if((doorbell&0x04)==0x04) {
      doorbell&=(~0x04);
#else
    if(digitalRead(DOORBELL3)==LOW) {
#endif
      if(doorbell3_milli==0) {
        doorbell3_milli=t;
        sprintf(outTopic, "%sdoorbell/3/state", STAT_TOPIC);
        client.publish(outTopic, "ON", true);
      }
      doorbell3=1;
    }
  } else {
#ifdef DOORBELL_INT
    if((doorbell&0x04)==0x00) {
#else
    if(digitalRead(DOORBELL3)==HIGH) {
#endif
      if((t-doorbell3_milli)>DOORBELL_COOLDOWN) {
        doorbell3_milli=0;
        doorbell3=0;
        sprintf(outTopic, "%sdoorbell/3/state", STAT_TOPIC);
        client.publish(outTopic, "OFF", true);
      }
    } else {
      doorbell3_milli=t;
#ifdef DOORBELL_INT
      doorbell&=(~0x04);
#endif
    }
  }
#endif

#ifdef DHT_SENSOR
  //DHT_TEMP_SECONDS
  if (lastTempRead == 0 || (millis()-lastTempRead > DHT_TEMP_SECONDS*1000))
  {
    lastTempRead = millis();
    // Get temperature event and print its value.
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    if (isnan(event.temperature)) {
      Serial.println(F("Error reading temperature!"));
      lastTempRead = 0;
    }
    else {
      Serial.print(F("Temperature: "));
      Serial.print(event.temperature);
      Serial.println(F("°C"));
      sprintf(dhtTopic, "%s%s/temperature", STAT_TOPIC, MQTT_CLIENT_NAME);
      dtostrf(event.temperature,0,2,outDhtValue);
      client.publish(dhtTopic, outDhtValue, false);      
    }
    // Get humidity event and print its value.
    dht.humidity().getEvent(&event);
    if (isnan(event.relative_humidity)) {
      Serial.println(F("Error reading humidity!"));
      lastTempRead = 0;
    }
    else {
      Serial.print(F("Humidity: "));
      Serial.print(event.relative_humidity);
      Serial.println(F("%"));
      sprintf(dhtTopic, "%s%s/humidity", STAT_TOPIC, MQTT_CLIENT_NAME);
      dtostrf(event.relative_humidity,0,2,outDhtValue);
      client.publish(dhtTopic, outDhtValue, false);      
    }
  }

#endif          

  unsigned long looptime = millis()-t;
  if(looptime<SLEEP_DELAY)
    SleepDelay(SLEEP_DELAY-looptime);
  
}
