/*
 * This file contains all the functions relevant to servo operations
 */

 void setup_servos() {
  if(DEBUG)
    weblog.println(F("Configuring Servo."));
    
  pwm.begin();
  
  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates

  load_servos(SPIFFS, "/servos.txt");

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
void moveServo(int x, int y) {

  armUp[x] = (y == 1); // see the comment on armUp[] in changer_v4_2.ino

  if(pwm.getPWM(x) != servos[x][y]) {
  // only update servoCount if the read position is different to where we're trying to get to.
    servoCount[x]++;
    
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
  value = constrain(value, 0, 4095);
  pwm.setPWM(arm, 0, value);
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
  
  if(servoTimeout > 0 && servoTimeout + 400 < millis()) {
    for (int x = 0; x < maxServo; x++) {
      pwm.setPWM(x, 0, 0);
      if(DEBUG) {
        weblog.print(F("Detatch Servo: "));
        weblog.println(x);
      }
    }
    servoTimeout = 0;
  }
  
}

void switchRods() {

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

  byte counter1 = 0; // the number of lines
  byte counter2 = 0;
  int counter3 = 0;
  
  while(file.available()){

      char temp = file.read();

      //weblog.print((char)temp);

      if(temp == ',') {
        if(counter2 == 0) // it's the first number, which is the number of operations for each servo.
          servoCount[counter1] = counter3;
        else
          servos[counter1][counter2-1] = counter3;
        counter2++;
        counter3 = 0;
      } else if(temp == '\n') {
        // run an interpretation.
        servos[counter1][counter2-1] = counter3;
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

      //display_print(F("."));
  }

  // did we forget to add the last value?
  // wtf is this?
  if(counter3 != 0) {
    servos[counter1][counter2-1] = counter3;
  }

  file.close();

  
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

void default_servos(){
    for(int x = 0; x < 4; x++) {
        servos[x][0] = 150; // 160 // 170
        servos[x][1] = 520; //470 //450
        servos[x][2] = 650;
    }

    servosDefaulted = true;

    // No saved position data - push every arm out to the eject position rather than
    // guessing at a low/high pattern position, and flag it so setup_servos() doesn't
    // immediately override this with pattern-based positioning.
    for (int x = 0; x < 4; x++) {
      moveServo(x, 2);
    }

    save_servos(SPIFFS, "/servos.txt");
}

void save_servos(fs::FS &fs, const char * path) {
  weblog.print(F("Saving Servo Data: "));
  weblog.print(path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
      weblog.println(F(" - failed to open file for writing"));
      return;
  } else {
    weblog.println(F(" - Success!"));
  }

  // lets go simple.
  for(int x = 0; x < 4; x++) {
      file.print(servoCount[x]);
      for(int y = 0; y < 3; y++) {
        file.print(F(","));
        file.print(servos[x][y]);
      }
      file.println();
  }

  file.close();

}