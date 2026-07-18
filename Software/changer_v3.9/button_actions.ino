// The autochanger has 4 arm buttons and a rotorary encoder (realistically, giving it 3 more buttons).
//
// Depending on the state of the environment, these buttons do completely different things.
//
// This is not at all fun.
//
// 

void setup_buttons() {

  // the problem with defines is no loops
  pinMode(BUTTON_1, INPUT_PULLUP);
  debouncer[0].attach(BUTTON_1);
  debouncer[0].interval(5);
  pinMode(BUTTON_2, INPUT_PULLUP);
  debouncer[1].attach(BUTTON_2);
  debouncer[1].interval(5);
  pinMode(BUTTON_3, INPUT_PULLUP);
  debouncer[2].attach(BUTTON_3);
  debouncer[2].interval(5);
  pinMode(BUTTON_4, INPUT_PULLUP);
  debouncer[3].attach(BUTTON_4);
  debouncer[3].interval(5);


  // This is the button on our Encoder
  pinMode(PGRM_BTN, INPUT_PULLUP);
  pgrmDebouncer.attach(PGRM_BTN);
  pgrmDebouncer.interval(5);
}

// look for our button combinations.
void checkButtons() {
  // Update the button states.

  byte check = 0; // this is a temp variable that gets set as the active button being pressed.

  for(int i = 0; i<4; i++) {
    if(debouncer[i].fell()) {
      buttonState[i] = 1;
      buttonExec[i] = true;
    } else if(debouncer[i].rose()) {
      buttonState[i] = 0;
      buttonExec[i] = false;
    }
      
  }

  // If you hold a number button for 2 seconds, it will switch to that program set.
  for(int i = 0; i<4; i++) {
    if(buttonState[i] == 1 && debouncer[i].duration() > 2000) {
      if(BUZZER)
            tone(BUZZER_PIN, buzzerNote, 300);
      change_program(i);
      check = i+1;
      
      buttonState[i] = 0;
      
    }
  }

  if(pgrmDebouncer.fell()) {
    pgrmState = true;
    pgrmExec = false;
  } else if(pgrmDebouncer.rose()) {
    if(pgrmState) {
      // bit of a weird edge case.  If we're doing a transition and have already
      // released the pgrmState as a result of the next bit of code, we don't
      // want to do this code.
      pgrmExec = true;
    }
    pgrmState = false;
  }

  // button has been pressed and released
  

  // This one iterates throught he menu pages.
  // probably needs to be cut down.  you can only iterate between page 0 and 1.
  // held for two seconds
  if(pgrmState && pgrmDebouncer.duration() > 2000) {
    menu_page++;
    pgrmState = false;
    if(DEBUG)
      Serial.println("Next Menu");
    
    if(menu_page == 2) {
      servonum = pattern[currentPattern][1];
      menu_page = 0;
    }
  }

  // ok, what do we do with the buttons for regular presses.
  // Ok, these are now split up according to what page we're on.
  if(menu_page == 0)
    page_0();
  else if(menu_page == 1)
    programMode();

 

}

// Change which program is active.
void change_program(byte new_program) {
  if(DEBUG) {
    Serial.print(F("Switching to Program: "));
    Serial.println(new_program+1);
  }

  for (int x = 0; x < 4; x++) {
    moveServo(x,0); // put all the servos down.
  }

  currentPattern = new_program;
  //servonum = 1;

  if( pattern[currentPattern][0]) {
    // not an empty pattern
    servonum = pattern[currentPattern][1]+1;
    moveServo(pattern[currentPattern][1],1);
  } else {
    servonum = -1;
  }
}

// kind of the wrong name for this function
// It handles when we EXIT program mode
void programMode() {
  // write our temp pattern to the array and reset the values to 0.
  // only do this if we actually have a pattern, i.e. more than 1 entry.


  if(menu_position == 0) {
    // This is the RES
    // reset the current pattern to the default.
    if(pgrmExec) {
      RestoreDefault(currentPattern);
      save_patterns(SPIFFS, "/patterns2.txt");
    }
  } else if (menu_position == 1) {
    // This is the NEW
    // wipes out the entire array for this program
    if(pgrmExec) {
      wipe_pattern(currentPattern);
      save_patterns(SPIFFS, "/patterns2.txt");
    }
  } else if (menu_position > pattern[currentPattern][0]+1) {
    // We've just added an extra entry
    for(int i = 0; i < 4; i++) {
      if(buttonExec[i] && buttonState[i]) {
          pattern[currentPattern][0]++;
          int temp = pattern[currentPattern][0];
          pattern[currentPattern][temp] = i;
          menu_position++;
          if(DEBUG)
            Serial.println(F("Saving Pattern."));
          save_patterns(SPIFFS, "/patterns2.txt");
        }
    }
  } else {
    for(int i = 0; i < 4; i++) {
      if(buttonExec[i] && buttonState[i]) {
          pattern[currentPattern][menu_position-1] = i;
          menu_position++;
          if(DEBUG)
            Serial.println(F("Saving Pattern."));
          save_patterns(SPIFFS, "/patterns2.txt");
        }
    }
  }

  for(int i = 0; i < 4; i++) {
    buttonExec[i] = false;
  }
  // this should work.
  pgrmExec = false;
}

// This is the main page for the system.
// you know, the one 99% of the time is in use.
// This is the stupidest name.
void page_0() {

  for (int x = 0; x < 4; x++) {
    if (!pattern[currentPattern][0]) {
      // This is a special case.  If there are no entries in a pattern
      // it should be the loading pattern.
      if(buttonState[x] && btnTimeStamps[x] < millis() - 1000) {
          if(servonum == x ) {
            servonum = -1;
            moveServo(x,0); // move down.
            btnTimeStamps[x] = millis();
          } else {
            moveServo(x,1); // move up
            servonum = x;
            btnTimeStamps[x] = millis();
          }
        }
    }
    else if (pattern[currentPattern][servonum] == x && buttonState[x]) {
      // if you've pressed the active rod, this will rotate it.
      switchRods();
    }
    
  }
}

// This is the page for re-programming the thing.
// Seriously, the worst names.
void page_1(byte check) {
  
   // put down the previous Servo.
      moveServo(servonum, 0);
      //servonum = button;
      moveServo(servonum, 1);
      
      // iterate the temp pattern number.
      temp_pattern[0]++;
      // Add this button to the temp_pattern.
      //temp_pattern[temp_pattern[0]] = button;

      if(temp_pattern[0] == 20) {
        pgrmMode = false;
        if(DEBUG)
          Serial.println(F("Limit Reached.  Exiting Program Mode."));
        programMode();
      }
}
