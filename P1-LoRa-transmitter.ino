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

/*Template of meter telegram*/
float meterData[] = {
  123456.789, //totConT1
  123456.789, //totConT2
  456789.123, //totInT1
  321987.654, //totInT2
  54.321,     //TotpowCon
  0,          //TotpowIn
  87654.321,  //avgDem
  12345.678,  //maxDemM
  543.21,     //volt1
  543.21,     //current1
  87654.321,  //totGasCon
  12345.678,  //totWatCon
  54.321,     //powCon1
  0,          //powCon2
  0,          //powCon3
  54.321,     //powIn1
  0,          //powIn2
  54.321,     //powIn3
  123.45,     //volt2
  234.36,     //volt3
  0,          //current2
  543.21,      //current3
  0,          //pad
  0           //pad
};
  
elapsedMillis runLoop, waitForSync, waitForSend;
unsigned long waitForSyncVal, waitForSendVal;
int syncMode, syncTry;
int syncCount = 0;
byte meterType, delayType, setSF, setBW;
byte packetCounter, telegramCounter, telegramAckCounter;
boolean gasFound, waterFound, threePhase;

unsigned int payloadLength;
unsigned int aesBufferSize = 96;
uint32_t romCRC;
unsigned char key[32];
unsigned char iv[16], iv2[16];

void setup() {
  initBoard();
  // When the power is turned on, a delay is required.
  delay(1500);
  setLCD(0, 0, 0);
  Serial.begin(115200);
  HWSERIAL.begin(115200, SERIAL_8N1, 12, 13);
  Serial.println("LoRa P1 transmitter");
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
        if(packetLoss > 128){ //if packetloss is too big, resync (receiver should have reverted to initial settings too)
          reSync();
        }
      }
    }
  }
  else syncLoop();
  onReceive(LoRa.parsePacket());
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
  }
  else if(inMessageType == 178 || inMessageType == 93){
    processSyncAck(inMessageType, inMessageCounter, incoming);
  }
}
