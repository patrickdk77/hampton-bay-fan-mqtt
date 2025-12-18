#include "rf-fans.h"


#define BASE_TOPIC HAMPTONBAY2_BASE_TOPIC

#define CMND_BASE_TOPIC CMND_TOPIC BASE_TOPIC
#define STAT_BASE_TOPIC STAT_TOPIC BASE_TOPIC

#define SUBSCRIBE_TOPIC_CMND CMND_BASE_TOPIC "/#"

#define SUBSCRIBE_TOPIC_STAT_SETUP STAT_BASE_TOPIC "/#"

#ifndef HAMPTONBAY2_TX_FREQ
  #define TX_FREQ 304.015 // a25-tx028 Hampton
#else
  #define TX_FREQ HAMPTONBAY2_TX_FREQ
#endif

// RC-switch settings
#define RF_PROTOCOL 14
#define RF_REPEATS  8 
                                    
// Keep track of states for all dip settings
static fan fans[16];

static int long lastvalue;
static unsigned long lasttime;

static void postStateUpdate(int id) {
  sprintf(outTopic, "%s/%s/power", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].powerState ? "ON":"OFF", true);
  sprintf(outTopic, "%s/%s/fan", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].fanState ? "ON":"OFF", true);
  sprintf(outTopic, "%s/%s/speed", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fanStateTable[fans[id].fanSpeed], true);
  sprintf(outTopic, "%s/%s/light", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].lightState ? "ON":"OFF", true);

  sprintf(outTopic, "%s/%s/percent", STAT_BASE_TOPIC, idStrings[id]);
  *outPercent='\0';
  if(fans[id].fanState) {
    switch(fans[id].fanSpeed) {
      case FAN_HI:
        sprintf(outPercent,"%d",FAN_PCT_HI);
        break;
      case FAN_MED:
        sprintf(outPercent,"%d",FAN_PCT_MED);
        break;
      case FAN_LOW:
        sprintf(outPercent,"%d",FAN_PCT_LOW);
        break;
    }
  } else
    sprintf(outPercent,"%d",FAN_PCT_OFF);
  client.publish(outTopic, outPercent, true);
}

static void transmitState(int fanId, int code) {
  mySwitch.disableReceive();         // Receiver off
  ELECHOUSE_cc1101.setMHZ(TX_FREQ);
  ELECHOUSE_cc1101.SetTx();           // set Transmit on
  mySwitch.enableTransmit(TX_PIN);   // Transmit on
  mySwitch.setRepeatTransmit(RF_REPEATS); // transmission repetitions.
  mySwitch.setProtocol(RF_PROTOCOL);        // send Received Protocol

  // Build out RF code
  //   Code follows the 24 bit pattern
  //   111111000110aaaa011cccdd
  //   Where a is the inversed/reversed dip setting, 
  //     ccc is the command (111 Power, 101 Fan, 100 Light, 011 Dim/Temp)
  //     dd is the data value
  int rfCode = 0xfc6000 | ((~fanId) &0x0f) << 8 | (code&0xff);
  
  mySwitch.send(rfCode, 24);      // send 24 bit code
  mySwitch.disableTransmit();   // set Transmit off
  ELECHOUSE_cc1101.setMHZ(RX_FREQ);
  ELECHOUSE_cc1101.SetRx();      // set Receive on
  mySwitch.enableReceive(RX_PIN);   // Receiver on
  Serial.print("Sent command hamptonbay2: ");
  Serial.print(fanId);
  Serial.print(" ");
  for(int b=24; b>0; b--) {
    Serial.print(bitRead(rfCode,b-1));
  }
  Serial.println("");
  postStateUpdate(fanId);
}

void hamptonbay2MQTT(char* topic, char* payloadChar, unsigned int length) {
  if(strncmp(topic, CMND_BASE_TOPIC, sizeof(CMND_BASE_TOPIC)-1) == 0) {
  
    // Get ID after the base topic + a slash
    char id[5];
    int percent;
    memcpy(id, &topic[sizeof(CMND_BASE_TOPIC)], 4);
    id[4] = '\0';
    if(strspn(id, idchars)) {
      uint8_t idint = strtol(id, (char**) NULL, 2);
      char *attr;
      // Split by slash after ID in topic to get attribute and action
    
      attr = strtok(topic+sizeof(CMND_BASE_TOPIC)-1 + 5, "/");
      
      if(attr==NULL) return;

      if(strcmp(attr,"percent") ==0) {
        percent=atoi(payloadChar);
        if(percent > FAN_PCT_OVER) {
          fans[idint].fanState = true;
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          if(percent > (FAN_PCT_MED + FAN_PCT_OVER)) {
            fans[idint].fanSpeed=FAN_HI;
            transmitState(idint,0x74);
          } else if(percent > (FAN_PCT_LOW + FAN_PCT_OVER)) {
            fans[idint].fanSpeed=FAN_MED;
            transmitState(idint,0x75);
          } else {
            fans[idint].fanSpeed=FAN_LOW;
            transmitState(idint,0x76);
          }
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0x77);
        }
      } else if(strcmp(attr,"fan") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].fanState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].fanState = true;
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          switch(fans[idint].fanSpeed) {
            case FAN_HI:
              transmitState(idint,0x74);
              break;
            case FAN_MED:
              transmitState(idint,0x75);
              break;
            case FAN_LOW:
              transmitState(idint,0x76);
              break;
          }
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0x77);
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"+") ==0) {
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_LOW:
              fans[idint].fanSpeed=FAN_MED;
              transmitState(idint,0x75);
              break;
            case FAN_MED:
              fans[idint].fanSpeed=FAN_HI;
              transmitState(idint,0x74);
              break;
            case FAN_HI:
              fans[idint].fanSpeed=FAN_HI;
              transmitState(idint,0x74);
              break;
            default:
              if(fans[idint].fanSpeed>FAN_HI)
                fans[idint].fanSpeed--;
              break;
          }
        } else if(strcmp(payloadChar,"-") ==0) {
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_HI:
              fans[idint].fanSpeed=FAN_MED;
              transmitState(idint,0x75);
              break;
            case FAN_MED:
              fans[idint].fanSpeed=FAN_LOW;
              transmitState(idint,0x76);
              break;
            case FAN_LOW:
              fans[idint].fanSpeed=FAN_LOW;
              transmitState(idint,0x76);
              break;
            default:
              if(fans[idint].fanSpeed<FAN_LOW)
                fans[idint].fanSpeed++;
              break;
          }
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
        } else if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_HI;
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          transmitState(idint,0x74);
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_MED;
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          transmitState(idint,0x75);
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_LOW;
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          transmitState(idint,0x76);
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0x77);
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].lightState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].lightState = true;
          if(fans[idint].powerState==false) {
            fans[idint].powerState=true;
            transmitState(idint,0x7e); // Turn on
          }
          transmitState(idint,0x72);
        } else {
          fans[idint].lightState = false;
          transmitState(idint,0x71);
        }
      } else if(strcmp(attr,"power") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].powerState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].powerState = true;
          transmitState(idint,0x7e);
        } else {
          fans[idint].powerState = false;
          transmitState(idint,0x7d);
        }
      }
    } else {
      // Invalid ID
      return;
    }
  }
  if(strncmp(topic, STAT_BASE_TOPIC, sizeof(STAT_BASE_TOPIC)-1) == 0) {
  
    // Get ID after the base topic + a slash
    char id[5];
    memcpy(id, &topic[sizeof(STAT_BASE_TOPIC)], 4);
    id[4] = '\0';
    if(strspn(id, idchars)) {
      uint8_t idint = strtol(id, (char**) NULL, 2);
      char *attr;
      // Split by slash after ID in topic to get attribute and action
    
      attr = strtok(topic+sizeof(STAT_BASE_TOPIC)-1 + 5, "/");

      if(strcmp(attr,"fan") ==0) {
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].fanState = true;
        } else {
          fans[idint].fanState = false;
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanSpeed = FAN_HI;
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanSpeed = FAN_MED;
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanSpeed = FAN_LOW;
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].lightState = true;
        } else {
          fans[idint].lightState = false;
        }
      } else if(strcmp(attr,"power") ==0) {
        if(strcmp(payloadChar,"on") == 0) {
          fans[idint].powerState = true;
        } else {
          fans[idint].powerState = false;
        }
      }
    } else {
      // Invalid ID
      return;
    }
  }
}

void hamptonbay2RF(int long value, int prot, int bits) {
    if( (prot >10 && prot<15)  && bits == 24 && ((value&0xfff000)==0xfc6000)) {
      unsigned long t=millis();
      if(value == lastvalue) {
        if(t - lasttime < NO_RF_REPEAT_TIME)
          return;
        lasttime=t;
      }
      lastvalue=value;
      lasttime=t;
      int dipId = (~value >> 8)&0x0f;
      // Got a correct id in the correct protocol
      if(dipId < 16) {
        // Blank out id in message to get light state
        switch(value&0xff) {
          case 0x7e: // PowerOn
            fans[dipId].powerState = true;
            break;
          case 0x7d: // PowerOff
            fans[dipId].powerState = false;
            break;
          case 0x72: // LightOn
            fans[dipId].lightState = true;
            break;
          case 0x71: // LightOff
            fans[dipId].lightState = false;
            break;
          case 0x6e: // Light Dim
            break;
          case 0x6d: // Light Tempature (2k, 3k, 5k)
            break;
          case 0x74: // Fan High
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_HI;
            break;
          case 0x75: // Fan Med
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_MED;
            break;
          case 0x76: // Fan Low
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_LOW;
            break;
          case 0x77: // Fan Off
            fans[dipId].fanState = false;
            break;
        }
        postStateUpdate(dipId);
      }
    }
}

void hamptonbay2MQTTSub(boolean setup) {
  client.subscribe(SUBSCRIBE_TOPIC_CMND);
  if(setup) client.subscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}

void hamptonbay2Setup() {
  lasttime=0;
  lastvalue=0;
  // initialize fan struct 
  for(int i=0; i<16; i++) {
    fans[i].powerState = false;
    fans[i].lightState = false;
    fans[i].fanState = false;  
    fans[i].fanSpeed = FAN_LOW;
  }
}
   
void hamptonbay2SetupEnd() {
  client.unsubscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}
