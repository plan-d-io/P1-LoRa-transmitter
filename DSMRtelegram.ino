void splitTelegram(String rawTelegram){
  int eof = rawTelegram.lastIndexOf('\n');
  //Serial.println(rawTelegram);
  int delimStart = 0;
  int delimEnd = 0;
  while(delimEnd < eof){
    delimEnd = rawTelegram.indexOf('\n', delimStart);       //Get the location of the newline char at the end of the line
    String s = rawTelegram.substring(delimStart, delimEnd); //Extract one line from the telegram
    delimStart = delimEnd+1;                                //Set the start of the next line (for the next iteration)
    /*Every line contains a key(value) pair, e.g. 1-0:2.7.0(01.100*kW)*/
    byte valueStart = s.indexOf('(');                       //The key is the part before the first bracket, e.g. 1-0:2.7.0
    String key = s.substring(0, valueStart);
    byte valueEnd = s.lastIndexOf(')');                     //The value is the part between the brackets, e.g. 01.100*kW
    String value = s.substring(valueStart+1, valueEnd);
    /*We now have every key(value) pair, so lets do something with them*/
    float splitValue;
    String splitUnit, splitString;
    struct tm splitTime;
    time_t splitTimestamp;
    //Serial.println(key);
    //Serial.println(value);
    /*Consumed energy tariff 1*/
    if(key == "1-0:1.8.1"){
      splitWithUnit(value, splitValue, splitUnit);
      meterData[0] = splitValue;
    }
    /*Consumed energy tariff 2*/
    if(key == "1-0:1.8.2"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[1] = splitValue;
    }
    /*Injected energy tariff 1*/
    if(key == "1-0:2.8.1"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[2] = splitValue;
    }
    /*Injected energy tariff 2*/
    if(key == "1-0:2.8.2"){    
      splitWithUnit(value, splitValue, splitUnit);
      meterData[3] = splitValue;
    }
    /*Total actual power consumption*/
    if(key == "1-0:1.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[4] = splitValue;
    }
    /*Total actual power injection*/
    if(key == "1-0:2.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[5] = splitValue;
    }
    /*Current average demand*/
    if(key == "1-0:1.4.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[6] = splitValue;
    }
    /*Max average demand this month*/
    if(key == "1-0:1.6.0"){  
      splitWithTimeAndUnit(value, splitValue, splitUnit, splitTime, splitTimestamp);
      meterData[7] = splitValue;
    }
    /*Volt 1*/
    if(key == "1-0:32.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[8] = splitValue;
    }
    /*Current 1*/
    if(key == "1-0:31.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[9] = splitValue;
    }
    /*Natural gas consumption*/ //temporary implementation
    if(key == "0-1:24.2.3"){
      //splitWithTimeAndUnit(value, splitValue, splitUnit, splitTime, splitTimestamp);
      //meterData[10] = splitValue;
      gasFound = true ;
    }
    /*Water consumption*/ //temporary implementation
    if(key == "0-2:24.2.1"){  
      //splitWithTimeAndUnit(value, splitValue, splitUnit, splitTime, splitTimestamp);
      //meterData[11] = splitValue;
      waterFound = true ;
    }
    /*Aactual power consumption 1*/
    if(key == "1-0:21.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[12] = splitValue;
    }
    /*Aactual power consumption 2*/
    if(key == "1-0:41.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[13] = splitValue;
    }
    /*Aactual power consumption 3*/
    if(key == "1-0:61.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[14] = splitValue;
    }
    /*Aactual power injection 1*/
    if(key == "1-0:22.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[15] = splitValue;
    }
    /*Aactual power injection 2*/
    if(key == "1-0:42.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[16] = splitValue;
    }
    /*Aactual power injection 3*/
    if(key == "1-0:62.7.0"){      
      splitWithUnit(value, splitValue, splitUnit);
      meterData[17] = splitValue;
    }
    /*Volt 2*/
    if(key == "1-0:52.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[18] = splitValue;
      threePhase = true;
    }
    /*Volt 3*/
    if(key == "1-0:72.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[19] = splitValue;
    }
    /*Current 2*/
    if(key == "1-0:51.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[20] = splitValue;

    }
    /*Current 3*/
    if(key == "1-0:71.7.0"){  
      splitWithUnit(value, splitValue, splitUnit);
      meterData[21] = splitValue;
    }
  }
  if(threePhase){
    delayType = 1;
    meterType = 3;
    if(gasFound) meterType = 31;
    if(waterFound) meterType = 32;
    if(gasFound && waterFound) meterType = 33;
  }
}

void splitWithUnit(String &value, float &splitValue, String &splitUnit){
  /*Split a value containing a unit*/
  int unitStart = value.indexOf('*');
  splitValue = value.substring(0, unitStart).toFloat();
  splitUnit = value.substring(unitStart+1);
}

void splitNoUnit(String &value, float &splitValue){
  /*Split a value containing no unit*/
  splitValue = value.toFloat();
}

void splitMeterTime(String &value, struct tm &splitTime, time_t &splitTimestamp){
  /*Split a timestamp, returning a tm struct and a time_t object*/
  splitTime.tm_year = (2000 + value.substring(0, 2).toInt()) - 1900; //Time.h years since 1900, so deduct 1900
  splitTime.tm_mon = (value.substring(2, 4).toInt()) - 1; //Time.h months start from 0, so deduct 1
  splitTime.tm_mday = value.substring(4, 6).toInt();
  splitTime.tm_hour = value.substring(6, 8).toInt();
  splitTime.tm_min = value.substring(8, 10).toInt();
  splitTime.tm_sec = value.substring(10, 12).toInt();
  splitTimestamp =  mktime(&splitTime);
  /*Metertime is in local time. DST shows the difference in hours to UTC*/
  if(value.substring(12) == "S"){
    splitTime.tm_isdst = 1;
    splitTimestamp = splitTimestamp - (2*3600);
  }
  else if(value.substring(12) == "W"){
    splitTime.tm_isdst = 0;
    splitTimestamp = splitTimestamp - (1*3600);
  }
  else splitTime.tm_isdst = -1;
}

void splitWithTimeAndUnit(String &value, float &splitValue, String &splitUnit, struct tm &splitTime, time_t &splitTimestamp){
  /*Split a timestamped value containing a unit (e.g. Mbus value)*/
  String buf = value;
  int timeEnd = value.indexOf(')');
  int valueStart = value.indexOf('(')+1;
  value = buf.substring(0, timeEnd);
  splitMeterTime(value, splitTime, splitTimestamp);
  value = buf.substring(valueStart);
  splitWithUnit(value, splitValue, splitUnit);
}
