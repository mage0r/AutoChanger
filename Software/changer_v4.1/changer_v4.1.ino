/*************************************************** 
 *  
 *  
 *  Autochanger - https://github.com/mage0r/AutoChanger
 *  
 *  Nano's are out, TinyPICO's are out, ESP32-S3 WROOM modules are in.
 *  
 *  Probably worth some justification there.  There weren't enough clock cycles to update the screen AND make
 *  sure I didn't miss any event triggers from the sensor.  Generally, the Nano only having two interrupt pins
 *  didn't help.
 *
 * The TinyPICO's are a bit pricey and getting hard to get.
 *  
 *  Oh god, please don't keep reading this code.  It is awful.
 *  
 *
 ****************************************************/

#include <Wire.h>
#include <Bounce2.h>
#include <Adafruit_PWMServoDriver.h>

// webserver contains all the variables and the like
// needed to run the ESP32 manager.
#include "webserver.h"

byte DEBUG = 1; // Do we want debug output on serial.
// 0 is off.
// 1 is default logs.
// 2 shows timing information.
boolean BUZZER = 1; // Is the buzzer on or off. used to be a define, now a bool.

// Set our version number.  Don't forget to update when featureset changes
#define PROJECT "AutoChanger"
#define VERSION "V.4.1"

#define ARM 10
#define BUTTON_1 13
#define BUTTON_2 14
#define BUTTON_3 15
#define BUTTON_4 16
#define PGRM_BTN 4
#define PGRM_A 6
#define PGRM_B 5
#define BUZZER_PIN 1

#define FORMAT_SPIFFS_IF_FAILED true

// The colour changer only uses 4 servos.
// content for this array is loaded from eeprom
// closed, open, eject
int servos[4][3];
boolean servosDefaulted = false; // set true by default_servos() when /servos.txt wasn't found

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
unsigned int oldservoCount[4];
unsigned long countSaveMillis;
unsigned int countSaveTime = 300000; // 5 minutes

// Rotary encoder
long oldPosition  = 0; // matches encoder.setCount(0) in setup_encoder()
byte menu_page = 0;
int menu_position = 0;
byte admin_mode = 0;
int temp_menu_position = 0;


unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
byte armCounter = 0;  // We ignore every second pass.
boolean armTrigger = false;

// button states
byte buttonState[4] = {false,false,false,false};
byte buttonExec[4] = {false,false,false,false};
boolean pgrmState = false;

// Debouncing
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
double buzzerNote = 1046.502;

// Array of ints to store our pattern.
int currentPattern = 0;
byte pattern[4][21];
byte displayPattern[20];

unsigned int test_run = 10;

// Non-blocking tones
unsigned long tone_off;

// Display update limiter
unsigned int displayUpdateTimer = 100;
unsigned long displayUpdate = 0;

// function prototypes.
// annoyingly, not evertything needs these.
void IRAM_ATTR arm_sensor_handler();

//time logging.
unsigned long timeLogging[2][11] = {{0,0,0,0,0,0,0,0,0,0,0},
                                    {0,0,0,0,0,0,0,0,0,0,0}};

// This is used by my runEvery Function.
// The name is arbitrary and you should create
// a new variable for each timed instance.
unsigned long run1000 = 0;

void setup() {
  Serial.begin(115200);

  // First, build information
  Serial.print(F(PROJECT));
  Serial.print(F(" "));
  Serial.println(F(VERSION));
  Serial.print(F("Build Date: "));
  Serial.println(F(__DATE__ " " __TIME__));
  Serial.print(F("Free Ram: "));
  Serial.println(ESP.getFreeHeap());

  setup_config();

  // I can't explain this, but if the display is set up after the servos it all falls apart
  setup_display();

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println(F("SPIFFS mount failed!"));
    return;
  } else {
    load_config(SPIFFS, "/config.ini");
  }

  // set up the Interrupts
  setup_interrupts();

  // Encoder config.
  setup_encoder();

  // buzzer config
  setup_buzzer();

  setup_buttons();

  setup_patterns();

  load_config(SPIFFS, "/config.txt");

  // quick restore defaults.
  if(!digitalRead(BUTTON_1)) {
    RestoreDefault(0);
  } else if(!digitalRead(BUTTON_2)) {
    RestoreDefault(1);
  } else if(!digitalRead(BUTTON_3)) {
    RestoreDefault(2);
  } else if(!digitalRead(BUTTON_4)) {
    RestoreDefault(3);
  }

  build_temp_pattern();

  // reset all the servo positions.
  setup_servos();

  delay(2000);
  
  if(DEBUG)
    Serial.println(F("Setup Completed."));


}

void loop() {
  if(rebooting) {
    delay(100);
    ESP.restart();
  }

  // armTrigger is set by an interrupt.
  if(armTrigger) {
    operateArm();
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

  build_temp_pattern();

  // Don't need to update this quite so often.
  if(displayUpdate < millis() - displayUpdateTimer) {
    display_page();
    displayUpdate = millis();
  }

  check_tone();

  // if it's changed, update the count for each servo.
  if(millis() - countSaveMillis >= countSaveTime) {
    countSaveMillis = millis();
    boolean updateCount = false;
    for(int i = 0; i<4; i++) {
      if(servoCount[i] != oldservoCount[i]) {
        oldservoCount[i] = servoCount[i];
        updateCount = true;
      }
    }
    if(updateCount)
      save_servos(SPIFFS, "/servos.txt");
  }

  if (runEvery(1000, &run1000)) {
    // for the first 30 seconds, check if can get on wifi
    if (wifi_enabled && !wifi_connected) {
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
  if(wifi_enabled && wifi_connected) {
    ota_loop();
  }
}

boolean runEvery(unsigned long interval, unsigned long *previousMillis)
{
  unsigned long currentMillis = millis();
  if (currentMillis - *previousMillis >= interval) {
    *previousMillis = currentMillis;
    return true;
  }
  return false;
}

// Cycles the currently selected arm's servo between its LOW and HIGH positions, CYCL times.
// Used to sanity-check the LOW/HIGH values you've just set on the arm-adjust page.
void testCycle() {
  for (int i = 0; i < test_run; i++) {
    moveServo(currentPattern, 1); // up to HIGH
    delay(400);
    moveServo(currentPattern, 0); // down to LOW
    delay(400);
  }
}