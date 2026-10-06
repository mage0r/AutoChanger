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
 *  The TinyPICO's are a bit pricey and getting hard to get.
 *  
 *  Oh god, please don't keep reading this code.  It is awful.
 *  It was awful when I wrote it, Claude has implemented a lot of features for me and has not made it any easier to read.
 *  Probably fair to say it's Claudes more than mine now really.
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

// A fixed-size ring buffer that tees everything written to it out to the real
// Serial (so the USB monitor keeps working exactly as before) and also keeps the
// most recent bytes in RAM, so the web UI's Serial Log tab can show them too. This
// is the main sketch file specifically so every other file can see the global
// instance below - per Arduino's own build docs, this file (matching the sketch
// folder's name) is always concatenated first, so a global declared here is the
// only kind guaranteed visible everywhere else, regardless of those other files'
// alphabetical order.
#define LOG_BUFFER_SIZE 4096

byte DEBUG = 1; // 0 = off, 1 = normal (if(DEBUG) throughout this codebase - fires for
                 // any non-zero value, including 2), 2 = also prefixes every line
                 // weblog prints with a millis() timestamp (see LogBuffer::write()
                 // just below) - for digging into timing-sensitive issues, where
                 // WHEN each of the existing log lines happened matters, not just
                 // their order. Nothing has to be added anywhere to use this - every
                 // weblog.print()/println() call already in the codebase gets
                 // timestamped automatically once this is 2.

class LogBuffer : public Print {
  public:
    size_t write(uint8_t c) override {
      // Stamps the START of each line, not every byte - checked here so it applies
      // to every existing call site for free, rather than needing every single
      // weblog.print() throughout the codebase to be changed individually.
      if(DEBUG >= 2 && atLineStart) {
        String prefix = String(millis()) + ": ";
        for(size_t i = 0; i < prefix.length(); i++)
          writeRaw((uint8_t)prefix[i]);
        atLineStart = false;
      }
      writeRaw(c);
      if(c == '\n')
        atLineStart = true;
      return 1;
    }
    size_t write(const uint8_t *data, size_t size) override {
      for(size_t i = 0; i < size; i++)
        write(data[i]);
      return size;
    }
    // Oldest-to-newest, exactly as Serial would have shown it.
    String getContents() {
      String result;
      result.reserve(filled);
      size_t start = (filled < LOG_BUFFER_SIZE) ? 0 : head;
      for(size_t i = 0; i < filled; i++) {
        result += (char)buf[(start + i) % LOG_BUFFER_SIZE];
      }
      return result;
    }
    void clear() {
      head = 0;
      filled = 0;
    }
  private:
    void writeRaw(uint8_t c) {
      Serial.write(c);
      buf[head] = c;
      head = (head + 1) % LOG_BUFFER_SIZE;
      if(filled < LOG_BUFFER_SIZE)
        filled++;
    }
    uint8_t buf[LOG_BUFFER_SIZE];
    size_t head = 0;
    size_t filled = 0;
    bool atLineStart = true;
};

LogBuffer weblog;
// 2 shows timing information.
boolean BUZZER = 1; // Is the buzzer on or off. used to be a define, now a bool.

// Set our version number.  Don't forget to update when featureset changes
#define PROJECT "AutoChanger"
#define VERSION "V.4.2.8"

#define ARM 10
#define BUTTON_1 13
#define BUTTON_2 14
#define BUTTON_3 15
#define BUTTON_4 16
#define PGRM_BTN 4
#define PGRM_A 6
#define PGRM_B 5
#define BUZZER_PIN 1

// Secondary I2C bus (see i2c.ino) - this device acts as the I2C peripheral, reusing
// the same command interpreter as Serial.
#define I2C2_SDA 2
#define I2C2_SCL 7
#define I2C2_ADDRESS_DEFAULT 0x42 // used if i2c2_address in config.ini is missing or out of range
#define I2C2_RESPONSE_MAX 120 // stay comfortably under the ~126 usable bytes of Wire's 128-byte buffer

boolean i2c_enabled = false; // persisted in config.ini - see I2C_ENABLE in commands.ino
uint8_t i2c2_address = I2C2_ADDRESS_DEFAULT; // persisted in config.ini - 7-bit address, 0x08-0x77

#define FORMAT_SPIFFS_IF_FAILED true

// The colour changer only uses 4 servos.
// content for this array is loaded from eeprom
// closed, open, eject
int servos[4][3];

// A servo test is requested here (see /servo_test in webserver.ino) and actually run
// from loop() (see below), never from the web handler itself. ESPAsyncWebServer
// handlers run on the async_tcp task - testCycleArm()'s delay(400) calls, run
// directly in a handler, block that task long enough to trip the ESP32's task
// watchdog and crash the device (confirmed from an actual crash log: "async_tcp"
// named as the task that failed to check in, "loopTask" still listed as running
// fine). loop() already blocks for the same duration when the OLED's own TEST
// button runs this, without issue, so running it from there instead avoids the
// problem rather than working around it.
volatile bool servoTestPending = false;
int servoTestArm = -1;
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
  weblog.print(F(PROJECT));
  weblog.print(F(" "));
  weblog.println(F(VERSION));
  weblog.print(F("Build Date: "));
  weblog.println(F(__DATE__ " " __TIME__));
  weblog.print(F("Free Ram: "));
  weblog.print(ESP.getFreeHeap() / 1024);
  weblog.println(F(" KB"));

  // Chip/flash identity - compare "Flash Size" against whatever the Arduino IDE's
  // Tools > Flash Size is set to. A mismatch there (not in this code) is the usual
  // cause of a wrong/too-small SPIFFS partition.
  weblog.print(F("Chip Model: "));
  weblog.print(ESP.getChipModel());
  weblog.print(F(" rev"));
  weblog.println(ESP.getChipRevision());
  weblog.print(F("Flash Size: "));
  weblog.print(ESP.getFlashChipSize() / 1024 / 1024);
  weblog.println(F(" MB"));
  weblog.print(F("Sketch Size: "));
  weblog.print(ESP.getSketchSize() / 1024);
  weblog.print(F(" KB, Free Sketch Space: "));
  weblog.print(ESP.getFreeSketchSpace() / 1024);
  weblog.println(F(" KB"));

  setup_config();

  // I can't explain this, but if the display is set up after the servos it all falls apart
  setup_display();

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    weblog.println(F("SPIFFS mount failed!"));
    return;
  } else {
    weblog.print(F("SPIFFS Total: "));
    weblog.print(SPIFFS.totalBytes() / 1024);
    weblog.print(F(" KB, Used: "));
    weblog.print(SPIFFS.usedBytes() / 1024);
    weblog.println(F(" KB"));
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

  if(i2c_enabled)
    setup_i2c_secondary();

  delay(2000);
  
  if(DEBUG)
    weblog.println(F("Setup Completed."));


}

void loop() {
  if(rebooting) {
    delay(100);
    ESP.restart();
  }

  readSerialCommands();
  readI2CCommands();

  // armTrigger is set by an interrupt.
  if(armTrigger) {
    operateArm();
  }

  // Set by /servo_test (webserver.ino) - run here, not in the handler itself. See
  // the comment on servoTestPending above for why.
  if(servoTestPending) {
    servoTestPending = false;
    testCycleArm(servoTestArm, test_run);
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