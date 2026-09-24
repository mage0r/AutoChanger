/*
 * operations related to our arm, ie. the shuttle that moves past the sensor.
 */

void operateArm() {
 // Check if we need to update the arm
    // We only need to do this if our pattern is non-zero
    // If the pattern is 0, it's a manual pattern
    // only do this activity if the 
    if(pattern[currentPattern][0] && (millis() - lastDebounceTime) > debounceDelay) {
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
