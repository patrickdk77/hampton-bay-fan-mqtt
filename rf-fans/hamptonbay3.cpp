#include "rf-fans.h"


#define BASE_TOPIC HAMPTONBAY3_BASE_TOPIC

#define CMND_BASE_TOPIC CMND_TOPIC BASE_TOPIC
#define STAT_BASE_TOPIC STAT_TOPIC BASE_TOPIC

#define SUBSCRIBE_TOPIC_CMND CMND_BASE_TOPIC "/#"

#define SUBSCRIBE_TOPIC_STAT_SETUP STAT_BASE_TOPIC "/#"

#ifndef HAMPTONBAY3_TX_FREQ
  #define TX_FREQ 304.95 // UC7078TR Hampton Bay made by Chia Wei Electric
#else
  #define TX_FREQ HAMPTONBAY3_TX_FREQ
#endif

// RC-switch settings
#define RF_PROTOCOL 13
#define RF_REPEATS  8 
                                    
// Keep track of states for all dip settings
static fan fans[16];

static int long lastvalue;
static unsigned long lasttime;

static void postStateUpdate(int id) {
  sprintf(outTopic, "%s/%s/direction", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].directionState ? "REVERSE":"FORWARD", true);
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
  //   Code follows the 12 bit pattern, built ontop of harberbreeze?
  //   lLOR??MHaaaa
  //   Where a is the inversed/reversed dip setting, 
  // 11111110  H	0xfe
  // 11111101  M	0xfd
  // 10111111  L	0xbf
  // 11011111  Off	0xdf
  // 11101111  Reverse	0xef
  // 01111111  Light	0x7f
  // Harber Breeze UC-7078TR

  int rfCode = ((~fanId) &0x0f) | ((code&0xff)<<4);
  
  mySwitch.send(rfCode, 12);      // send 24 bit code
  mySwitch.disableTransmit();   // set Transmit off
  ELECHOUSE_cc1101.setMHZ(RX_FREQ);
  ELECHOUSE_cc1101.SetRx();      // set Receive on
  mySwitch.enableReceive(RX_PIN);   // Receiver on
  Serial.print("Sent command hamptonbay3: ");
  Serial.print(fanId);
  Serial.print(" ");
  for(int b=24; b>0; b--) {
    Serial.print(bitRead(rfCode,b-1));
  }
  Serial.println("");
  postStateUpdate(fanId);
}

void hamptonbay3MQTT(char* topic, char* payloadChar, unsigned int length) {
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
      
      if(attr == NULL) return;

      if(strcmp(attr,"percent") ==0) {
        percent=atoi(payloadChar);
        if(percent > FAN_PCT_OVER) {
          fans[idint].fanState = true;
          if(percent > (FAN_PCT_MED + FAN_PCT_OVER)) {
            fans[idint].fanSpeed=FAN_HI;
            transmitState(idint,0xfe);
          } else if(percent > (FAN_PCT_LOW + FAN_PCT_OVER)) {
            fans[idint].fanSpeed=FAN_MED;
            transmitState(idint,0xfd);
          } else {
            fans[idint].fanSpeed=FAN_LOW;
            transmitState(idint,0xbf);
          }
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0xdf);
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
          switch(fans[idint].fanSpeed) {
            case FAN_HI:
              transmitState(idint,0xfe);
              break;
            case FAN_MED:
              transmitState(idint,0xfd);
              break;
            case FAN_LOW:
              transmitState(idint,0xbf);
              break;
          }
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0xdf);
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"+") ==0) {
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_LOW:
              fans[idint].fanSpeed=FAN_MED;
              transmitState(idint,0xfd);
              break;
            case FAN_MED:
              fans[idint].fanSpeed=FAN_HI;
              transmitState(idint,0xfe);
              break;
            case FAN_HI:
              fans[idint].fanSpeed=FAN_HI;
              transmitState(idint,0xfe);
              break;
            default:
              if(fans[idint].fanSpeed>FAN_HI)
                fans[idint].fanSpeed--;
              break;
          }
        } else if(strcmp(payloadChar,"-") ==0) {
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_HI:
              fans[idint].fanSpeed=FAN_MED;
              transmitState(idint,0xfd);
              break;
            case FAN_MED:
              fans[idint].fanSpeed=FAN_LOW;
              transmitState(idint,0xbf);
              break;
            case FAN_LOW:
              fans[idint].fanSpeed=FAN_LOW;
              transmitState(idint,0xbf);
              break;
            default:
              if(fans[idint].fanSpeed<FAN_LOW)
                fans[idint].fanSpeed++;
              break;
          }
        } else if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_HI;
          transmitState(idint,0xfe);
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_MED;
          transmitState(idint,0xfd);
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_LOW;
          transmitState(idint,0xbf);
        } else {
          fans[idint].fanState = false;
          transmitState(idint,0xdf);
        }
      } else if(strcmp(attr,"light") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].lightState)
            strcpy(payloadChar,"off");
          else
            strcpy(payloadChar,"on");
        }
        if(strcmp(payloadChar,"on") == 0 && !fans[idint].lightState) {
          fans[idint].lightState = true;
          transmitState(idint,0x7f);
        } else if (fans[idint].lightState) {
          fans[idint].lightState = false;
          transmitState(idint,0x7f);
        }
      } else if(strcmp(attr,"direction") ==0) {
        if(strcmp(payloadChar,"toggle") == 0) {
          if(fans[idint].directionState)
            strcpy(payloadChar,"forward");
          else
            strcpy(payloadChar,"reverse");
        }
        if(strcmp(payloadChar,"reverse") == 0 && !fans[idint].directionState) {
          fans[idint].directionState = true;
          transmitState(idint,0xef);
        } else if(fans[idint].directionState) {
          fans[idint].directionState = false;
          transmitState(idint,0xef);
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
      } else if(strcmp(attr,"direction") ==0) {
        if(strcmp(payloadChar,"reverse") == 0) {
          fans[idint].directionState = true;
        } else {
          fans[idint].directionState = false;
        }
      }
    } else {
      // Invalid ID
      return;
    }
  }
}

void hamptonbay3RF(int long value, int prot, int bits) {
    if( (prot >10 && prot<15)  && bits == 12 && ((value&0x0c0)==0x0c0)) {
      unsigned long t=millis();
      if(value == lastvalue) {
        if(t - lasttime < NO_RF_REPEAT_TIME)
          return;
        lasttime=t;
      }
      lastvalue=value;
      lasttime=t;
      int dipId = (~value)&0x0f;
      // Got a correct id in the correct protocol
      if(dipId < 16) {
        // Blank out id in message to get light state
        switch((value>>4)&0xff) {
          case 0xef: // Direction
            fans[dipId].directionState=!(fans[dipId].directionState);
            break;
          case 0x7f: // Light
            fans[dipId].lightState = !(fans[dipId].lightState);
            break;
          case 0xbf: // Fan Low
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_LOW;
            break;
          case 0xfd: // Fan Med
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_MED;
            break;
          case 0xfe: // Fan Hi
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_HI;
            break;
          case 0xdf: // Fan Off
            fans[dipId].fanState = false;
            break;
        }
        postStateUpdate(dipId);
      }
    }
}

void hamptonbay3MQTTSub(boolean setup) {
  client.subscribe(SUBSCRIBE_TOPIC_CMND);
  if(setup) client.subscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}

void hamptonbay3Setup() {
  lasttime=0;
  lastvalue=0;
  // initialize fan struct 
  for(int i=0; i<16; i++) {
    fans[i].powerState = false;
    fans[i].lightState = false;
    fans[i].fanState = false;  
    fans[i].fanSpeed = FAN_LOW;
    fans[i].directionState = false;
  }
}
   
void hamptonbay3SetupEnd() {
  client.unsubscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}
