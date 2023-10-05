/*************************************************** 
 *  
 *  
 *  Autochanger - https://github.com/mage0r/AutoChanger
 *  
 *  This code is designed to run on an Arduino Nano 3.0 plugged in to the custom board in the repo.
 *  
 *  The Nano is flashed with a custom bootloader to enable EEPROM saving
 *  
 *  Oh god, please don't keep reading this code.  It is awful.
 *  
 *
 ****************************************************/

#include <Wire.h>
#include <Servo.h>
#include <Bounce2.h>
#include <EEPROM.h>
#include <Encoder.h>
#include <Adafruit_PWMServoDriver.h>

#include <U8g2lib.h>

// initiate the display.
U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);


#define DEBUG 1 // Do we want debug output on serial.
#define BUZZER 0 // Is the buzzer on or off.

// Set our version number.  Don't forget to update when featureset changes
#define PROJECT "AutoChanger"
#define VERSION "V.3.3"

// The colour changer only uses 4 servos.
// content for this array is loaded from eeprom
// first 
int servos[4][3];

unsigned long servoTimeout = 0;

// our servo # counter
uint8_t servonum = 1;
uint8_t maxServo = 4;
uint8_t servonum_temp; // declare this once and hopefully don't overrun my buffers

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// We track how many movements the servo has made.
// because, why not?! :)
unsigned int servoCount[4];
unsigned long EEPromTimeStamp;
unsigned int EEPromUpdateTime = 60000;


const int armPin = 13;
unsigned int armState;             // the current reading from the input pin
unsigned int lastArmState = HIGH;   // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 15;    // the debounce time; increase if the output flickers
byte armCounter = 0;  // We ignore every second pass.

// input buttons
// These are hardcoded
const int buttonPins[4] = {10,9,8,7};
const int pgrmPin = 6;
boolean buttonState[4] = {false,false,false,false};
boolean pgrmState = false;
// Rotary encoder
Encoder myEnc(2, 3);
long oldPosition  = -999;
byte menu_page = 0;
byte menu_position = 0;
byte admin_mode = 0;
int temp_menu_position = 0;

// Debouncing
Bounce debouncer[4] = Bounce();
byte previousButton;
Bounce pgrmDebouncer = Bounce();
unsigned long buttonPressTimeStamp;
boolean triggerPgrm = false;
boolean pgrmMode = false;
unsigned long pgrmTimeStamp = 0;
unsigned long btnTimeStamps[4] = {0,0,0,0};
byte temp_pattern[21] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // temporary pattern.

// Buzzer
const int buzzerPin = 4;

// Array of ints to store our pattern.
byte currentPattern = 0;
byte pattern[4][21];
byte displayPattern[20];

// EEPROM Addresses
const int EEPROMServoCount = 10;
const int EEPROMServo = 50;
const int EEPROMPattern = 100;

unsigned int test_run = 100;

void setup() {
  Serial.begin(115200);

  pwm.begin();
  
  pwm.setPWMFreq(60);  // Analog servos run at ~60 Hz updates
  
  u8g2.begin();
  // Kick off the Display.
  u8g2.setFont(u8g2_font_cu12_tr);
  u8g2.firstPage();
  do {
    u8g2.setCursor(0,12);
    u8g2.print(F(PROJECT));
    u8g2.setCursor(0,30);
    u8g2.print(F(VERSION));
  } while ( u8g2.nextPage() );

  // First, build information
  Serial.print(F(PROJECT));
  Serial.print(F(" "));
  Serial.println(F(VERSION));
  Serial.print(F("Build Date: "));
  Serial.println(F(__DATE__ " " __TIME__));
  Serial.print(F("Free Ram: "));
  Serial.println(freeRam());

  // get our previous settings.
  readEEPROM();

  pinMode(armPin, INPUT);

  for (int x = 0; x < 4; x++) {
    pinMode(buttonPins[x], INPUT);
    debouncer[x].attach(buttonPins[x]);
    debouncer[x].interval(5);
  }

  pinMode(pgrmPin, INPUT);
  pgrmDebouncer.attach(pgrmPin);
  pgrmDebouncer.interval(5);

  pinMode(buzzerPin, OUTPUT);

  

  // reset all the servo positions.
  for (int x = 0; x < maxServo; x++) {
    if(pattern[currentPattern][0] && pattern[currentPattern][1] == x)
      moveServo(x,2);
    else
      moveServo(x,1);
  }
  
  // if the program pin is high, trigger a simulator mode.
  if(!digitalRead(pgrmPin)) {
    menu_page = 2; // show the admin page
    admin_mode = 1;
  } else if(!digitalRead(buttonPins[0])) {
    RestoreDefault(0);
  } else if(!digitalRead(buttonPins[1])) {
    RestoreDefault(1);
  } else if(!digitalRead(buttonPins[2])) {
    RestoreDefault(2);
  } else if(!digitalRead(buttonPins[3])) {
    RestoreDefault(3);
  }

  delay(2000);
  build_temp_pattern();
  
  
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
      if(y == 1)
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

  moveServo(pattern[currentPattern][servonum],1);

  servonum ++;
  
  if (servonum > pattern[currentPattern][0]) {
    if(DEBUG)
      Serial.println(F("Reset to start"));
    servonum = 1;
  }

  moveServo(pattern[currentPattern][servonum],2);

}

// kind of the wrong name for this function
// It handles when we EXIT program mode
void programMode() {
  // write our temp pattern to the array and reset the values to 0.
  // only do this if we actually have a pattern, i.e. more than 1 entry.
  if(temp_pattern[0] > 1) {
    byte temp_number = temp_pattern[0];
    for(int x = 0; x <= temp_number; x++) {
      // debug info
      if(DEBUG) {
        Serial.print(temp_pattern[x]);
        Serial.print(F(","));
      }
      
      // copy
      pattern[currentPattern][x] = temp_pattern[x];

      // reset
      temp_pattern[x] = 0;
    }
    if(DEBUG)
      Serial.println();

    // Update the eeprom.
    if(DEBUG)
      Serial.println(F("Writing new Pattern to memory."));
    EEPROM.put(EEPROMPattern,pattern);
  } else {
    Serial.println(F("No pattern detected.  Not updating."));
  }

  // reset our device to position 1.
  for (int x = 0; x < 4; x++) {
      moveServo(x,1);
  }
  servonum = 1;
  moveServo(pattern[currentPattern][servonum], 2);

}

void loop() {

  int reading = digitalRead(armPin);

  read_encoder();

  // Update our buttons.
  for (int x = 0; x < 4; x++) {
    debouncer[x].update();
  }
  pgrmDebouncer.update();

  // Check if we need to update the arm
  // We only need to do this if our pattern is non-zero
  // If the pattern is 0, it's a manual pattern
  if(pattern[currentPattern][0])
    checkArm(reading);

  // run our button combo section
  checkButtons();

  detachServo();

  build_temp_pattern();
  display_page();

  writeEEPROM();
}

// When we're in admin mode, we can edit individual values.
void changeValues(int adjust) {
  // Terrible switch case of what to do.
  if(menu_position == 0) {
    
  } else if(menu_position == 1){
    // Change the test run value
    test_run = test_run + adjust;
   } else if (menu_position == 3) {
    // Change the eject value
    servos[currentPattern][2] = servos[currentPattern][2] + adjust;
   } else if (menu_position == 5) {
    // Change the High value
    servos[currentPattern][0] = servos[currentPattern][0] + adjust;
   } else if (menu_position == 7) {
    // Change the low value
    servos[currentPattern][1] = servos[currentPattern][1] + adjust;
   }
   
}

// Just pull this out in to it's own function to make this easier to read.
void checkArm(int reading) {

  // If the switch changed, due to noise or pressing:
  if (reading != lastArmState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != armState) {
      armState = reading;

      if (armState == LOW) {
        if(armCounter == 0){
          switchRods();
          armCounter = 1;
          
          // Turn the buzzer on for 1 second
          if(BUZZER)
            tone(buzzerPin, "c", 1000);
        }
        else
          armCounter = 0;
      }
    }
  }

  // save the reading.  Next time through the loop,
  // it'll be the lastArmState:
  lastArmState = reading;
}

void TestRun() {
  // Trigger a testrun if the program button is held down during start.

  Serial.println(F("Entering Test Mode."));
  Serial.println(F("Executing 100 cycles of the servos."));
  
  for (int y = 0; y < test_run; y++) {
    for (int x = 0; x < maxServo; x++) {
      if(x == 0) {
        moveServo(maxServo-1,1);
      } else
        moveServo(x-1,1);

      if( x == 1)
        Serial.println(F("1"));
      else if( x == 2)
        Serial.println(F("2"));
      else if( x == 3)
        Serial.println(F("3"));
        
      moveServo(x,2);
      delay(400);
    }

    writeEEPROM(); // update the eeprom if needed.
  }
}


// This is triggered on a reboot.  maybe switch it to a menu item?
void RestoreDefault(byte button) {
  // Restore the default patterns!

  static byte defaultPattern[4][21] = {
    {2,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {3,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {20,1,1,1,1,2,2,2,2,3,3,3,3,0,0,0,0,0,1,2,3},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  };

  for(int i = 0; i < 21; i++) {
    pattern[button][i] = defaultPattern[button][i];
  }

  // Write them back to our ram
  EEPROM.put(EEPROMPattern,pattern);

  if(DEBUG) {
    Serial.print(F("Default Pattern restored for Pattern "));
    Serial.print(button);
    Serial.println(".");

    Serial.println(F("Patterns: "));
    for(int x = 0; x < 4; x++) {
      Serial.print(F("  "));
      for(int y = 0; y < 21; y++) {
        Serial.print(pattern[x][y]);
        Serial.print(F(":"));
      }
      Serial.println();
    }
  }
}

// This temporary pattern is used to contain only the 7 characters
// the display can handle.
void build_temp_pattern() {
  servonum_temp = servonum;
    for(int i = 0; i<20; i++) {
      displayPattern[i] = 0;
    }

    for (int i = 2; i >= 0; i--) {
      servonum_temp--;
      if(servonum_temp == 0)
        servonum_temp = pattern[currentPattern][0];
      displayPattern[i] = pattern[currentPattern][servonum_temp]+1;
    }
    
    servonum_temp = servonum;
    for (int i = 4; i < 7; i++) {
      servonum_temp++;
      if(servonum_temp > pattern[currentPattern][0])
        servonum_temp = 1;
      displayPattern[i] = pattern[currentPattern][servonum_temp]+1;
    }
}

int freeRam(void)
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
