#include <ESP32Encoder.h>

// Rotary encoder
ESP32Encoder encoder;

void setup_encoder() {
  if(DEBUG)
    weblog.println(F("Configuring Encoder."));
    
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
      // shift the cursor around - wraps at both ends rather than clamping.
      int maxPos = patterns[currentPattern].length+3; // add-new slot's position

      if(forward)
        menu_position++;
      else
        menu_position--;

      if(menu_position < 0)
        menu_position = maxPos;
      else if (menu_position > maxPos)
        menu_position = 0;
      
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
        else if(menu_position > 6)
          menu_position = 6; // TEST is the last field; 7 is its edit mode, entered by a press

        syncArmServo(); // reflect the newly selected field right away
      } else if (menu_position == 7) {
        changeValues(forward ? 1 : -1); // cycle count moves by 1
      } else {
        changeValues(forward ? 10 : -10); // servo positions move in bigger steps
        syncArmServo(); // reflect the adjusted value right away
      }
    } else if(menu_page == 3) {
      // We're on the wifi page - a single screen (status + IP), nothing to browse to.
      menu_position = 0;
    } else if(menu_page == 5) {
      // We're on the sensor page. Position 0 = the sensor on/off field (press
      // toggles directly, no edit submode needed for a boolean); 1 = the debounce
      // field, browsing; 2 = debounce, editing (see sensorPageMode() in button.ino
      // for how presses move between these).
      if(menu_position == 0) {
        menu_position = 1;
      } else if(menu_position == 1) {
        menu_position = 0;
      } else {
        // editing debounce - adjust by 10ms per detent. Clamped: debounceDelay is
        // unsigned long, so an unclamped decrement below 0 would wrap to a huge
        // value and effectively stop the sensor from ever triggering, not just
        // fail cleanly - and no real reason to allow more than a couple of
        // seconds either.
        long newDelay = (long)debounceDelay + (forward ? 10 : -10);
        if(newDelay < 0)
          newDelay = 0;
        if(newDelay > 2000)
          newDelay = 2000;
        debounceDelay = newDelay;
      }
    } else if(menu_page == 6) {
      // Same 3-position scheme as the sensor page above: 0 = buzzer on/off, 1 =
      // frequency browsing, 2 = frequency editing.
      if(menu_position == 0) {
        menu_position = 1;
      } else if(menu_position == 1) {
        menu_position = 0;
      } else {
        // 50Hz per detent - a coarser step than debounce's 10ms, since the
        // audible range (clamped 100-5000Hz in setBuzzerFrequency(), buzzer.ino)
        // spans a lot more ground than a reasonable debounce window does.
        double newFreq = buzzerNote + (forward ? 50 : -50);
        if(newFreq < 100)
          newFreq = 100;
        if(newFreq > 5000)
          newFreq = 5000;
        buzzerNote = newFreq;
      }
    }

    // consume exactly one detent's worth from the baseline; any extra (fast spins) carries over to the next call.
    oldPosition += forward ? 3 : -3;
  }
}

// When we're in admin mode, we can edit individual values. Clamped to the same
// ranges the web UI and servos.txt loading use: positions 1-4095 (the PCA9685's
// 12-bit range - 0 would leave the servo unpowered), test cycles 1-50.
void changeValues(int adjust) {
  if(menu_position == 1 || menu_position == 3 || menu_position == 5) {
    int field = menu_position / 2; // 1->LOW(0), 3->HIGH(1), 5->EJECT(2)
    servos[currentPattern][field] = constrain(servos[currentPattern][field] + adjust, 1, 4095);
  } else if (menu_position == 7) {
    test_run = constrain((int)test_run + adjust, 1, 50);
  }
}