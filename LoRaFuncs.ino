void processSyncAck(byte inMsgType, byte inMsgCounter, byte msg[]){
  if(msg[0] > 6 && msg[0] < 13 && (msg[1] == 125 || msg[1] == 250)){ //check for validity of link parameters
    if(inMsgType == 178 ){ //ACK start sync message
      if(syncMode > -1){
        if(sizeof(msg) > 2){
          if(msg[0] == setSF && msg[1] == setBW && msg[2] == byte(waitForSyncVal/1000)){
            Serial.print("Received sync signal ACK, setting SF to ");
            Serial.print(setSF);
            Serial.print(", BW to ");
            Serial.print(setBW);
            Serial.print(", and timer to ");
            Serial.print(waitForSyncVal/1000);
            Serial.println(" seconds");
            LoRa.setSpreadingFactor(setSF);
            LoRa.setSignalBandwidth(setBW*1000);
            waitForSend = waitForSendVal;
            waitForSync = 0;
            syncMode = 2;
          }
          else{
            Serial.println("Returned RF link parameters do not match");
            return;
          }
        }
      }
    }
    else if(inMsgType == 93 ){ //ACK stop sync signal
      if(sizeof(msg) > 1){
        if(msg[0] == setSF && msg[1] == setBW){
          if(syncMode > 1){
            waitForSync = 0;
            syncMode = 9;
            Serial.println("Received stop sync signal ACK");
          }
        }
      }
    }
  }
  else{
    Serial.println("Invzalid RF link parameters returned");
    return;
  }
}

void sendSync(bool startSync){
  setSF = loraConfig[syncCount][0];
  setBW = loraConfig[syncCount][1];
  waitForSendVal = loraConfig[syncCount][2]*1000;
  waitForSyncVal = loraConfig[syncCount][3]*1000;
  Serial.print("Sending");
  if(!startSync) Serial.print(" stop");
  Serial.print(" sync beacon with SF");
  Serial.print(setSF);
  Serial.print(", BW");
  Serial.print(setBW);
  if(startSync){
    Serial.print(", timer ");
    Serial.print(waitForSyncVal/1000);
  }
  Serial.println("");
  unsigned long tempMillis = millis();
  LoRa.beginPacket();
  LoRa.write(networkNum);
  if(startSync) LoRa.write(170);
  else LoRa.write(85);
  LoRa.write(packetCounter);
  if(startSync) LoRa.write(4);
  else LoRa.write(3);
  LoRa.write(syncCount);
  LoRa.write(setSF);
  LoRa.write(setBW);
  if(startSync) LoRa.write(byte(waitForSyncVal/1000)); //Send timing beacon (window in which receiver must listen to sync requests)
  LoRa.endPacket();
  packetCounter = 0;
  Serial.print("Transmission took ");
  Serial.print(millis() - tempMillis);
  Serial.println(" ms");
}

void sendTelegram(byte telegramType){
  Serial.print("Sending telegram type ");
  Serial.println(telegramType);
  /*Create a plaintext input buffer for the AES encryptor and set all elements to 0
    IMPORTANT: the AES encrypter expects the buffer to be a multiple of 16bytes in size*/
  unsigned char plaintextPayload[aesBufferSize];
  memset(plaintextPayload,0,sizeof(plaintextPayload));
  /*Set the metertype. If it's a single-phase meter, use a smaller payload to increase transmission speed*/
  if(telegramType == 0 || telegramType == 1) payloadLength = 12; //number of 32bit values encoded into the payload
  else if(telegramType >= 3) payloadLength = 24;
  unsigned int payloadByteSize = payloadLength*4; //number of bytes required to encode the 32bit values
  /*Only copy the values we need into the input buffer for the AES encryptor. The rest of the buffer remains 0 (zero-padded input)*/
  for(int i = 0; i < payloadLength; i++){
    unsigned long tempVar = meterData[i]*1000; //Convert the float value into unsigned long
    int j = i*4;
    /*Split the unsigned long into individual bytes and copy them into the buffer*/
    plaintextPayload[j+0] = tempVar & 0xFF;   
    plaintextPayload[j+1] = (tempVar >> 8) & 0xFF;
    plaintextPayload[j+2] = (tempVar >> 16) & 0xFF;
    plaintextPayload[j+3] = (tempVar >> 24) & 0xFF;
  }
  //Serial.print("Plaintext byte array: ");
  int k = payloadByteSize - 1;
  /*for(int l = 0; l< payloadByteSize; l++){
    Serial.print(plaintextPayload[l], DEC);
    if(l<k) Serial.print(", ");
  }
  Serial.println("");*/
  /*The resulting input array for the AES encryptor*/
  k = payloadByteSize - 1;
  memcpy(iv, iv2, sizeof(iv));
  /*Create the output buffer for the AES encryptor and set all elements to 0*/
  unsigned char encryptedPayload[aesBufferSize]; 
  memset(encryptedPayload,0,sizeof(encryptedPayload));
  /*Load the AES context*/
  mbedtls_aes_context aes;
  mbedtls_aes_init( &aes );
  mbedtls_aes_setkey_enc( &aes, key, 256 );
  mbedtls_aes_crypt_cbc( &aes, MBEDTLS_AES_ENCRYPT, aesBufferSize, iv, plaintextPayload, encryptedPayload ); //AES only accepts payloads of 16 byte multiples!
  mbedtls_aes_free( &aes );
  k = payloadByteSize - 1;
  romCRC = (~crc32_le((uint32_t)~(0xffffffff), (const uint8_t*)plaintextPayload, 8))^0xffffffFF;
  if(syncMode < 0) Serial.println("Sending meter telegram over LoRa");
  unsigned long tempMillis = millis();
  /*Transmit everything*/
  LoRa.beginPacket();        // start packet.  ???
  LoRa.write(networkNum);
  LoRa.write(telegramType);
  LoRa.write(telegramCounter);
  LoRa.write(byte(payloadByteSize));
  for(int i = 0; i < payloadByteSize; i++){
    LoRa.write(encryptedPayload[i]);
  }
  LoRa.endPacket();
  telegramCounter++;
  Serial.print("Transmission took ");
  Serial.print(millis() - tempMillis);
  Serial.println(" ms");
  runPacketLoss++;
}

void processTelegramAck(byte inMsgType, byte inMsgCounter, byte msg[]){
  //check en verwerk meter telegram ACKs
  //als ack = virtueel telegram, increment counter
  if(sizeof(msg) > 3){
    Serial.println("Message is CRC check");
    unsigned long recVar = 0;
    recVar += msg[3] << 24;
    recVar += msg[2] << 16;
    recVar += msg[1] << 8;
    recVar += msg[0];
    if(recVar == romCRC){
      Serial.println("CRC check matches");
      //telegramAckCounter++;
      telegramAckCounter = inMsgCounter;
      runPacketLoss = 0;
      setLCD(14, 0, 0);
    }
  }
  else Serial.println("CRC ACK too small, discarded");
}

void reSync(){
  Serial.println("Restarting sync procedure");
  Serial.println("Sending restart sync ACK");
  LoRa.beginPacket();
  LoRa.write(networkNum);
  LoRa.write(24);
  LoRa.write(telegramCounter);
  LoRa.write(0);
  LoRa.endPacket();
  telegramCounter = 0;
  telegramAckCounter = 0;
  syncCount = 0;
  syncTry = 0;
  setSF = loraConfig[syncCount][0];
  setBW = loraConfig[syncCount][1];
  waitForSendVal = loraConfig[syncCount][2]*1000;
  waitForSyncVal = loraConfig[syncCount][3]*1000;
  waitForSync = 300000;
  waitForSend = 6000;
  LoRa.setSpreadingFactor(setSF);
  LoRa.setSignalBandwidth(setBW*1000);
  syncMode = 0;
}

void syncLoop(){
  if(syncMode == 0){ //initial state: periodically send sync start beacon to see if receiver replies
    if(waitForSync > 180000){
      if(waitForSend > 3000) setLCD(10, 0, 0); //update LCD ever 3s
      if(waitForSend > 6000){ //send sync request every 6s
        Serial.println("Sending sync discovery");
        setLCD(11, 0, 0);
        sendSync(true);
        waitForSend = 0;
        syncTry++;
        if(syncTry > 6){ //wait three minutes before trying again
          Serial.println("No response, waiting three minutes before trying again");
          waitForSync = 0;
          syncTry = 0;
        }
      }
    }
    else setLCD(12, 0, 0);
  }
  else if(syncMode == 1){ //send sync beacon with new rf channel parameters and wait for confirmation of receiver
    if(waitForSync > waitForSyncVal){
      if(waitForSend > waitForSendVal){ //send sync beacon every now and then, wait for the rest of this loop for confirmation
        Serial.println("Sending sync request");
        sendSync(true);
        waitForSend = 0;
        syncTry++;
        setLCD(13, syncTry, 0);
        if(syncCount == 0){ //cannot switch to even lower rf channel parameters here, so keep trying
          if(syncTry > 6){ //wait five minutes before trying again
            Serial.println("No response, waiting five minutes before trying again");
            waitForSync = 0;
            syncTry = 0;
          }
        }
        else{
          if(syncTry > 2){ //stop and stay on this rf channel
            Serial.println("Did not receive sync ACK, stopping sync");
            if(syncCount > 0) syncCount--;
            sendSync(false);
            waitForSend = 0;
            syncMode = 8;
          }
        }
      }
    }
    else setLCD(14, 0, 0);
  }
  else if(syncMode == 2){
    //Send calibration telegrams
    if(waitForSync < waitForSyncVal){ //during the sync interval, send calibration messages
      if(waitForSend > waitForSendVal){
        Serial.println("Sending calibration telegram");
        sendTelegram(0);
        waitForSend = 0;
        syncTry = 0;
        setLCD(14, 0, 0);
      }
    }
    else{
      waitForSend = 0;
      syncMode = 3; //after sending for the requested interval, switch to processing the results
    }
  }
  else if(syncMode == 3){ //additional wait to account for transmission delay
    setLCD(14, 0, 0);
    if(waitForSend > 4500){
      waitForSend = 0;
      syncMode = 4;
    }
  }
  else if(syncMode == 4){
    Serial.print("We have sent ");
    Serial.print(telegramCounter);
    Serial.print(" telegrams, received ");
    Serial.println(telegramAckCounter);
    waitForSend = 0;
    waitForSync = 0;
    if(telegramCounter > telegramAckCounter*2){
      Serial.println("Stopping sync");
      if(syncCount > 0) syncCount--; //revert to previous settings or stay on the first setting
      sendSync(false);
      waitForSend = 0;
      syncMode = 8;
    }
    else{
      syncCount++; //try better settings
      if(syncCount < 7){
        waitForSync = waitForSyncVal;
        waitForSyncVal = loraConfig[syncCount][3]*1000;
        waitForSend = waitForSendVal;
        syncMode = 1;
      }
      else{
        Serial.println("Best possible settings reached, stopping sync");
        syncCount--;
        sendSync(false);
        waitForSend = 0;
        syncMode = 8;
      }

    }
    telegramCounter = 0;
    telegramAckCounter = 0;
  }
  else if(syncMode == 8){
    setLCD(15, 0, 0);
    if(waitForSend > waitForSendVal){
      Serial.println("Did not receive stop sync ack, repeating");
      sendSync(false);
      syncMode = 9;
    }
  }
  else if(syncMode == 9){
    setLCD(16, 0, 0);
    Serial.print("Setting SF to ");
    Serial.print(setSF);
    Serial.print(" and BW to ");
    Serial.print(setBW);
    Serial.println(", stopping sync");
    LoRa.setSpreadingFactor(setSF);
    LoRa.setSignalBandwidth(setBW*1000);
    if(setSF == 7) accPacketLoss = 26;
    else if (setSF == 8) accPacketLoss = 21;
    else if (setSF == 9) accPacketLoss = 15;
    else if (setSF == 10) accPacketLoss = 9;
    else if (setSF == 8) accPacketLoss = 6;
    else if (setSF == 12) accPacketLoss = 3;
    telegramCounter = 0;
    telegramAckCounter = 0;
    syncMode = -1;
  }
}
