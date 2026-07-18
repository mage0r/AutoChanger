/*
 * This file contains all the functions relevant to servo operations
 */

 void setup_servos() {
  if(DEBUG)
    Serial.println(F("Configuring Servo."));
    
  pwm.begin();
  
  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates

  load_servos(SPIFFS, "/servos.txt");

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

void load_servos(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Servos: "));
  Serial.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      Serial.println(F(" - failed to open file for reading"));
      Serial.println(F("Creating Default servos."));
      default_servos();
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
        if(counter2 == 0) // it's the first number, which is the number of operations for each servo.
          servoCount[counter2] = counter3;
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

  
  Serial.println(F("Servo Config: "));
  for(int x = 0; x < 4; x++) {
    Serial.print(F("  "));
    for(int y = 0; y < 3; y++) {
      Serial.print(servos[x][y]);
      Serial.print(F(":"));
    }
    Serial.println();
  }
  

  Serial.println(F("Servo Load Complete."));
}

void default_servos(){
    for(int x = 0; x < 4; x++) {
        servos[x][0] = 150;
        servos[x][1] = 520;
        servos[x][2] = 600;
    }

    save_servos(SPIFFS, "/servos.txt");
}

void save_servos(fs::FS &fs, const char * path) {
  Serial.print(F("Saving Servo Data: "));
  Serial.print(path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
      Serial.println(F(" - failed to open file for writing"));
      return;
  } else {
    Serial.println(F(" - Success!"));
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


