/*************************************************** 

  This sketch is intended to set the precise positions of the servos
  for the Autochanger, and save this configuration in to the eeprom.

 ****************************************************/

#include <Wire.h>
//#include <Servo.h>
#include <EEPROM.h>
//#include <Bounce2.h>
#include <Adafruit_PWMServoDriver.h>

// The colour changer only uses 4 servos.
// closed, open, eject
int servos[4][3] = {
  {100,420,600},
  {100,420,600},
  {100,420,600},
  {100,420,600},
};

unsigned int servoCount[4] = {0,0,0,0};

// don't forget, our servo's start at 0!
byte pattern[4][21] = {
  {2,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {3,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {4,0,1,2,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

//Servo myservo[4];

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// our servo # counter
uint8_t servonum = 0;

// EEPROM Addresses
const int EEPROMServoCount = 10;
const int EEPROMServo = 50;
const int EEPROMPattern = 100;

void setup() {
  Serial.begin(115200);

  pwm.begin();

  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates
  

  for (int x = 0; x < 4; x++) {
      //myservo[x].attach(servos[x][0]);
      //myservo[x].write(servos[x][1]);
      Serial.print("Servo ");
      Serial.print(x);
      Serial.print(": Position ");
      pwm.setPWM(x, 0, servos[x][0]);
      Serial.println(servos[x][0]);
      delay(600);
      pwm.setPWM(x, 0, 0);
      //myservo[x].detach();
  }

  EEPROM.begin(512);

  writeEEPROM();
  
}

void loop() {
  
  for (int x = 0; x < 4; x++) {
      //myservo[x].attach(servos[x][0]);
      //myservo[x].write(servos[x][1]);
      Serial.print("Servo ");
      Serial.print(x);
      Serial.print(": Position ");
      pwm.setPWM(x, 0, servos[x][1]);
      Serial.println(servos[x][1]);
      delay(600);
      pwm.setPWM(x, 0, servos[x][0]);
      Serial.println(servos[x][0]);
      delay(600);
      pwm.setPWM(x, 0, 0);
      //myservo[x].detach();
  }
  
}

void writeEEPROM() {

  // Write the servos
  Serial.println("Servo configs");
  for(int i = 0; i< 4; i++) {
    for(int j = 0; j < 3; j++) {
      Serial.print(servos[i][j]);
      Serial.print(":");
    }
    Serial.println();
  }

  EEPROM.put(EEPROMServo,servos);

  //Write the Patterns
  Serial.println("Patterns");
  for(int i = 0; i< 4; i++) {
    for(int j = 0; j < 21; j++) {
      Serial.print(pattern[i][j]);
      Serial.print(":");
    }
    Serial.println();
  }
  
  EEPROM.put(EEPROMPattern,pattern);

  // this resets the servo counts.
  // Don't use it.
  EEPROM.put(EEPROMServoCount,servoCount);

  EEPROM.commit();
  
}
