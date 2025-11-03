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
#include <EEPROM.h>
#include <Adafruit_PWMServoDriver.h>
#include <TinyPICO.h>


byte DEBUG = 1; // Do we want debug output on serial.
// 0 is off.
// 1 is default logs.
// 2 shows timing information.
boolean BUZZER = 1; // Is the buzzer on or off. used to be a define, now a bool.

// Set our version number.  Don't forget to update when featureset changes
#define PROJECT "AutoChanger"
#define VERSION "V.3.8"

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
byte displayPattern[20];

// EEPROM Addresses
const int EEPROMCurrent = 0;
const int EEPROMServoCount = 10;
const int EEPROMServo = 50;
const int EEPROMPattern = 100;

unsigned int test_run = 100;

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
  
  // get our previous settings.
  setup_eeprom();

  // reset all the servo positions.
  setup_servos();

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

  //setup_wifi();

  delay(2000);
  
  if(DEBUG)
    Serial.println(F("Setup Completed."));

  if(DEBUG == 2)
    Serial.println(F("start,trigger,encoder,debouncer,buttons,servos,temp_pattern,display,tone,eeprom,finish"));

}

void loop() {

  if(DEBUG == 2)
    timeLogging[1][0] = millis();
  
  // armTrigger is set by an interrupt.
  if(armTrigger) {
    operateArm();
  }

  if(DEBUG == 2)
    timeLogging[1][1] = millis();

  read_encoder();

  if(DEBUG == 2)
    timeLogging[1][2] = millis();

  // Update our buttons.
  for (int x = 0; x < 4; x++) {
    debouncer[x].update();
  }
  pgrmDebouncer.update();

  if(DEBUG == 2)
    timeLogging[1][3] = millis();

  // run our button combo section
  checkButtons();

  if(DEBUG == 2)
    timeLogging[1][4] = millis();

  detachServo();

  if(DEBUG == 2)
    timeLogging[1][5] = millis();

  build_temp_pattern();

  if(DEBUG == 2)
    timeLogging[1][6] = millis();

  // Don't need to update this quite so often.
  if(displayUpdate < millis() - displayUpdateTimer) {
    display_page();
    displayUpdate = millis();
  }

  if(DEBUG == 2)
    timeLogging[1][7] = millis();

  check_tone();

  if(DEBUG == 2)
    timeLogging[1][8] = millis();

  writeEEPROM();

  if(DEBUG == 2)
    timeLogging[1][9] = millis();

  //server.handleClient();
  //wm.process();

  if(DEBUG == 2)
    timeLogging[1][10] = millis();

  // lets do some comparisons to work out how long the executions take.
  if(DEBUG == 2) {
    timeLogging[0][0] = timeLogging[1][0];
    for(int i = 1; i < 11; i++) {
      long tempLog = timeLogging[1][i] - timeLogging[1][i-1];
      if(tempLog > timeLogging[0][i])
        timeLogging[0][i] = tempLog;
    }
  }

  if(DEBUG == 2 && millis() > 10000 && debugUpdate < millis() - 10000) {
    // every 10 seconds show the highest time value we've seen.
    // looks like the maximum time is around 70ms.
    // max 19ms to update the oled.
    // around 50ms to write the eeprom
    Serial.print(timeLogging[0][0]);
    for(int i = 1; i < 11; i++) {
        Serial.print(F(","));
        Serial.print(timeLogging[0][i]);
    }
    Serial.println();
    debugUpdate = millis();
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

    writeEEPROM(); // update the eeprom if needed.
  }
}
