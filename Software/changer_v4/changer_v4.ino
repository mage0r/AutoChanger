/*************************************************** 
 *  
 *  
 *  Autochanger - https://github.com/mage0r/AutoChanger
 *  
 *  Nano's are out, TinyPICO's are in.
 *  
 *  Probably worth some justification there.  There weren't enough clock cycles to update the screen AND make
 *  sure I didn't miss any event triggers from the sensor.  Generally, the Nano only having two interrupt pins
 *  didn't help.
 *  
 *  Oh god, please don't keep reading this code.  It is awful.
 *  
 *
 ****************************************************/

#include <Wire.h>
#include <Bounce2.h>
#include <Adafruit_PWMServoDriver.h>
#include <TinyPICO.h>

// webserver contains all the variables and the like
// needed to run the ESP32 manager.
#include "webserver.h"

// Set our version number.  Don't forget to update when featureset changes
#define PROJECT "AutoChanger"
#define VERSION "V.4.0"

// This is used by my runEvery Function.
// And I immediately realise the problem with this.
unsigned long previousMillis = 0;

byte DEBUG = 1; // Do we want debug output on serial.
// 0 is off.
// 1 is default logs.
// 2 shows timing information.
boolean BUZZER = 1; // Is the buzzer on or off. used to be a define, now a bool.

// The colour changer only uses 4 servos.
// content for this array is loaded from eeprom
// closed, open, eject
int servos[4][3];

unsigned long servoTimeout = 0;

unsigned long debugUpdate = 0;

// our servo # counter
int servonum = 1;
uint8_t maxServo = 4;
uint8_t servonum_temp; // declare this once and hopefully don't overrun my buffers

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// We track how many movements the servo has made.
// because, why not?! :)
unsigned int servoCount[4];
unsigned long EEPromTimeStamp;
unsigned int EEPromUpdateTime = 60000;

// Rotary encoder
long oldPosition  = -999;
byte menu_page = 0;
int menu_position = 0;
byte admin_mode = 0;
int temp_menu_position = 0;


const int armPin = 23;
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
byte armCounter = 0;  // We ignore every second pass.
boolean armTrigger = false;

// input buttons
// These are hardcoded
const int buttonPins[4] = {27,26,25,18};
const int pgrmPin = 4;
byte buttonState[4] = {false,false,false,false};
byte buttonExec[4] = {false,false,false,false};
boolean pgrmState = false;

// Debouncing
//Bounce debouncer[4] = Bounce();
Bounce * debouncer = new Bounce[4];
byte previousButton;
Bounce pgrmDebouncer = Bounce();
unsigned long buttonPressTimeStamp;
boolean triggerPgrm = false;
boolean pgrmMode = false;
boolean pgrmExec = false;
unsigned long pgrmTimeStamp = 0;
unsigned long btnTimeStamps[4] = {0,0,0,0};
byte temp_pattern[21] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // temporary pattern.

// Buzzer
const int buzzerPin = 19;
double buzzerNote = 1046.502;

// Array of ints to store our pattern.
int currentPattern = 0;
byte pattern[4][21];
String pattern2[4];
byte displayPattern[10];
int* activePattern = 0; // trying a dynamic array.
int activePatternLength = 0;

unsigned int test_run = 100;

// Non-blocking tones
unsigned long tone_off;

// Display update limiter
unsigned int displayUpdateTimer = 100;
unsigned long displayUpdate = 0;

// function prototypes.
// annoyingly, not evertything needs these.
void IRAM_ATTR arm_sensor_handler();

void setup() {
  Serial.begin(115200);

  setup_config();

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println(F("SPIFFS mount failed!"));
    return;
  } else {
    load_config(SPIFFS, "/config.ini");
  }

  wifi_counter = millis();

  // reset all the servo positions.
  setup_servos();

  // set up patterns.
  setup_patterns();

  // set up the Interrupts
  setup_interrupts();

  // Encoder config.
  setup_encoder();

  // buzzer config
  setup_buzzer();

  setup_buttons();

  // quick restore defaults.
  if(!digitalRead(buttonPins[0])) {
    RestoreDefault(0);
  } else if(!digitalRead(buttonPins[1])) {
    RestoreDefault(1);
  } else if(!digitalRead(buttonPins[2])) {
    RestoreDefault(2);
  } else if(!digitalRead(buttonPins[3])) {
    RestoreDefault(3);
  }

  build_temp_pattern();

  setup_display();

}

void loop() {

  // armTrigger is set by an interrupt.
  if(armTrigger) {
    operateArm();
    build_temp_pattern();
  }

  read_encoder();

  // Update our buttons.
  for (int x = 0; x < 4; x++) {
    debouncer[x].update();
  }
  pgrmDebouncer.update();

  // run our button combo section
  checkButtons();

  detachServo();


  // Don't need to update this quite so often.
  if(displayUpdate < millis() - displayUpdateTimer) {
    display_page();
    displayUpdate = millis();
  }

  check_tone();

  if(rebooting) {
    delay(100);
    ESP.restart();
  }

  if (runEvery(1000)) {
    // for the first 30 seconds, check if can get on wifi
    if (!wifi_enabled) {
      if (millis() - wifi_counter <= 30000) {
        setup_wifi();
      } else {
        // We've tried to hit the pre-configured wifi for 30 seconds.
        // time to give up and be our own host.
        setup_AP();
      }
    }
  }

  // check if we need to do ota activities.
  if(wifi_enabled) {
    ota_loop();
  }

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

    //writeEEPROM(); // update the eeprom if needed.
  }
}

boolean runEvery(unsigned long interval)
{
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    return true;
  }
  return false;
}
