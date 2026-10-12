/*
 * operations related to our arm, ie. the shuttle that moves past the sensor.
 */

// Turns automatic response to the arm sensor on or off and saves the setting - used
// by the serial SENSOR_ENABLE command, the OLED's sensor page, and (indirectly)
// nothing else yet. The sensor itself keeps firing either way; see the gating
// comment in changer_v4.3.ino's loop() for what actually changes. Mainly useful
// alongside the AYAB-facing S/R/L commands (commands.ino) - AYAB driving arms
// directly and the automatic sequence both reacting to every carriage pass would
// fight each other.
void setSensorEnabled(boolean enabled) {
  sensor_enabled = enabled;
  save_config(LittleFS, "/config.ini");
}

// Acts on any carriage passes the sensor interrupt has counted since the last call, in
// order - one switch per two passes, as before. Called every loop().
//
// At DEBUG 1 it also logs what happened to every edge, so a missed pass can be told
// apart: no edge at all means the sensor didn't see the carriage (alignment, distance,
// a faulty sensor); edges "ignored as bounce" mean the debounce is swallowing them;
// "ignored" passes say why (sensor off, not on the main page, manual pattern).
void process_sensor() {
  static uint32_t seenEdges = 0;
  static uint32_t seenPasses = 0;

  uint32_t edges = sensorEdges;   // 32-bit reads - atomic on the ESP32
  uint32_t passes = sensorPasses;
  uint32_t newEdges = edges - seenEdges;
  uint32_t newPasses = passes - seenPasses;
  seenEdges = edges;
  seenPasses = passes;

  if(newEdges > newPasses && DEBUG) {
    weblog.print(F("Sensor: "));
    weblog.print(newEdges - newPasses);
    weblog.print(F(" edge(s) ignored as bounce (less than "));
    weblog.print(debounceDelay);
    weblog.println(F("ms after the previous edge)"));
  }

  for(uint32_t i = 0; i < newPasses; i++) {
    const __FlashStringHelper *ignored = NULL;
    if(!sensor_enabled)
      ignored = F("sensor is off");
    else if(menu_page != 0)
      ignored = F("not on the main page");
    else if(!patterns[currentPattern].length)
      ignored = F("manual pattern");

    if(ignored) {
      if(DEBUG) {
        weblog.print(F("Sensor: pass ignored - "));
        weblog.println(ignored);
      }
      continue;
    }

    if(armCounter == 0) {
      switchRods();
      armCounter = 1;
      if(DEBUG)
        weblog.println(F("Sensor: pass - switching arms"));

      // Turn the buzzer on for 1 second
      if(BUZZER)
        tone(BUZZER_PIN, buzzerNote, 1000);
    } else {
      armCounter = 0;
      if(DEBUG)
        weblog.println(F("Sensor: pass - no switch (every second pass)"));
    }
  }
}

// Moves the currently selected arm's servo to match whichever LOW/HIGH/EJECT field is in view.
// Call this only when something actually changes (switching field, switching arm, adjusting a
// value) - never on a bare loop tick. detachServo() releases the servo after 400ms idle; polling
// this every tick would make that release look like a change worth reacting to and re-trigger
// indefinitely, fighting the detach forever.
void syncArmServo() {
  if(menu_position <= 1) {
    moveServo(currentPattern, 0); // LOW
  } else if(menu_position <= 3) {
    moveServo(currentPattern, 1); // HIGH
  } else if(menu_position <= 5) {
    moveServo(currentPattern, 2); // EJECT
  }
}

// Starts (or restarts) a non-blocking test sequence - one arm, or all four in
// sequence - for loop() to carry out over the following seconds (see the
// servoTestPending check in changer_v4.3.ino and testCycleArm() below). Used by
// the web UI's /servo_test and /servo_test_all, and the serial TEST command.
// Explicitly resets armTestCount and armTestAll rather than trusting whatever
// they were last left at: armTestCount only gets reset by testCycleArm() itself
// when a count naturally completes, and armTestAll isn't touched by a single-arm
// request at all - so starting a new test before a previous one finished on its
// own would otherwise inherit a stale count, or have a single-arm request
// unexpectedly cascade into testing the rest because an earlier "all" run never
// got the chance to clear that flag.
void startArmTest(int arm, bool all) {
  armTestCount = 0;
  armTestAll = all;
  servoTestArm = arm;
  servoTestPending = true;
}

// Does the actual cycling, taking the arm directly rather than relying on
// currentPattern - that global is reused for which arm the OLED's arm-adjust page
// has selected, but it's ALSO the active knitting pattern's index. Temporarily
// overwriting it to test a specific arm from the web (which runs concurrently with
// whatever the sensor/main loop is doing) could corrupt an in-progress knit if the
// sensor fires at the wrong moment - this sidesteps that risk entirely by never
// touching currentPattern at all.
void testCycleArm(int arm, int count) {
  if(armUp[arm]) {
    moveServo(arm, 0); // down to LOW
  } else {
    moveServo(arm, 1); // up to HIGH
  }

  armTestCount++;

  if(armTestCount >= count ) { // met our testing requirements
    armTestCount = 0;
    
    if(armTestAll && servoTestArm < 3) {
      // if we're testing all the arms and we haven't hit the end.
      servoTestArm++; // break out and set the global for the next cycle.
      servoTestPending = true; // back in the game
    } else {
      armTestAll = false;
      servoTestPending = false; // finished testing
    }
    
  }
}