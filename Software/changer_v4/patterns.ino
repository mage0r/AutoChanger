/* Define all the pattern related functions
 *  
 */

void setup_patterns() {
  load_patterns(SPIFFS, "/patterns2.txt");

  // set our active pattern.  We may logically need to do this every time
  // we load_patterns, but just in case.
  set_active(0);
}

// This temporary pattern is used to contain only the 7 characters
// the display can handle.
// Only used for the main page.
void build_temp_pattern() {

  if(activePatternLength > 0) {
    // Array is not empty.
    
    // past pattern
    for (int i=0;i<6;i++) {
      displayPattern[i] = displayPattern[i+1];
    }

    // working out the last entry in the sequence.
    // turns out this is a pain in the arse.
    servonum_temp = servonum;
    for (int i = 4; i < 7; i++) {
      servonum_temp++;
      if(servonum_temp == activePatternLength)
        servonum_temp = 0;
    }

    displayPattern[6] = activePattern[servonum_temp]+1;
    
  }

  for(int i=0;i<7;i++) {
    Serial.print(displayPattern[i]);
    Serial.print(",");
  }
  Serial.println();
}

// Wipe out the current pattern.
void wipe_pattern(byte button) {
  pattern2[button] = "";
}

// This is triggered on a reboot.  maybe switch it to a menu item?
void RestoreDefault(byte button) {
  // Restore the default patterns!

  String defaultPattern[4] = {
    "01",
    "012",
    "0123",
    ""
  };

  for(int i = 0; i < 4; i++) {
    pattern2[button] = defaultPattern[button];
  }

}

// create the array for the active partition.
// Strings work great for storing/copying to and from spiffs
// but they're terrible for the active array where we're always dealing with ints.
void set_active(int switchPattern) {
  // we have an existing array so need to free it.
  if(activePattern != 0) {
    free(activePattern);
  }

  activePatternLength = pattern2[switchPattern].length();

  activePattern = (int*) ps_calloc(activePatternLength, sizeof(int));

  for(int i=0;i<activePatternLength; i++){
    // one day, someone will read this and just freak out.
    activePattern[i] = pattern2[switchPattern].substring(i,i+1).toInt();
  }

}

void load_patterns(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Patterns: "));
  Serial.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      Serial.println(F("- failed to open file for reading"));
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
  
  while(file.available()){

      char temp = file.read();

      //Serial.print((char)temp);

      if(temp == '\n') {
        // run an interpretation.
        counter1++;
      } else if (temp == '\r') {
        // skip carriage return
      } else {
        // append to the pattern string.
        // simple translation from a byte to a character.
        // lets just make sure we've only got valid numbers here.
        if(temp-48 >= 0 && temp-48 < 4)
          pattern2[counter1] += temp-48;
      }

  }

  file.close();

  /*
  Serial.println(F("Patterns: "));
  for(int x = 0; x < 4; x++) {
    Serial.println(pattern2[x]);
  }
  */

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
    Serial.println(F(" - Success!"));
  }

  // lets go simple.
  for(int x = 0; x < 4; x++) {
    file.println(pattern2[x]);
  }

  file.close();

}
