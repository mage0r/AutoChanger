/*
 * This file contains all the functions relevant to servo operations
 */

 void setup_servos() {
  if(DEBUG)
    Serial.println(F("Configuring Servo."));
    
  pwm.begin();
  
  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates

  
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

// after a given time, detach all servos.
void detachServo() {
  
  if(servoTimeout > 0 && servoTimeout + 400 < millis()) {
    for (int x = 0; x < maxServo; x++) {
      pwm.setPWM(x, 0, 0);
      if(DEBUG) {
        Serial.print(F("Detatch Servo: "));
        Serial.println(x);
      }
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
