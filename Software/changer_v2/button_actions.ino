// The autochanger has 4 arm buttons and a rotorary encoder (realistically, giving it 3 more buttons).
//
// Depending on the state of the environment, these buttons do completely different things.
//
// This is not at all fun.
//
// 

// look for our button combinations.
void checkButtons() {
  // Update the button states.
  boolean pgrm = (pgrmDebouncer.fell());
  for(int i = 0; i<4; i++) {
    buttonState[i] = (debouncer[i].fell());
    if(buttonState[i])
      btnTimeStamps[i] = millis();
  }

  byte check = 0;

  // what the fuck does this even do?
  // I think it works out when somehting gets released.
  // Like, checks if the button was pressed for 2 seconds.
  for(int i = 0; i<4; i++) {
    if(debouncer[i].rose() && btnTimeStamps[i] != 0 && btnTimeStamps[i] + 2000 <= millis()) {
      btnTimeStamps[i] = 0;
      check = i+1;
    }
  }

  // This one iterates throught he menu pages.
  // probably needs to be cut down.  you can only iterate between page 0 and 1.
  if(pgrmDebouncer.rose() && pgrmTimeStamp != 0 && pgrmTimeStamp + 2000 <= millis()) {
    menu_page++;
    if(menu_page == 1) {
      menu_page = 0;
    }
  }


  // Ok, these are now split up according to what page we're on.
  
  if(menu_page == 0)
    page_0(check);

  if (pgrm) {
    // pgrm button now cycles between screens on the display.
    pgrmTimeStamp = millis();
  } else {
    // buttons have been released.
    // how to make this efficient.....
  }
      
}

// This is the main page for the system.
// you know, the one 99% of the time is in use.
// This is the stupidest name.
void page_0(byte check) {

  for (int x = 0; x < 4; x++) {
    if(check-1 == x) {
      // this check works out if we're trying to switch to a different pattern.
      // First close all the nodes.
      moveServo(x,1);
      if(DEBUG) {
          Serial.print(F("Switching to Program: "));
          Serial.println(check);
      }
      if (!pattern[currentPattern][0] && pattern[check-1][1] != servonum) {
          // special edge case, if our previous pattern was empty, put all the servos down.
          // oh, and we re-used servonum to act as our current servo, rather than a position in the
          // pattern array.
          for (int x = 0; x < 4; x++) {
              moveServo(x,1);
          }
        } else if(pattern[currentPattern][servonum] != pattern[check-1][1]) {
          moveServo(pattern[currentPattern][servonum],1); // put our current servo down.
        }
        
        currentPattern = check-1;
        servonum = 1; // first in the pattern.
        // only need to do stuff if the pattern is active
        if( pattern[currentPattern][0]) {
          moveServo(pattern[currentPattern][servonum],2);
        }

    }
    else if (!pattern[currentPattern][0]) {
      // This is a special case.  If there are no entries in a 
      if(buttonState[x]) {
          if(servonum == x) {
            moveServo(x,1); // move down.
            servonum = -1;
          } else {
            moveServo(x,2); // move up
            servonum = x;
          }
        } else {
          moveServo(x,1);
        }
    }
    else if (pattern[currentPattern][servonum] == x && buttonState[x]) {
      switchRods();
    }
    
  }
}

// This is the page for re-programming the thing.
// Seriously, the worst names.
void page_1() {
  
   // put down the previous Servo.
      moveServo(servonum, 1);
      //servonum = button;
      moveServo(servonum, 2);
      
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
