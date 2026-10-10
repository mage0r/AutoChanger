/* Define all the pattern related functions
 *  
 */

void setup_patterns() {
  load_patterns(LittleFS, "/patterns.txt");
}

// Validates and applies a new step sequence for pattern n (0-3): checks length and
// that every character is a logical digit 1-4, converts those to the stored 0-3
// array index, saves to /patterns.txt, and re-syncs the physical arm if n is the
// currently active pattern. Returns an empty string on success, or a short
// human-readable reason on failure. Shared by the web UI's /setpattern
// (webserver.ino) and the serial/AYAB P<pattern> command (commands.ino), so this
// validation lives in exactly one place rather than being duplicated between them.
// Checks a pattern edit without changing anything - "" if it's valid, else the reason.
// steps are the logical 1-4 users see.
String validatePatternSteps(int n, const String &steps) {
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
  return "";
}

// Validates and applies a pattern edit (serial P command). Returns "" or the reason.
String setPatternSteps(int n, String steps) {
  String error = validatePatternSteps(n, steps);
  if(error.length())
    return error;
  applyPatternSteps(n, steps);
  return "";
}

// Applies an already-validated pattern edit, saves it, and re-syncs the arms if it's
// the active pattern. Runs on loop() - the web UI queues it via WebActions.
void applyPatternSteps(int n, String steps) {
  // convert the logical 1-4 callers use to the stored 0-3 array index.
  for(unsigned int i = 0; i < steps.length(); i++) {
    steps[i] = steps[i] - 1;
  }

  steps.toCharArray(patterns[n].steps, MAX_PATTERN_LENGTH+1);
  patterns[n].length = steps.length();
  save_patterns(LittleFS, "/patterns.txt");

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
}

// This temporary pattern is used to contain only the 7 characters
// the display can handle.
// Only used for the main page.
// Puts servonum back on step 1 if it's outside the current pattern (a pattern reset
// or shortened while it pointed past the new end). Only meaningful for non-empty
// patterns - an empty one uses servonum as the raised arm, or -1.
void normalize_servonum() {
  int len = patterns[currentPattern].length;
  if(len && (servonum < 1 || servonum > len))
    servonum = 1;
}

void build_temp_pattern() {

  // clear our array.
  for(int i = 0; i<DISPLAY_WINDOW; i++) {
    displayPattern[i] = 0;
  }
  
  if(patterns[currentPattern].length) {
    // Array is not empty.
    normalize_servonum();
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
  //save_patterns(LittleFS, "/patterns2.txt");

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

// Logs why a saved pattern line was rejected. Always printed, not just under DEBUG -
// it means data on the device is wrong and a pattern has been cleared.
void pattern_load_error(byte index, const String &reason) {
  weblog.print(F("Pattern "));
  weblog.print(index+1);
  weblog.print(F(" in patterns.txt is invalid ("));
  weblog.print(reason);
  weblog.println(F(") - cleared. Arms are stored as 0-3 in the file (shown as 1-4 on screen)."));
}

// Parses one saved pattern line into patterns[index]. Tells the format apart by
// whether a comma shows up at all:
//  - new format: the line IS the steps string already, e.g. "0123"
//  - old format: "count,val,val,...,val" (padded with trailing zeros past count) -
//    the count tells us how many of the comma-separated values are real steps.
//
// Every step must be an arm index 0-3: everything downstream does
// `steps[i] - '0'` straight into 4-element arrays (servos[], armUp[], the PCA9685
// channel), so one bad character means out-of-bounds reads/writes. The line is
// parsed into a scratch buffer first and only copied in if the whole thing is
// valid - a bad line leaves the pattern empty (manual/loading mode, which never
// indexes steps[]) rather than half-loaded. Returns false if the line was rejected.
bool parse_pattern_line(String &line, byte index) {
  char buf[MAX_PATTERN_LENGTH+1];
  byte written = 0;

  line.trim(); // tolerate stray spaces/tabs from hand-editing in the web editor

  if(line.indexOf(',') == -1) {
    // new format
    if(line.length() > MAX_PATTERN_LENGTH) {
      wipe_pattern(index);
      pattern_load_error(index, "longer than " + String(MAX_PATTERN_LENGTH) + " steps");
      return false;
    }
    for(unsigned int i = 0; i < line.length(); i++) {
      char c = line[i];
      if(c < '0' || c > '3') {
        wipe_pattern(index);
        pattern_load_error(index, "step " + String(i+1) + " is '" + String(c) + "'");
        return false;
      }
      buf[written++] = c;
    }
  } else {
    // old format
    int commaIndex = line.indexOf(',');
    int count = line.substring(0, commaIndex).toInt();
    if(count < 0)
      count = 0;
    if(count > MAX_PATTERN_LENGTH)
      count = MAX_PATTERN_LENGTH;

    String rest = line.substring(commaIndex+1);

    while(rest.length() && written < count) {
      int nextComma = rest.indexOf(',');
      String valStr = (nextComma == -1) ? rest : rest.substring(0, nextComma);
      valStr.trim();
      // exactly one digit 0-3 - toInt() alone would turn "x" or "" into a valid-looking 0
      if(valStr.length() != 1 || valStr[0] < '0' || valStr[0] > '3') {
        wipe_pattern(index);
        pattern_load_error(index, "step " + String(written+1) + " is '" + valStr + "'");
        return false;
      }
      buf[written++] = valStr[0];
      if(nextComma == -1)
        break;
      rest = rest.substring(nextComma+1);
    }
  }

  buf[written] = '\0';
  memcpy(patterns[index].steps, buf, written+1);
  patterns[index].length = written;
  return true;
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
      save_patterns(LittleFS, path);
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

// Written via SafeFile (safefile.h): a power cut mid-save leaves the previous file.
// One line per pattern; an empty pattern is a blank line.
void save_patterns(fs::FS &fs, const char * path) {
  String out = "";
  for(int x = 0; x < 4; x++) {
    out += patterns[x].steps;
    out += "\n";
  }

  weblog.print(F("Saving Pattern Data: "));
  weblog.print(path);
  weblog.println(SafeFile::write(fs, path, out) ? F(" - Success!") : F(" - FAILED"));
}