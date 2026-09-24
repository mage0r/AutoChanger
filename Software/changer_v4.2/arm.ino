/*
 * operations related to our arm, ie. the shuttle that moves past the sensor.
 */

void operateArm() {
 // Check if we need to update the arm
    // We only need to do this if our pattern is non-zero
    // If the pattern is 0, it's a manual pattern
    // only do this activity if the 
    if(patterns[currentPattern].length && (millis() - lastDebounceTime) > debounceDelay) {
      if(armCounter == 0){
          switchRods();
          armCounter = 1;
          
          // Turn the buzzer on for 1 second
          if(BUZZER)
            tone(BUZZER_PIN, buzzerNote, 1000);
      }
      else {
          armCounter = 0;
      }

      lastDebounceTime = millis();
    }
    armTrigger = false;
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