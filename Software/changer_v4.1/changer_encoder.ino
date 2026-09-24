#include <ESP32Encoder.h>

// Rotary encoder
ESP32Encoder encoder;

void setup_encoder() {
  if(DEBUG)
    Serial.println(F("Configuring Encoder."));
    
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachHalfQuad(PGRM_A, PGRM_B);
  encoder.setCount(0);
}

void read_encoder() {
  long newPosition = encoder.getCount();
  long delta = newPosition - oldPosition; // this encoder reports ~3 raw counts per physical click/detent

  if (delta >= 3 || delta <= -3) {

    boolean forward = (delta > 0);

    // if we're on the main menu page:
    if(menu_page == 0) {
      armCounter = forward ? 0 : 1;
    } else if(menu_page == 1) {
      // We're in programming mode
      // shift the cursor around.

      if(forward)
        menu_position++;
      else
        menu_position--;

      if(menu_position < 0)
        menu_position = 0;
      else if (menu_position >= pattern[currentPattern][0]+4) // When re-programming, max position is n+2 (RES, NEW, SAVE take 0-2).
        menu_position = pattern[currentPattern][0]+3;
      
    } else if(menu_page == 2) {
      // We're on the arm adjust page.
      // Even menu_position = browsing between fields, odd = editing the selected field's value.
      if(menu_position % 2 == 0) {
        if(forward)
          menu_position += 2;
        else
          menu_position -= 2;

        if(menu_position < 0)
          menu_position = 0;
        else if(menu_position > 7)
          menu_position = 7;
      } else if (menu_position == 7) {
        changeValues(forward ? 1 : -1);
      } else {
        int step = (menu_position == 7) ? 1 : 10; // servo positions move in bigger steps; cycle count by 1
        changeValues(forward ? step : -step);
      }
    } else if(menu_page == 3) {
      // We're on the wifi page - a single screen (status + IP), nothing to browse to.
      menu_position = 0;
    }

    // consume exactly one detent's worth from the baseline; any extra (fast spins) carries over to the next call.
    oldPosition += forward ? 3 : -3;
  }
}

// When we're in admin mode, we can edit individual values.
void changeValues(int adjust) {
  if(menu_position == 1) {
    // Change the Low value (down position)
    servos[currentPattern][0] = servos[currentPattern][0] + adjust;
   } else if (menu_position == 3) {
    // Change the High value (up position)
    servos[currentPattern][1] = servos[currentPattern][1] + adjust;
   } else if (menu_position == 5) {
    // Change the Eject value
    servos[currentPattern][2] = servos[currentPattern][2] + adjust;
   } else if (menu_position == 7) {
    // Change the test cycle count
    test_run = test_run + adjust;
   }
   
}