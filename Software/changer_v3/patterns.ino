/* Define all the pattern related functions
 *  
 */

// This temporary pattern is used to contain only the 7 characters
// the display can handle.
// Only used for the main page.
void build_temp_pattern() {

  // clear our array.
  for(int i = 0; i<20; i++) {
    displayPattern[i] = 0;
  }
  
  if(pattern[currentPattern][0]) {
    // Array is not empty.
    servonum_temp = servonum;

    for (int i = 2; i >= 0; i--) {
      servonum_temp--;
      if(servonum_temp == 0)
        servonum_temp = pattern[currentPattern][0];
      displayPattern[i] = pattern[currentPattern][servonum_temp]+1;
    }
    
    servonum_temp = servonum;
    for (int i = 4; i < 7; i++) {
      servonum_temp++;
      if(servonum_temp > pattern[currentPattern][0])
        servonum_temp = 1;
      displayPattern[i] = pattern[currentPattern][servonum_temp]+1;
    }
  }
}

// Wipe out the current pattern.
void wipe_pattern(byte button) {
  
  for(int i = 0; i < 21; i++) {
    pattern[button][i] = 0;
  }
}

// This is triggered on a reboot.  maybe switch it to a menu item?
void RestoreDefault(byte button) {
  // Restore the default patterns!

  static byte defaultPattern[4][21] = {
    {2,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {3,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {4,0,1,2,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  };

  for(int i = 0; i < 21; i++) {
    pattern[button][i] = defaultPattern[button][i];
  }

  // Write them back to our ram
  EEPROM.put(EEPROMPattern,pattern);
  EEPROM.commit();

  if(DEBUG) {
    Serial.print(F("Default Pattern restored for Pattern "));
    Serial.print(button);
    Serial.println(".");

    Serial.println(F("Patterns: "));
    for(int x = 0; x < 4; x++) {
      Serial.print(F("  "));
      for(int y = 0; y < 21; y++) {
        Serial.print(pattern[x][y]);
        Serial.print(F(":"));
      }
      Serial.println();
    }
  }
}
