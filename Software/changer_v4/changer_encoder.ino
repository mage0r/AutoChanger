
#include <ESP32Encoder.h>

// Rotary encoder
ESP32Encoder encoder;

void setup_encoder() {
  if(DEBUG)
    Serial.println(F("Configuring Encoder."));
    
  ESP32Encoder::useInternalWeakPullResistors=UP;
  encoder.attachHalfQuad(33, 32);
  encoder.setCount(0);
}

void read_encoder() {
  long newPosition = encoder.getCount();
  
  if (newPosition != oldPosition) {

    // if we're on the main menu page:
    if(menu_page == 0) {
      if(oldPosition < newPosition) {
        armCounter = 0;
      }else if(oldPosition > newPosition) {
        armCounter = 1;
      }
    } else if(menu_page == 1) {
      // We're in programming mode
      // shift the cursor around.

      if(oldPosition < newPosition)
        menu_position++;
      else if(oldPosition > newPosition)
        menu_position--;

      if(menu_position < 0)
        menu_position = 0;
      else if (menu_position >= pattern[currentPattern][0]+3) // When re-programming, max position is n+1.
        menu_position = pattern[currentPattern][0]+2;
      
    }
  }

  oldPosition = newPosition;
}

// handle when we have menu items to scroll through
void encoder_menu() {
  
}
