/*
 * This file contains all the functions relevant to servo operations
 */

 void setup_servos() {
  if(DEBUG)
    weblog.println(F("Configuring Servo."));
    
  pwm.begin();
  
  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates

  load_servos(LittleFS, "/servos.txt");

  if(servosDefaulted) {
    // /servos.txt wasn't found - arms are already out at the eject position (see default_servos()).
    // Open straight into the arm-adjust page on EJECT so it can be checked/tuned right away.
    menu_page = 2;
    menu_position = 4;
  } else {
    for (int x = 0; x < maxServo; x++) {
      if(patterns[currentPattern].length && (patterns[currentPattern].steps[0] - '0') == x)
        moveServo(x,1);
      else
        moveServo(x,0);
    }
  }
  
 }

// we don't really need to hold the servos
// this function turns the servo on, moves it, then switches it off.
// x is the servo number.
// y is the position to move to in the "servos" array
// Last PWM value commanded on each channel, or -1 for unknown (at boot). Tracked here
// rather than read back with pwm.getPWM(), which returns the channel's ON count (always
// 0 here), so every call used to look like a move and inflate servoCount[]. Detaching
// doesn't reset it: an unpowered servo stays where it was put.
int lastPwm[4] = {-1, -1, -1, -1};

void moveServo(int x, int y) {
  if(x < 0 || x > 3 || y < 0 || y > 2)
    return; // a bad arm index would write outside every per-arm array

  armUp[x] = (y == 1); // see the comment on armUp[] in arm.h

  if(lastPwm[x] != servos[x][y]) {
  // only count it, and power the servo, if it's actually going somewhere new.
    servoCount[x]++;
    lastPwm[x] = servos[x][y];
    
     pwm.setPWM(x, 0, servos[x][y]);
    if(DEBUG) {
      weblog.print(F("Moving Servo: "));
      weblog.print(x);
      if(y == 0)
        weblog.println(F(" down."));
      else
        weblog.println(F(" up."));
    }

    // Only reset the timeout on an actual move. If we reset it unconditionally, a caller that
    // polls moveServo() every loop (e.g. armAdjustMode(), to keep the arm synced to a field)
    // would keep the servo powered indefinitely even while sitting idle, since detachServo()
    // never gets a 400ms gap with nothing happening to detach into.
    servoTimeout = millis();
  }
}

// Moves an arm directly to an arbitrary PWM value, bypassing servos[][] entirely -
// used to preview a Low/High/Eject value from the web UI's Arm Positions tab before
// it's been saved, so moveServo()'s "look up servos[arm][field]" wouldn't have the
// right value to use yet. Clamped the same as /servo_save (see webserver.ino) and
// for the same reason - this takes a raw typed value, not one that's already been
// validated.
void previewServo(int arm, int value) {
  if(arm < 0 || arm > 3)
    return;
  value = constrain(value, 0, 4095);
  pwm.setPWM(arm, 0, value);
  lastPwm[arm] = value; // so moving back to a stored position afterwards is a real move
  servoCount[arm]++;
  servoTimeout = millis();
  if(DEBUG) {
    weblog.print(F("Previewing Servo: "));
    weblog.print(arm);
    weblog.print(F(" at "));
    weblog.println(value);
  }
}

// after a given time, detach all servos.
void detachServo() {
  
  if(servoTimeout > 0 && millis() - servoTimeout > 400) {
    for (int x = 0; x < maxServo; x++) {
      pwm.setPWM(x, 0, 0);
      if(DEBUG) {
        weblog.print(F("Detach Servo: "));
        weblog.println(x);
      }
    }
    servoTimeout = 0;
  }
  
}

void switchRods() {
  if(!patterns[currentPattern].length)
    return;
  normalize_servonum();

  moveServo(patterns[currentPattern].steps[servonum-1] - '0', 0);

  servonum ++;
  
  if (servonum > patterns[currentPattern].length) {
    if(DEBUG)
      weblog.println(F("Reset to start"));
    servonum = 1;
  }

  moveServo(patterns[currentPattern].steps[servonum-1] - '0', 1);

}

// Mirrors switchRods() exactly, just stepping backward instead of forward - lowers the
// current arm, steps servonum back one (wrapping to the end instead of the start), then
// raises whatever arm is now current. Used by the web UI's Back button (see /back in
// webserver.ino); the normal forward progression is still driven by the arm sensor via
// operateArm(), this only ever runs on an explicit request to step back.
void switchRodsBack() {
  if(!patterns[currentPattern].length)
    return;
  normalize_servonum();

  moveServo(patterns[currentPattern].steps[servonum-1] - '0', 0);

  servonum --;

  if (servonum < 1) {
    if(DEBUG)
      weblog.println(F("Wrapped to end"));
    servonum = patterns[currentPattern].length;
  }

  moveServo(patterns[currentPattern].steps[servonum-1] - '0', 1);

}

void load_servos(fs::FS &fs, const char * path) {
  weblog.print(F("Loading Servos: "));
  weblog.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      weblog.println(F(" - failed to open file for reading"));
      weblog.println(F("Creating Default servos."));
      default_servos();
      return;
  } else {
    weblog.println(F(" - Success!"));
  }

  // Start every arm from defaults, so an arm whose line is missing or bad still
  // ends up with sane positions rather than 0 (which the PCA9685 treats as "off").
  for(int x = 0; x < 4; x++) {
    set_default_servo_values(x);
    servoCount[x] = 0;
  }

  byte arm = 0;   // which arm the next non-blank line belongs to
  byte valid = 0; // how many arms loaded cleanly
  String line = "";

  // One line per arm: count,low,high,eject. Blank lines are skipped (an editor
  // adding a trailing newline is harmless); lines past the 4th are ignored rather
  // than written past the end of servos[].
  while(file.available() || line.length()) {
    char c = file.available() ? file.read() : '\n'; // flush a final unterminated line
    if(c == '\r')
      continue;
    if(c != '\n') {
      line += c;
      continue;
    }

    line.trim();
    if(line.length()) {
      if(arm >= 4) {
        weblog.println(F("servos.txt has more than 4 lines - extras ignored."));
      } else if(parse_servo_line(line, arm)) {
        valid++;
      } else {
        weblog.print(F("servos.txt line for arm "));
        weblog.print(arm+1);
        weblog.print(F(" is invalid (\""));
        weblog.print(line);
        weblog.println(F("\") - using default positions for that arm."));
      }
      arm++;
    }
    line = "";
  }

  file.close();

  if(valid == 0) {
    // Empty or completely unreadable - most likely a write cut short by a power
    // loss. Treat it exactly like a missing file: defaults, arms out to EJECT,
    // boot into the arm-adjust page so it gets re-checked.
    weblog.println(F("servos.txt has no usable data - recreating defaults."));
    default_servos();
    return;
  }

  
  weblog.println(F("Servo Config: "));
  for(int x = 0; x < 4; x++) {
    weblog.print(F("  "));
    for(int y = 0; y < 3; y++) {
      weblog.print(servos[x][y]);
      weblog.print(F(":"));
    }
    weblog.println();
  }
  

  weblog.println(F("Servo Load Complete."));
}

// Just the default LOW/HIGH/EJECT values for one arm - no movement, no save.
// Used by load_servos() for arms with a missing/bad line, and by default_servos().
void set_default_servo_values(int arm) {
    servos[arm][0] = 150; // 160 // 170
    servos[arm][1] = 520; //470 //450
    servos[arm][2] = 650;
}

// Parses one field: digits only, no sign, not empty. toInt() alone would accept
// "", "abc" or "-5" and quietly turn them into something.
bool parse_servo_field(String field, long &out) {
  field.trim();
  if(field.length() == 0 || field.length() > 9)
    return false;
  for(unsigned int i = 0; i < field.length(); i++) {
    if(!isDigit(field[i]))
      return false;
  }
  out = field.toInt();
  return true;
}

// Parses "count,low,high,eject" into servoCount[arm] / servos[arm][]. Only writes
// anything if the whole line is valid. Positions must be 1-4095: the PCA9685's
// 12-bit range, and 0 would leave the servo unpowered rather than positioned.
bool parse_servo_line(String &line, byte arm) {
  long vals[4];
  byte n = 0;
  int start = 0;

  while(true) {
    int comma = line.indexOf(',', start);
    String field = (comma == -1) ? line.substring(start) : line.substring(start, comma);
    if(n >= 4 || !parse_servo_field(field, vals[n]))
      return false;
    n++;
    if(comma == -1)
      break;
    start = comma + 1;
  }

  if(n != 4)
    return false;
  for(int i = 1; i < 4; i++) {
    if(vals[i] < 1 || vals[i] > 4095)
      return false;
  }

  servoCount[arm] = vals[0];
  for(int y = 0; y < 3; y++)
    servos[arm][y] = vals[y+1];
  return true;
}

void default_servos(){
    for(int x = 0; x < 4; x++) {
        set_default_servo_values(x);
    }

    servosDefaulted = true;

    // No saved position data - push every arm out to the eject position rather than
    // guessing at a low/high pattern position, and flag it so setup_servos() doesn't
    // immediately override this with pattern-based positioning.
    for (int x = 0; x < 4; x++) {
      moveServo(x, 2);
    }

    save_servos(LittleFS, "/servos.txt");
}

// Written via SafeFile (safefile.h): a power cut mid-save leaves the previous file.
void save_servos(fs::FS &fs, const char * path) {
  String out = "";
  for(int x = 0; x < 4; x++) {
    out += String(servoCount[x]);
    for(int y = 0; y < 3; y++)
      out += "," + String(servos[x][y]);
    out += "\n";
  }

  weblog.print(F("Saving Servo Data: "));
  weblog.print(path);
  weblog.println(SafeFile::write(fs, path, out) ? F(" - Success!") : F(" - FAILED"));
}