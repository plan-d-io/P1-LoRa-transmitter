#include "boards.h"
#include <LoRa.h>
#include "mbedtls/aes.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include <esp32/rom/crc.h>
#include <elapsedMillis.h>
#include "time.h"

#define HWSERIAL Serial1 //Use hardware UART for communication with digital meter

/*Keep or change these three settings, but make sure the are identical on both transmitter and receiver!*/
uint8_t networkNum = 127;                 //Must be unique for every transmitter-receiver pair
char plaintextKey[] = "abcdefghijklmnop"; //The key used to encrypt/decrypt the meter telegram
char networkID[] = "myLoraNetwork";       //Used to generate the initialisation vector for the AES encryption algorithm

/*SF, BW and  wait times used to sync transmitter and receiver
 Needs to be indentical on both transmitter and receiver*/
static const byte loraConfig[][4] ={
  {12, 125, 8, 38},
  {12, 250, 3, 20},
  {11, 250, 2, 10},
  {10, 250, 2, 8},
  {9, 250, 2, 8},
  {8, 250, 2, 8},
  {7, 250, 1, 8}
};

/*ms between updates to keep in line with 1% LoRa duty cycle, for single and three phase meter telegrams
 Needs to be indentical on both transmitter and receiver*/
static const unsigned long loraUpdate[][7] ={
  {230474, 107207, 57803, 31099, 16700, 9000, 4500},
  {372346, 173169, 93369, 50238, 26977, 14538, 7000}
  };

bool telegramDebug = true;

/*Template of meter telegram*/
float meterData[] = {
  999999.999, //totConT1
  999999.999, //totConT2
  999999.999, //totInT1
  999999.999, //totInT2
  99.999,     //TotpowCon
  99.999,     //TotpowIn
  999999.999, //avgDem
  999999.999, //maxDemM
  999.99,     //volt1
  999.99,     //current1
  999999.999, //totGasCon
  999999.999, //totWatCon
  99.999,     //powCon1
  999999.999, //powCon2
  999999.999, //powCon3
  99.999,     //powIn1
  999999.999, //powIn2
  999999.999,  //powIn3
  999.99,     //volt2
  999.99,     //volt3
  999.99,     //current2
  999.99,     //current3
  0,          //pad
  0           //pad
};
  
elapsedMillis runLoop, waitForSync, waitForSend, sinceLastMsg;
unsigned long waitForSyncVal, waitForSendVal;
int syncMode, syncTry;
int syncCount = 0;
int accPacketLoss = 25;
unsigned int runPacketLoss;
bool revertTried;
byte meterType, delayType, setSF, setBW;
byte packetCounter, telegramCounter, telegramAckCounter;
boolean gasFound, waterFound, threePhase;

unsigned int payloadLength;
unsigned int aesBufferSize = 96;
uint32_t romCRC;
unsigned char key[32];
unsigned char iv[16], iv2[16];

bool timeSet;
elapsedMillis sinceClockCheck;

void setup() {
  initBoard();
  // When the power is turned on, a delay is required.
  delay(1500);
  setLCD(0, 0, 0);
  Serial.begin(115200);
  HWSERIAL.begin(115200, SERIAL_8N1, 12, 13);
  Serial.println("LoRa P1 transmitter");
  setTimezone("CET-1CEST,M3.5.0,M10.5.0/3"); // Timezone for Brussels
  /*Start LoRa radio*/
  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
  if (!LoRa.begin(LoRa_frequency)) {
      Serial.println("Starting LoRa failed!");
      while (1);
  }
  LoRa.setSyncWord(0xF3);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  Serial.println("Sending at SF12 125");
  /*Create a 32 byte key for use by the AES encryptor by SHA256 hashing the user provided key*/
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  const size_t keyLength = strlen(plaintextKey);         
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *) plaintextKey, keyLength);
  mbedtls_md_finish(&ctx, key);
  mbedtls_md_free(&ctx);
  /*Create a 16 byte initialisation vector for the AES encryptor by SHA1 hashing the user provided network name*/
  md_type = MBEDTLS_MD_SHA1;
  const size_t networkIDLength = strlen(networkID);         
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *) networkID, networkIDLength);
  mbedtls_md_finish(&ctx, iv);
  mbedtls_md_free(&ctx);
  /*Init some runtime variables*/
  waitForSendVal = loraConfig[syncCount][2]*1000;
  waitForSyncVal = loraConfig[syncCount][3]*1000;
  setSF = loraConfig[syncCount][0];
  setBW = loraConfig[syncCount][1];
  syncMode = 0;
  waitForSync = 380000;
}

void loop() {
  if(syncMode < 0){
    setLCD(1, 0, 0);
    if (HWSERIAL.available() > 0) {
      String s=HWSERIAL.readStringUntil('!');
      s = s + HWSERIAL.readStringUntil('\n');
      s = s + '\n';
      Serial.println("Received meter telegram");
      splitTelegram(s);
      if(runLoop > loraUpdate[delayType][syncCount]){
        sendTelegram(meterType);
        runLoop = 0;
        uint8_t packetLoss = telegramCounter - telegramAckCounter;
        /*
        if(telegramCounter == 255){ //ensure correct rollback of both counters
          telegramCounter = 0;
          telegramAckCounter = 0;
        }*/
        if(packetLoss > 64 || runPacketLoss > accPacketLoss){ //if packetloss is too big, resync (receiver should have reverted to initial settings too)
          reSync();
        }
      }
    }
    if(sinceLastMsg > 300000) reSync();
  }
  else syncLoop();
  onReceive(LoRa.parsePacket());
  if(sinceClockCheck >= 600000){
    timeSet = false;
    sinceClockCheck = 0;
  }
}

void onReceive(int packetSize) {
    if (packetSize == 0)
        return;  // if there's no packet, return.  ?????,???
  // read packet header bytes:
  byte inNetworkNum = LoRa.read();
  byte inMessageType = LoRa.read();
  byte inMessageCounter = LoRa.read();
  byte inPayloadSize = LoRa.read();
  Serial.print("Receiving LoRa message, meant for network ID ");
  Serial.print(inNetworkNum);
  Serial.print(", message type ");
  Serial.print(inMessageType);
  Serial.print(" witch message count ");
  Serial.print(inMessageCounter);
  Serial.print(", length of ");
  Serial.print(inPayloadSize);
  Serial.println(" bytes");
  if(inNetworkNum != networkNum) {
    Serial.println("Wrong network ID");
    return;
  }
  byte incoming[inPayloadSize];
  int i = 0;
  while(i<inPayloadSize) {
    incoming[i]= LoRa.read();
    i++;
  }
  if(inMessageType == 8 || inMessageType == 128){
    processTelegramAck(inMessageType, inMessageCounter, incoming);
    sinceLastMsg = 0;
  }
  else if(inMessageType == 178 || inMessageType == 93){
    processSyncAck(inMessageType, inMessageCounter, incoming);
    sinceLastMsg = 0;
  }
  else if(inMessageType == 231){ //resync request
    reSync();
    sinceLastMsg = 0;
  }
}

void setTimezone(String timezone){
  Serial.printf("  Setting Timezone to %s\n", timezone.c_str());
  setenv("TZ", timezone.c_str(), 1);  // Adjust the TZ.
  tzset();
}
