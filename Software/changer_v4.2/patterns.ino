/* Define all the pattern related functions
 *  
 */

void setup_patterns() {
  load_patterns(SPIFFS, "/patterns.txt");
}

// This temporary pattern is used to contain only the 7 characters
// the display can handle.
// Only used for the main page.
void build_temp_pattern() {

  // clear our array.
  for(int i = 0; i<DISPLAY_WINDOW; i++) {
    displayPattern[i] = 0;
  }
  
  if(patterns[currentPattern].length) {
    // Array is not empty.
    servonum_temp = servonum;

    for (int i = 2; i >= 0; i--) {
      servonum_temp--;
      if(servonum_temp == 0)
        servonum_temp = patterns[currentPattern].length;
      displayPattern[i] = (patterns[currentPattern].steps[servonum_temp-1] - '0') + 1;
    }
    
    servonum_temp = servonum;
    for (int i = 4; i < 7; i++) {
      servonum_temp++;
      if(servonum_temp > patterns[currentPattern].length)
        servonum_temp = 1;
      displayPattern[i] = (patterns[currentPattern].steps[servonum_temp-1] - '0') + 1;
    }
  }
}

// Wipe out the current pattern.
void wipe_pattern(byte button) {
  patterns[button].steps[0] = '\0';
  patterns[button].length = 0;
}

// This is triggered on a reboot.  maybe switch it to a menu item?
void RestoreDefault(byte button) {
  // Restore the default patterns!

  static const char* defaultPattern[4] = {
    "01",
    "012",
    "0123",
    "",
  };

  strncpy(patterns[button].steps, defaultPattern[button], MAX_PATTERN_LENGTH);
  patterns[button].steps[MAX_PATTERN_LENGTH] = '\0';
  patterns[button].length = strlen(patterns[button].steps);

  // Write them back to our ram
  //save_patterns(SPIFFS, "/patterns2.txt");

  if(DEBUG) {
    Serial.print(F("Default Pattern restored for Pattern "));
    Serial.print(button);
    Serial.println(".");

    Serial.println(F("Patterns: "));
    for(int x = 0; x < 4; x++) {
      Serial.print(F("  "));
      Serial.println(patterns[x].steps);
    }
  }
}

// Parses one saved pattern line into patterns[index]. Tells the format apart by
// whether a comma shows up at all:
//  - new format: the line IS the steps string already, e.g. "0123"
//  - old format: "count,val,val,...,val" (padded with trailing zeros past count) -
//    the count tells us how many of the comma-separated values are real steps,
//    and each numeric value converts straight to its digit character.
void parse_pattern_line(String &line, byte index) {
  if(line.indexOf(',') == -1) {
    line.toCharArray(patterns[index].steps, MAX_PATTERN_LENGTH+1);
    patterns[index].length = strlen(patterns[index].steps);
    return;
  }

  int commaIndex = line.indexOf(',');
  int count = line.substring(0, commaIndex).toInt();
  if(count > MAX_PATTERN_LENGTH)
    count = MAX_PATTERN_LENGTH;

  String rest = line.substring(commaIndex+1);
  byte written = 0;

  while(rest.length() && written < count) {
    int nextComma = rest.indexOf(',');
    String valStr = (nextComma == -1) ? rest : rest.substring(0, nextComma);
    patterns[index].steps[written] = '0' + valStr.toInt();
    written++;
    if(nextComma == -1)
      break;
    rest = rest.substring(nextComma+1);
  }

  patterns[index].steps[written] = '\0';
  patterns[index].length = written;
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

  byte patternIndex = 0;
  String line = "";

  while(file.available() && patternIndex < 4){

      char temp = file.read();

      if(temp == '\n') {
        parse_pattern_line(line, patternIndex);
        line = "";
        patternIndex++;
      } else if (temp == '\r') {
        // skip carriage return
      } else {
        line += temp;
      }

  }

  // handle a final line with no trailing newline.
  if(line.length() && patternIndex < 4) {
    parse_pattern_line(line, patternIndex);
    patternIndex++;
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
    file.println(patterns[x].steps);
  }

  file.close();

  Serial.println(F(" - Success!"));

}