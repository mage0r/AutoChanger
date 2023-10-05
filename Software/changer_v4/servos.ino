/*
 * This file contains all the functions relevant to servo operations
 */

void setup_servos() {
  Serial.println(F("Configuring Servo."));
    
  pwm.begin();
  
  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates

  load_servos(SPIFFS, "/servos.txt");

  // set inital position
  for (int x = 0; x < maxServo; x++) {
    if(pattern[currentPattern][0] && pattern[currentPattern][1] == x)
      moveServo(x,1);
    else
      moveServo(x,0);
  }
  
 }

// we don't really need to hold the servos
// this function turns the servo on, moves it, then switches it off.
// x is the servo number.
// y is the position to move to in the "servos" array
void moveServo(int x, int y) {

  if(pwm.getPWM(x) != servos[x][y]) {
  // only update servoCount if the read position is different to where we're trying to get to.
    servoCount[x]++;
    
     pwm.setPWM(x, 0, servos[x][y]);
    if(DEBUG) {
      Serial.print(F("Moving Servo: "));
      Serial.print(x);
      if(y == 0)
        Serial.println(F(" down."));
      else
        Serial.println(F(" up."));
    }
  }
  
  servoTimeout = millis();
}

// This sets the servo position without any checks at all.
// useful for tuning the arm positions.
void setServo(int servo, int position) {
  pwm.setPWM(servo, 0, servos[servo][position]);
}

// after a given time, detach all servos.
void detachServo() {
  
  if(servoTimeout > 0 && servoTimeout + 400 < millis()) {
    for (int x = 0; x < maxServo; x++) {
      pwm.setPWM(x, 0, 0);
    }
    servoTimeout = 0;
  }
  
}

void switchRods() {

  moveServo(pattern[currentPattern][servonum],0);

  servonum ++;
  
  if (servonum > pattern[currentPattern][0]) {
    if(DEBUG)
      Serial.println(F("Reset to start"));
    servonum = 1;
  }

  moveServo(pattern[currentPattern][servonum],1);

}

void load_servos(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Servos: "));
  Serial.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      Serial.println(F("- failed to open file for reading"));
      Serial.println(F("Creating Default servos."));
      //save_patterns(SPIFFS, path);
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
        servos[counter1][counter2] = counter3;
        counter2++;
        counter3 = 0;
      } else if(temp == '\n') {
        // run an interpretation.
        servos[counter1][counter2] = counter3;
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
  if(counter3 != 0) {
    servos[counter1][counter2] = counter3;
  }

  file.close();

  /*
  Serial.println(F("Servo Config: "));
  for(int x = 0; x < 4; x++) {
    Serial.print(F("  "));
    for(int y = 0; y < 3; y++) {
      Serial.print(servos[x][y]);
      Serial.print(F(":"));
    }
    Serial.println();
  }
  */

  Serial.println(F("Servo Load Complete."));
}
