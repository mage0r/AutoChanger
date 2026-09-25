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
 *  Claude has assisted me heavily since v3.9.  It has not made anything any easier to read.
 *  
 *
 ****************************************************/

#include <Wire.h>
#include <Bounce2.h>
#include <Adafruit_PWMServoDriver.h>

// webserver contains all the variables and the like
// needed to run the ESP32 manager.
#include "webserver.h"

// patterns.h contains all the types and globals for pattern storage.
#include "patterns.h"

// arm.h contains the arm-sensor debounce/pass-counter globals.
#include "arm.h"

// button.h contains the button-debounce state globals.
#include "button.h"

byte DEBUG = 1; // Do we want debug output on serial.
// 0 is off.
// 1 is default logs.
// 2 shows timing information.
boolean BUZZER = 1; // Is the buzzer on or off. used to be a define, now a bool.

// Set our version number.  Don't forget to update when featureset changes
#define PROJECT "AutoChanger"
#define VERSION "V.4.2"

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


// Buzzer
double buzzerNote = 1046.502;

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
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.println(F(" KB"));

  // Chip/flash identity - compare "Flash Size" against whatever the Arduino IDE's
  // Tools > Flash Size is set to. A mismatch there (not in this code) is the usual
  // cause of a wrong/too-small SPIFFS partition.
  Serial.print(F("Chip Model: "));
  Serial.print(ESP.getChipModel());
  Serial.print(F(" rev"));
  Serial.println(ESP.getChipRevision());
  Serial.print(F("Flash Size: "));
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(F(" MB"));
  Serial.print(F("Sketch Size: "));
  Serial.print(ESP.getSketchSize() / 1024);
  Serial.print(F(" KB, Free Sketch Space: "));
  Serial.print(ESP.getFreeSketchSpace() / 1024);
  Serial.println(F(" KB"));

  setup_config();

  // I can't explain this, but if the display is set up after the servos it all falls apart
  setup_display();

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println(F("SPIFFS mount failed!"));
    return;
  } else {
    Serial.print(F("SPIFFS Total: "));
    Serial.print(SPIFFS.totalBytes() / 1024);
    Serial.print(F(" KB, Used: "));
    Serial.print(SPIFFS.usedBytes() / 1024);
    Serial.println(F(" KB"));
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

  readSerialCommands();

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