/* Define all the pattern related functions
 *  
 */

void setup_patterns() {
  load_patterns(SPIFFS, "/patterns2.txt");
}

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
  //save_patterns(SPIFFS, "/patterns2.txt");

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

void load_patterns(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Patterns: "));
  Serial.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      Serial.println(F(" - failed to open file for reading"));
      Serial.println(F("Creating Default Patterns."));
      RestoreDefault(0);
      RestoreDefault(1);
      RestoreDefault(2);
      RestoreDefault(3);
      save_patterns(SPIFFS, path);
      return;
  } else {
    Serial.println(F(" - Success!"));
  }

  byte counter1 = 0;
  byte counter2 = 0;
  int counter3 = 0;
  
  while(file.available()){

      char temp = file.read();

      //Serial.print((char)temp);

      if(temp == ',') {
        pattern[counter1][counter2] = counter3;
        counter2++;
        counter3 = 0;
      } else if(temp == '\n') {
        // run an interpretation.
        pattern[counter1][counter2] = counter3;
        counter1++;
        counter2 = 0;
        counter3 = 0;
      } else if (temp == '\r') {
        // skip carriage return
      } else {
        // append to the variable.
        counter3 = counter3 * 10;
        counter3 = counter3 + temp-48;
      }

  }

  file.close();

  Serial.println(F("Pattern Load Complete."));
}

void save_patterns(fs::FS &fs, const char * path) {
  Serial.print(F("Saving Pattern Data: "));
  Serial.print(path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
      Serial.println(F("- failed to open file for writing"));
      return;
  } else {
    Serial.print(F(" - File Opened"));
  }

  for(int x = 0; x < 4; x++) {
    file.print(pattern[x][0]);
    for(int y = 1; y < 21; y++) {
      file.print(F(","));
      file.print(pattern[x][y]);
    }
    file.println();
  }

  file.close();

  Serial.println(F(" - Success!"));

}
