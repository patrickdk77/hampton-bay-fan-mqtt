#include "rf-fans.h"


#define BASE_TOPIC HAMPTONBAY_BASE_TOPIC

#define CMND_BASE_TOPIC CMND_TOPIC BASE_TOPIC
#define STAT_BASE_TOPIC STAT_TOPIC BASE_TOPIC

#define SUBSCRIBE_TOPIC_CMND CMND_BASE_TOPIC "/#"

#define SUBSCRIBE_TOPIC_STAT_SETUP STAT_BASE_TOPIC "/#"

#ifndef HAMPTONBAY_TX_FREQ
  #define TX_FREQ     303.631 // FAN-9T
#else
  #define TX_FREQ HAMPTONBAY_TX_FREQ
#endif

// RC-switch settings
#ifndef HAMPTONBAY_PROTOCOL
  #define RF_PROTOCOL 6
#else
  #define RF_PROTOCOL HAMPTONBAY_PROTOCOL
#endif
#define RF_REPEATS 8 
                                    
#ifdef HAMPTONBAY_BRIGHTNESS
  #define HAMPTONBAY_MASK 0x1c001f
#else
  #define HAMPTONBAY_MASK 0x1c3f1f
#endif

// Keep track of states for all dip settings
static fan fans[16];

static int calculateBrightness(int value, bool mode) {
  // Brightness values are based on an inverted integer scale,
  // where 50 = 1% and 1 = 100%. 0 is off. MQTT range is 1-255.
  if (value == 0) {
    return 0;
  }
  if (!mode) {
    int invertedIndex = map(value, 1, 255, 50, 1); // Encode for transmit
    // Return as 6-bit binary (00111111)
    // Minimum safe brightness is 41 = 20%
    return (invertedIndex > 41 ? 41 : invertedIndex) & 0x3F;
  }
  else {
    return map(value, 50, 1, 1, 255); // Decode after receive
  }
}

static int calculateChecksum(int number) {
#ifdef HAMPTONBAY_CHECKSUM
  // Checksum required on some models
  // It is calculated by adding each of the 4-bit chunks that precede it
  // (16-bits in all) and putting the remainder in these bits
  int sum = 0;

  while (number > 0) {
    sum += number & 0xF;
    number >>= 4;
  }
  return sum % 16;
#else
  return 0;
#endif
}


static void postStateUpdate(int id) {
  sprintf(outTopic, "%s/%s/fan", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].fanState ? "ON":"OFF", true);
  sprintf(outTopic, "%s/%s/speed", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fanStateTable[fans[id].fanSpeed], true);
  sprintf(outTopic, "%s/%s/light", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, fans[id].lightState ? "ON":"OFF", true);

#ifdef HAMPTONBAY_BRIGHTNESS
  *outPercent='0\0';
  if(fans[id].lightState) {
    sprintf(outPercent,"%d",fans[id].lightBrightness);
  }
  sprintf(outTopic, "%s/%s/brightness", STAT_BASE_TOPIC, idStrings[id]);
  client.publish(outTopic, outPercent, true);
#endif

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

static void transmitState(int fanId) {
  mySwitch.disableReceive();         // Receiver off
  ELECHOUSE_cc1101.setMHZ(TX_FREQ);
  ELECHOUSE_cc1101.SetTx();           // set Transmit on
  mySwitch.enableTransmit(TX_PIN);   // Transmit on
  mySwitch.setRepeatTransmit(RF_REPEATS); // transmission repetitions.
  mySwitch.setProtocol(RF_PROTOCOL);        // send Received Protocol

  // Build out RF code
  //   Code follows the 21 bit pattern
  //   000aaaabbbbbblffzzzzz
  //   Where a is the inversed/reversed dip setting, 
  //     l is light state, ff is fan speed
  //     b can be light brightness, z can be checksum
  int fanRf = fans[fanId].fanState ? fans[fanId].fanSpeed : 0;
#ifdef HAMPTONBAY_BRIGHTNESS
  int lightRf = fans[fanId].lightState ? calculateBrightness(fans[fanId].lightBrightness, 0) : 0;
#else
  int lightRf = fans[fanId].lightState;
#endif
  int rfVal = dipToRfIds[((~fanId)&0x0f)] << 9 | llightRf << 2 | fanRf;
  int rfCode = rfVal << 5 | calculateChecksum(rfVal);;
            
  mySwitch.send(rfCode, 21);      // send 21 bit code
  mySwitch.disableTransmit();   // set Transmit off
  ELECHOUSE_cc1101.setMHZ(RX_FREQ);
  ELECHOUSE_cc1101.SetRx();      // set Receive on
  mySwitch.enableReceive(RX_PIN);   // Receiver on
  Serial.print("Sent command hamptonbay: ");
  Serial.print(fanId);
  Serial.print(" ");
  for(int b=21; b>0; b--) {
    Serial.print(bitRead(rfCode,b-1));
  }
  Serial.println("");
  postStateUpdate(fanId);
}

void hamptonbayMQTT(char* topic, char* payloadChar, unsigned int length) {
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
          } else if(percent > (FAN_PCT_LOW + FAN_PCT_OVER)) {
            fans[idint].fanSpeed=FAN_MED;
          } else {
            fans[idint].fanSpeed=FAN_LOW;
          }
        } else {
          fans[idint].fanState = false;
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
        } else {
          fans[idint].fanState = false;
        }
      } else if(strcmp(attr,"speed") ==0) {
        if(strcmp(payloadChar,"+") ==0) {
          fans[idint].fanState = true;
          switch(fans[idint].fanSpeed) {
            case FAN_LOW:
              fans[idint].fanSpeed=FAN_MED;
              break;
            case FAN_MED:
              fans[idint].fanSpeed=FAN_HI;
              break;
            case FAN_HI:
              fans[idint].fanSpeed=FAN_HI;
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
              break;
            case FAN_MED:
              fans[idint].fanSpeed=FAN_LOW;
              break;
            case FAN_LOW:
              fans[idint].fanSpeed=FAN_LOW;
              break;
            default:
              if(fans[idint].fanSpeed<FAN_LOW)
                fans[idint].fanSpeed++;
              break;
          }
        } else if(strcmp(payloadChar,"high") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_HI;
        } else if(strcmp(payloadChar,"medium") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_MED;
        } else if(strcmp(payloadChar,"low") ==0) {
          fans[idint].fanState = true;
          fans[idint].fanSpeed = FAN_LOW;
        } else {
          fans[idint].fanState = false;
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
        } else {
          fans[idint].lightState = false;
        }
      } else if(strcmp(attr,"brightness") ==0) {
        int payloadInt = atoi(payloadChar);
        fans[idint].lightBrightness = payloadInt;
        if(payloadInt == 0) {
          fans[idint].lightState = true;
        } else {
          fans[idint].lightState = false;
        }
      }
      transmitState(idint);
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
      } else if(strcmp(attr,"brightness") ==0) {
        int payloadInt = atoi(payloadChar);
        fans[idint].lightBrightness = payloadInt;
        if(payloadInt == 0) {
          fans[idint].lightState = true;
        } else {
          fans[idint].lightState = false;
        }
      }
    }
  }
}

void hamptonbayRF(int long value, int prot, int bits) {
    if( (prot == 6  || prot == 11 || prot == HAMPTONBAY_PROTOCOL) && bits == 21 && ((value&HAMPTONBAY_MASK)==0x000000)) {
      int id = (~value >> 14)&0x0f;
      // Got a correct id in the correct protocol
      if(id < 16) {
        // reverse order of bits
        int dipId = dipToRfIds[id];
        // Blank out id in message to get light state
#ifdef HAMPTONBAY_BRIGHTNESS
        int states = value & 0b11111111;
        fans[dipId].lightState = states >> 7;
#else
        int states = value & 0b11111111;
        fans[dipId].lightState = states >> 7;
#endif
        // Blank out light state to get fan state
        switch((states & 0b01111111) >> 5) {
          case 0:
            fans[dipId].fanState = false;
            break;
          case 1:
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_HI;
            break;
          case 2:
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_MED;
            break;
          case 3:
            fans[dipId].fanState = true;
            fans[dipId].fanSpeed = FAN_LOW;
            break;
        }
        postStateUpdate(dipId);
      }
    }
}

void hamptonbayMQTTSub(boolean setup) {
  client.subscribe(SUBSCRIBE_TOPIC_CMND);
  if(setup)   client.subscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}

void hamptonbaySetup() {
  // initialize fan struct 
  for(int i=0; i<16; i++) {
    fans[i].lightState = false;
    fans[i].lightBrightness = 0;
    fans[i].fanState = false;  
    fans[i].fanSpeed = FAN_LOW;
  }
}

void hamptonbaySetupEnd() {
  client.unsubscribe(SUBSCRIBE_TOPIC_STAT_SETUP);
}
