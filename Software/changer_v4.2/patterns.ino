/* Define all the pattern related functions
 *  
 */

void setup_patterns() {
  load_patterns(SPIFFS, "/patterns.txt");
}

// Validates and applies a new step sequence for pattern n (0-3): checks length and
// that every character is a logical digit 1-4, converts those to the stored 0-3
// array index, saves to /patterns.txt, and re-syncs the physical arm if n is the
// currently active pattern. Returns an empty string on success, or a short
// human-readable reason on failure. Shared by the web UI's /setpattern
// (webserver.ino) and the serial/AYAB P<pattern> command (commands.ino), so this
// validation lives in exactly one place rather than being duplicated between them.
String setPatternSteps(int n, String steps) {
  if(n < 0 || n > 3) {
    return "n must be 0-3";
  }
  if(steps.length() > MAX_PATTERN_LENGTH) {
    return "sequence too long";
  }
  for(unsigned int i = 0; i < steps.length(); i++) {
    if(steps[i] < '1' || steps[i] > '4') {
      return "sequence must only contain digits 1-4";
    }
  }

  // convert the logical 1-4 callers use to the stored 0-3 array index.
  for(unsigned int i = 0; i < steps.length(); i++) {
    steps[i] = steps[i] - 1;
  }

  steps.toCharArray(patterns[n].steps, MAX_PATTERN_LENGTH+1);
  patterns[n].length = steps.length();
  save_patterns(SPIFFS, "/patterns.txt");

  if(n == currentPattern) {
    // re-sync the physical arm to match the freshly edited sequence.
    for (int x = 0; x < 4; x++) {
      moveServo(x, 0);
    }
    if(patterns[currentPattern].length) {
      servonum = 1;
      moveServo(patterns[currentPattern].steps[0] - '0', 1);
    } else {
      servonum = -1;
    }
  }

  return ""; // success
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
    weblog.print(F("Default Pattern restored for Pattern "));
    weblog.print(button);
    weblog.println(".");

    weblog.println(F("Patterns: "));
    for(int x = 0; x < 4; x++) {
      weblog.print(F("  "));
      weblog.println(patterns[x].steps);
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
  weblog.print(F("Loading Patterns: "));
  weblog.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      weblog.println(F(" - failed to open file for reading"));
      weblog.println(F("Creating Default Patterns."));
      RestoreDefault(0);
      RestoreDefault(1);
      RestoreDefault(2);
      RestoreDefault(3);
      save_patterns(SPIFFS, path);
      return;
  } else {
    weblog.println(F(" - Success!"));
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

  weblog.println(F("Pattern Load Complete."));
}

void save_patterns(fs::FS &fs, const char * path) {
  weblog.print(F("Saving Pattern Data: "));
  weblog.print(path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
      weblog.println(F("- failed to open file for writing"));
      return;
  } else {
    weblog.print(F(" - File Opened"));
  }

  for(int x = 0; x < 4; x++) {
    file.println(patterns[x].steps);
  }

  file.close();

  weblog.println(F(" - Success!"));

}