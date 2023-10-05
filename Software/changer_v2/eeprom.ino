// The autochanger reads and writes content from the on board eeprom.
//
//

// Pull out our saved configs.
// This is only run on boot.
void readEEPROM() {
  byte offset = 0;
  //Read our values from EEPROM

  // Read what the saved patterns.
  EEPROM.get(EEPROMPattern, pattern);
  if(DEBUG) {
    Serial.println(F("Patterns: "));
    for(int x = 0; x < 4; x++) {
      // if we're in debug mode, just check if the pattern needs to be reset.
      if(pattern[x][0] == 255) {
        RestoreDefault(x);
      }
      Serial.print(F("  "));
      for(int y = 0; y < 21; y++) {
        Serial.print(pattern[x][y]);
        Serial.print(F(":"));
      }
      Serial.println();
    }
  }

  // Read the Servo config. 
  EEPROM.get(EEPROMServo, servos);
  // do some error checking and set some defaults.
  // god this sucks.

  for(int x = 0; x < 4; x++) {
    if(servos[x][0] > 4096)
      servos[x][0] = 150;
    if(servos[x][1] > 4096)
      servos[x][1] = 600;
    if(servos[x][2] > 4096)
      servos[x][2] = 600;
  }
  
  
  if(DEBUG) {
    
    Serial.println(F("Servo Config: "));
    for(int x = 0; x < 4; x++) {
      Serial.print(F("  "));
      for(int y = 0; y < 3; y++) {
        Serial.print(servos[x][y]);
        Serial.print(F(":"));
      }
      Serial.println();
    }
    
  }
  

  // Read our current servo iterations.
  EEPROM.get(EEPROMServoCount,servoCount);

  // Show us how many actions each servo has made.
  // This happens even if not in debug mode
  Serial.println(F("Servo movement counts: "));
  for(int i = 0; i < 4; i++) {
    Serial.print(F("  Servo "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.println(servoCount[i]);
  }
}

// Save our config.
// most of the time, we're just saving the updated movement counts.
void writeEEPROM() {
  // Every 60 seconds we update the eeprom.
  // This is safe and should only write if the value has changed.
  if ( millis() > EEPromTimeStamp+EEPromUpdateTime) {
    Serial.println(F("Updating EEPROM."));

    // update the patterns

    // update the servo config.
    EEPROM.put(EEPROMServo,servos);

    // update the servo counts.
    EEPROM.put(EEPROMServoCount,servoCount);
    
    EEPromTimeStamp = millis();
    if(DEBUG) {
      Serial.print(F("Free Ram: "));
      Serial.println(freeRam());
    }
  }

  
}
