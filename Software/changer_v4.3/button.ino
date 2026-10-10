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
      buttonLongFired[i] = false;
    } else if(debouncer[i].rose()) {
      if(!buttonLongFired[i])
        buttonShort[i] = true; // released before the long press fired
      buttonLongFired[i] = false;
      buttonState[i] = 0;
      buttonExec[i] = false;
    }
      
  }

  // If you hold a number button for 2 seconds, it will switch to that program set.
  // Not on the arm-adjust page - that uses a single press instead (see armAdjustMode()).
  if(menu_page != 2) {
    for(int i = 0; i<4; i++) {
      if(buttonState[i] == 1 && debouncer[i].duration() > 2000) {
        if(BUZZER)
              tone(BUZZER_PIN, buzzerNote, 300);
        change_program(i);
        check = i+1;
        
        buttonState[i] = 0;
        buttonLongFired[i] = true; // so the release isn't also taken as a short press
        
      }
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
  // Cycles 0 (main) -> 1 (program) -> 2 (arm adjust) -> 3 (wifi) -> 4 (i2c) ->
  // 5 (sensor) -> 6 (buzzer) -> back to 0.
  // held for two seconds
  if(pgrmState && pgrmDebouncer.duration() > 2000) {
    menu_page++;
    pgrmState = false;
    menu_position = 0; // reset the cursor, each page uses it differently
    if(DEBUG)
      weblog.println("Next Menu");
    
    if(menu_page == 2) {
      // entering the arm-adjust page - show the LOW position right away.
      syncArmServo();
    } else if(menu_page == 7) {
      // wrap back round to the main page. change_program() puts every arm down (they
      // may have been left up/mid-adjust on the arm-adjust page) and raises step 1's.
      menu_page = 0;
      change_program(currentPattern);
    }
  }

  // ok, what do we do with the buttons for regular presses.
  // Ok, these are now split up according to what page we're on.
  if(menu_page == 0)
    page_0();
  else if(menu_page == 1)
    programMode();
  else if(menu_page == 2)
    armAdjustMode();
  else if(menu_page == 3)
    wifiPageMode();
  else if(menu_page == 4)
    i2cPageMode();
  else if(menu_page == 5)
    sensorPageMode();
  else if(menu_page == 6)
    buzzerPageMode();

  // short presses are only ever consumed on the page they happened on
  for(int i = 0; i < 4; i++)
    buttonShort[i] = false;

}

// This is the page for adjusting the arm servo positions (Low/High/Eject) and the test cycle count
// (TEST). menu_position is even while browsing between fields (LOW=0, HIGH=2, EJECT=4, TEST=6) and
// odd while editing the highlighted field's value. A short press on the program button toggles
// between the two; rotating the encoder while editing adjusts the value via changeValues() (see
// changer_encoder.ino). On TEST, the first press edits the cycle count and the second saves it
// and runs the test on the selected arm.
void armAdjustMode() {

  // A single press of a number button switches which arm we're adjusting - no long press needed here.
  for(int i = 0; i < 4; i++) {
    if(buttonExec[i]) {
      currentPattern = i;
      buttonExec[i] = false;
      syncArmServo(); // reflect the newly selected arm's current field right away
    }
  }

  if(pgrmExec) {
    if(menu_position == 0) {
      // enter edit mode for the currently selected field.
      menu_position++;
    } else if(menu_position == 2) {
      menu_position++;
    } else if(menu_position == 4) {
      menu_position++;
    } else if (menu_position == 6) {
      menu_position++; // edit the test cycle count
    } else {
      // confirm the edit and persist it.
      if(menu_position == 1 || menu_position == 3 || menu_position == 5) {
        save_servos(LittleFS, "/servos.txt");
      } else {
        save_config(LittleFS, "/config.ini");
        startArmTest(currentPattern, false); // TEST: confirming the count runs it
      }
      menu_position--;
      if(DEBUG)
        weblog.println(F("Servo value saved."));
    }
  }
  pgrmExec = false;
}

// This is the WiFi page - a single screen. Pressing toggles WiFi on/off.
void wifiPageMode() {
  if(pgrmExec) {
    setWifiEnabled(!wifi_enabled);
    if(DEBUG)
      weblog.println(F("WiFi toggled."));
  }
  pgrmExec = false;
}

void i2cPageMode() {
  if(pgrmExec) {
    setI2CEnabled(!i2c_enabled);
    if(DEBUG)
      weblog.println(F("I2C toggled."));
  }
  pgrmExec = false;
}

void sensorPageMode() {
  if(pgrmExec) {
    if(menu_position == 0) {
      // toggle directly - a boolean doesn't need a separate edit submode the way
      // the numeric debounce field below does.
      setSensorEnabled(!sensor_enabled);
      if(DEBUG)
        weblog.println(F("Sensor toggled."));
    } else if(menu_position == 1) {
      // enter edit mode for debounce.
      menu_position = 2;
    } else {
      // confirm and persist the debounce edit.
      save_config(LittleFS, "/config.ini");
      menu_position = 1;
      if(DEBUG)
        weblog.println(F("Debounce saved."));
    }
  }
  pgrmExec = false;
}

// Same 3-position scheme as sensorPageMode() above: 0 = on/off (toggles directly),
// 1 = frequency browsing, 2 = frequency editing.
void buzzerPageMode() {
  if(pgrmExec) {
    if(menu_position == 0) {
      setBuzzerEnabled(!BUZZER);
      if(DEBUG)
        weblog.println(F("Buzzer toggled."));
    } else if(menu_position == 1) {
      menu_position = 2;
    } else {
      save_config(LittleFS, "/config.ini");
      menu_position = 1;
      if(DEBUG)
        weblog.println(F("Buzzer frequency saved."));
    }
  }
  pgrmExec = false;
}

// Change which program is active.
void change_program(byte new_program) {
  if(DEBUG) {
    weblog.print(F("Switching to Program: "));
    weblog.println(new_program+1);
  }

  for (int x = 0; x < 4; x++) {
    moveServo(x,0); // put all the servos down.
  }

  currentPattern = new_program;

  if( patterns[currentPattern].length) {
    // not an empty pattern
    // servonum is a 1-based STEP INDEX, not an arm number. This used to be set to
    // (first arm + 1), which only happened to equal 1 because every default
    // pattern starts with arm 0 - any pattern starting with another arm began
    // mid-sequence with the wrong arm up, and a short one could index past the
    // end of steps[].
    servonum = 1;
    moveServo(patterns[currentPattern].steps[0] - '0',1);
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
      save_patterns(LittleFS, "/patterns.txt");
    }
  } else if (menu_position == 1) {
    // This is the NEW
    // wipes out the entire array for this program
    if(pgrmExec) {
      wipe_pattern(currentPattern);
      save_patterns(LittleFS, "/patterns.txt");
    }
  } else if (menu_position == 2) {
    // This is the SAVE
    // write out the current pattern and drop back to the main page.
    if(pgrmExec) {
      save_patterns(LittleFS, "/patterns.txt");
      menu_position = 0; // reset the cursor so next time we enter program mode it starts at RES
      menu_page = 0;
      change_program(currentPattern); // arms down, step 1's arm up - matching what the screen shows
      if(DEBUG)
        weblog.println(F("Saved. Exiting Program Mode."));
    }
  } else if (menu_position > patterns[currentPattern].length+2) {
    // We've just added an extra entry
    for(int i = 0; i < 4; i++) {
      if(buttonExec[i] && buttonState[i]) {
          patterns[currentPattern].steps[patterns[currentPattern].length] = '0' + i;
          patterns[currentPattern].length++;
          patterns[currentPattern].steps[patterns[currentPattern].length] = '\0';
          menu_position++;
          if(DEBUG)
            weblog.println(F("Entry added (not yet saved - press SAV to persist)."));
        }
    }
  } else {
    for(int i = 0; i < 4; i++) {
      if(buttonExec[i] && buttonState[i]) {
          patterns[currentPattern].steps[menu_position-3] = '0' + i;
          menu_position++;
          if(DEBUG)
            weblog.println(F("Entry edited (not yet saved - press SAV to persist)."));
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

  // Acts once per short press (on release) - see buttonShort[] in button.h. Same
  // behaviour as webPressButton() below.
  for (int x = 0; x < 4; x++) {
    if(buttonShort[x])
      webPressButton(x);
  }
}

// Lets the web UI trigger the same action a physical arm button press would on the main
// page - same two behaviors as page_0() above, just called directly instead of driven by
// the debounced buttonState[]. Only meaningful on the main page (menu_page 0).
void webPressButton(int x) {
  if(menu_page != 0 || x < 0 || x > 3)
    return;

  if(!patterns[currentPattern].length) {
    // empty/manual pattern - toggle this arm up/down directly.
    if(servonum == x) {
      servonum = -1;
      moveServo(x, 0);
    } else {
      moveServo(x, 1);
      servonum = x;
    }
  } else {
    normalize_servonum();
    if((patterns[currentPattern].steps[servonum-1] - '0') == x) {
      // pressed the currently active rod - advance to the next step.
      switchRods();
    }
  }
}