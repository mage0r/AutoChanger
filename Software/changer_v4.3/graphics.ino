// The auto-changer has several specific display elements that need to be loaded in.
//
// In regular operation mode, display a rolling list of which changers to engage.
//
// In Programming mode, Reset, save, alter and add new elements.
//
// In Config mode:
// Trigger N number of cycles through all the arms (used for a burn in operation).
// Adjust heights for all the arms.

#include <U8g2lib.h>

// initiate the display.
// Full-buffer (_F_) mode: the whole 128x32 frame (512 bytes) is drawn in RAM and sent
// once, instead of page mode re-running every draw call once per 8px page.
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);

void setup_display() {
  if(DEBUG)
    weblog.print(F("Configuring Display......"));
    
  u8g2.begin();
  // Kick off the Display.
  u8g2.setFont(u8g2_font_cu12_tr);
  u8g2.clearBuffer();
  u8g2.setCursor(0,12);
  u8g2.print(F(PROJECT));
  u8g2.setCursor(0,30);
  u8g2.print(F(VERSION));
  u8g2.sendBuffer();

  if(DEBUG)
    weblog.println(F("Done."));
}

// Common display elements.
void header_display() {
      // The current program number 
      u8g2.setFont(u8g2_font_fub17_tn);
      u8g2.setCursor(0, 32);
      u8g2.print(currentPattern+1); // This is our program number
}

void footer_display(){     
      // This is the #/20 in the bottom Right.
      //if(servonum < 10) {
        u8g2.setFont(u8g2_font_6x10_tn);
        u8g2.setFontDirection(0);
        u8g2.setCursor(98, 32);
        if(!patterns[currentPattern].length) {
          // loading mode - servonum is an arm index (or -1), not a step position
          u8g2.print(F("--"));
        } else {
          if(servonum < 10)
            u8g2.print(F("0"));
          u8g2.print(servonum);
        }
        u8g2.print(F("/"));
        if(patterns[currentPattern].length < 10)
          u8g2.print(F("0"));
        u8g2.print(patterns[currentPattern].length);
      //}
}

void main_display() {
  // words are hard. Names are harderer.

  // Set the direction flag.
        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(60, 36, "O"); // arrow in the bottom middle.
        // This is the direction arrow.
        if(armCounter)
          u8g2.drawStr(0, 14, "M");
        else
          u8g2.drawStr(114, 14, "N");

        if(displayPattern[0]){
          u8g2.setFont(u8g2_font_fub11_tn);
          u8g2.setCursor(22, 20);
          u8g2.print(displayPattern[0]);
           u8g2.setFont(u8g2_font_fub14_tn);
          u8g2.print(displayPattern[1]);
          u8g2.setFont(u8g2_font_fub17_tn);
          u8g2.print(displayPattern[2]);
        }
        
        u8g2.setFont(u8g2_font_fub20_tn);
        u8g2.setCursor(60, 20);
        //u8g2.print(displayPattern[3]);
        if(patterns[currentPattern].length)
          u8g2.print((patterns[currentPattern].steps[servonum-1] - '0')+1); // +1 fixes the off by one array.
        else if(servonum >= 0) // empty loading pattern: servonum is the raised arm (0-3), or -1 for none
          u8g2.print(servonum+1);
        else
          u8g2.print("_");

        if(displayPattern[0]){
          u8g2.setFont(u8g2_font_fub17_tn);
          u8g2.setCursor(80, 20);
          u8g2.print(displayPattern[4]);
          u8g2.setFont(u8g2_font_fub14_tn);
          u8g2.print(displayPattern[5]);
          u8g2.setFont(u8g2_font_fub11_tn);
          u8g2.print(displayPattern[6]);
        }

}

void program_display() {
  // Display the sequence loaded in.
  // This screen is used when re-programming the unit.

  // display pattern here doesn't repeat.
  // Discrete pages: page 1 shows digits 1-6 (shares the screen with RES/NEW/SAV, so stays narrow);
  // every page after that has the whole screen to itself, so it holds 10 digits at a time (7-16,
  // 17-26, etc). Jumping a full page at a time means each new page resets to the left edge instead
  // of just nudging the window along.
  int current = menu_position - 2; // 1-based digit index currently focused (or length+1 on the add-new slot)
  if(current < 1)
    current = 1;
  if(current > patterns[currentPattern].length)
    current = patterns[currentPattern].length; // the add-new slot has no digit of its own to anchor on

  int lower_offset, upper_offset;
  if(current <= 6) {
    lower_offset = 1;
    upper_offset = 6;
  } else {
    int page = (current - 7) / 10;
    lower_offset = 7 + page*10;
    upper_offset = lower_offset + 9;
  }
  if(upper_offset > patterns[currentPattern].length)
    upper_offset = patterns[currentPattern].length;

  // RES/NEW/SAV only make sense - and only fit - on the first page; once we've scrolled past it,
  // give the digits the full width instead.
  if(lower_offset == 1) {
    u8g2.setFontDirection(3);

    if(menu_position == 0) {
      u8g2.setFont(u8g2_font_t0_15b_tf);
      u8g2.drawStr(27,25, "RES");
    } else {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(25,25, "RES");
    }

    if(menu_position == 1) {
      u8g2.setFont(u8g2_font_t0_15b_tf);
      u8g2.drawStr(40,25, "NEW");
    } else {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(39,25, "NEW");
    }

    if(menu_position == 2) {
      u8g2.setFont(u8g2_font_t0_15b_tf);
      u8g2.drawStr(53,25, "SAV");
    } else {
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(51,25, "SAV");
    }

    u8g2.setFontDirection(0);
  }


  // display the arrows.
  u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
  if(lower_offset > 1)
    u8g2.drawStr(0, 14, "M"); // only display this if there are more 
  if(upper_offset < patterns[currentPattern].length)
    u8g2.drawStr(114, 14, "N");
  else if(patterns[currentPattern].length < MAX_PATTERN_LENGTH) {
    // if we're at the end of the set values display a +
    if(menu_position == patterns[currentPattern].length+3)
    {
      u8g2.setCursor(105, 20);
      u8g2.setFont(u8g2_font_fub20_tn);
    } else {
      u8g2.setCursor(110, 16);
      u8g2.setFont(u8g2_font_fub11_tn);
    }
    u8g2.print("+"); // The next character. Only display if we're not at the max count.
  }

  // display the digits. Page 1 leaves room on the left for RES/NEW/SAV; later pages don't need to,
  // so the digits get to start further left and use the freed space.
  u8g2.setCursor(lower_offset == 1 ? 58 : 16, 20);
  for(int i = lower_offset; i <= upper_offset; i++) {
    if(menu_position-2 == i)
      u8g2.setFont(u8g2_font_fub20_tn);
    else
      u8g2.setFont(u8g2_font_fub11_tn);
    //u8g2.print(temp_program[i]);
    u8g2.print((patterns[currentPattern].steps[i-1] - '0')+1);
  }

}

// Page for adjusting the arm servo positions (Low/High/Eject) and the test cycle count (TEST).
// One field shown at a time; menu_position picks which (see armAdjustMode() in button_actions.ino).
void arm_adjust_display() {
  // display the back and forward icons.
  u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
  if(menu_position > 1)
    u8g2.drawStr(0, 14, "M");
  if(menu_position < 6)
  u8g2.drawStr(114, 14, "N");  

  u8g2.setFont(u8g2_font_fub11_tr);

  if(menu_position <= 1) {
    u8g2.drawStr(30,17, "LOW");
    u8g2.drawStr(80,20, "___");
    u8g2.drawStr(80,17, String(servos[currentPattern][0]).c_str());
  } else if(menu_position <= 3) {
    u8g2.drawStr(30,17, "HIGH");
    u8g2.drawStr(80,20, "___");
    u8g2.drawStr(80,17, String(servos[currentPattern][1]).c_str());
  } else if(menu_position <= 5) {
    u8g2.drawStr(30,17, "EJCT");
    u8g2.drawStr(80,20, "___");
    u8g2.drawStr(80,17, String(servos[currentPattern][2]).c_str());
  } else {
    u8g2.drawStr(30,17, "TEST");
    u8g2.drawStr(80,20, "___");
    u8g2.drawStr(80,17, String(test_run).c_str());
  }
}

// WiFi page: on/off status and the current IP address on two lines. Press toggles on/off.
void wifi_display() {
  u8g2.setFont(u8g2_font_fub11_tr);

  u8g2.setCursor(10, 14);
  u8g2.print("WIFI: ");
  u8g2.print(wifi_enabled ? "<ON>" : "<OFF>");

  if(wifi_enabled) {
    //u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(10, 30);
    if(!wifi_connected) {
      u8g2.print("connecting...");
    } else if(WiFi.getMode() == WIFI_AP) {
      u8g2.print(WiFi.softAPIP().toString());
    } else {
      u8g2.print(WiFi.localIP().toString());
    }
  }
}

void i2c_display() {
  u8g2.setFont(u8g2_font_fub11_tr);

  u8g2.setCursor(10, 14);
  u8g2.print("I2C: ");
  u8g2.print(i2c_enabled ? "<ON>" : "<OFF>");

  if(i2c_enabled) {
    u8g2.setCursor(10, 30);
    u8g2.print("Addr: 0x");
    if(i2c2_address < 0x10)
      u8g2.print("0"); // zero-pad a single hex digit, e.g. "0x08" not "0x8"
    u8g2.print(i2c2_address, HEX);
  }
}

void sensor_display() {
  // nav arrows, same style as arm_adjust_display() - position 0 is the sensor
  // field, 1/2 are both the debounce field (browsing/editing), so only two
  // "screens" to arrow between.
  u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
  if(menu_position >= 1)
    u8g2.drawStr(0, 14, "M");
  if(menu_position == 0)
    u8g2.drawStr(114, 14, "N");

  u8g2.setFont(u8g2_font_fub11_tr);

  if(menu_position == 0) {
    // no back arrow shown at position 0 (see above), so no need to clear it here.
    u8g2.setCursor(10, 14);
    u8g2.print("SENSOR:");
    u8g2.setCursor(10, 30);
    u8g2.print(sensor_enabled ? "<ON>" : "<OFF>");
  } else {
    // starts well clear of x=30 (arm_adjust_display()'s label position) for a
    // visibly clean gap after the back arrow icon at x=0 (shown whenever
    // menu_position >= 1, which is always true here), not just barely missing it.
    u8g2.setCursor(45, 14);
    u8g2.print("DEBOUNCE");
    // Square brackets instead of an underline - the screen is only 32px tall, and
    // an underline below the value (the way arm_adjust_display() does it) would
    // land off the bottom of the screen for a value already this low.
    u8g2.setCursor(10, 30);
    u8g2.print("[");
    u8g2.print(debounceDelay);
    u8g2.print("ms]");
  }
}

// Same layout as sensor_display() above - see its comments for why the label
// positions and bracket-instead-of-underline choices are what they are.
void buzzer_display() {
  u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
  if(menu_position >= 1)
    u8g2.drawStr(0, 14, "M");
  if(menu_position == 0)
    u8g2.drawStr(114, 14, "N");

  u8g2.setFont(u8g2_font_fub11_tr);

  if(menu_position == 0) {
    u8g2.setCursor(10, 14);
    u8g2.print("BUZZER:");
    u8g2.setCursor(10, 30);
    u8g2.print(BUZZER ? "<ON>" : "<OFF>");
  } else {
    u8g2.setCursor(45, 14);
    u8g2.print("FREQ");
    u8g2.setCursor(10, 30);
    u8g2.print("[");
    u8g2.print((int)buzzerNote);
    u8g2.print("Hz]");
  }
}

// FNV-1a over a block of bytes - just a cheap fingerprint, not for anything secure.
uint32_t display_hash(uint32_t h, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  for(size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

// Fingerprint of everything any page draws. display_page() only redraws when this
// changes, so a new value shown anywhere needs adding here too.
uint32_t display_state_hash() {
  uint32_t h = 2166136261UL;
  h = display_hash(h, &menu_page, sizeof(menu_page));
  h = display_hash(h, &menu_position, sizeof(menu_position));
  h = display_hash(h, &currentPattern, sizeof(currentPattern));
  h = display_hash(h, &servonum, sizeof(servonum));
  h = display_hash(h, &armCounter, sizeof(armCounter));
  h = display_hash(h, &patterns[currentPattern].length, sizeof(patterns[currentPattern].length));
  h = display_hash(h, patterns[currentPattern].steps, patterns[currentPattern].length);
  h = display_hash(h, servos[currentPattern], sizeof(servos[currentPattern]));
  h = display_hash(h, &test_run, sizeof(test_run));
  h = display_hash(h, &wifi_enabled, sizeof(wifi_enabled));
  h = display_hash(h, &wifi_connected, sizeof(wifi_connected));
  uint32_t ip = (WiFi.getMode() == WIFI_AP) ? (uint32_t)WiFi.softAPIP() : (uint32_t)WiFi.localIP();
  h = display_hash(h, &ip, sizeof(ip));
  h = display_hash(h, &i2c_enabled, sizeof(i2c_enabled));
  h = display_hash(h, &i2c2_address, sizeof(i2c2_address));
  h = display_hash(h, &sensor_enabled, sizeof(sensor_enabled));
  h = display_hash(h, &debounceDelay, sizeof(debounceDelay));
  h = display_hash(h, &BUZZER, sizeof(BUZZER));
  int freq = (int)buzzerNote;
  h = display_hash(h, &freq, sizeof(freq));
  return h;
}

#define DISPLAY_REFRESH_MS 5000 // redraw anyway this often, in case the panel glitched

// All the displays
// terrible Idea.  I regret it already.
void display_page() {
  static uint32_t lastHash = 0;
  static unsigned long lastDraw = 0;

  uint32_t h = display_state_hash();
  if(h == lastHash && millis() - lastDraw < DISPLAY_REFRESH_MS)
    return; // nothing on screen would change
  lastHash = h;
  lastDraw = millis();

  u8g2.clearBuffer();

      // This is used on the main page
      // displays the number pattern.
      if( menu_page == 0 ) {
        header_display();
        main_display();
        footer_display();
      } else if( menu_page == 1 ) {
        // Reprogram the patterns
        header_display();
        program_display();
        footer_display();
      } else if( menu_page == 2 ) {
        // Adjust the arm servo positions (High/Low/Eject) and the test cycle count.
        header_display();
        arm_adjust_display();
      } else if( menu_page == 3 ) {
        // Enable/disable WiFi and show the current IP address.
        wifi_display();
      } else if( menu_page == 4 ) {
        // Enable/disable the secondary I2C bus and show its configured address.
        i2c_display();
      } else if( menu_page == 5 ) {
        // Enable/disable automatic response to the arm sensor.
        sensor_display();
      } else if( menu_page == 6 ) {
        // Enable/disable the arm-switch buzzer and set its tone frequency.
        buzzer_display();
      }
      /*
       * else if( menu_page == 8 ) {
        // Update
        // check if new version is 
        
      }
      */

  u8g2.sendBuffer();

}