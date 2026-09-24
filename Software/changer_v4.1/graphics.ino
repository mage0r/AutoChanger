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
U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);

void setup_display() {
  if(DEBUG)
    Serial.print(F("Configuring Display......"));
    
  u8g2.begin();
  // Kick off the Display.
  u8g2.setFont(u8g2_font_cu12_tr);
  u8g2.firstPage();
  do {
    u8g2.setCursor(0,12);
    u8g2.print(F(PROJECT));
    u8g2.setCursor(0,30);
    u8g2.print(F(VERSION));
  } while ( u8g2.nextPage() );

  if(DEBUG)
    Serial.println(F("Done."));
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
        if(servonum < 10)
          u8g2.print(F("0"));
        u8g2.print(servonum);
        u8g2.print(F("/"));
        if(pattern[currentPattern][0] < 10)
          u8g2.print(F("0"));
        u8g2.print(pattern[currentPattern][0]);
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
        if(pattern[currentPattern][0])
          u8g2.print(pattern[currentPattern][servonum]+1); // +1 fixes the off by one array.
        else if(servonum) // This is the empty loading pattern.  Only 4 possible answers
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

  // display pattern here doesn't repeat.
  // We have seven characters we can show.
  // First set offsets.
  int upper_offset = pattern[currentPattern][0];
  if(pattern[currentPattern][0] > 6) {
    if(menu_position < 7)
      upper_offset = 6;
  }

  int lower_offset = upper_offset - 5;
  if(upper_offset < 6)
    lower_offset = 1;


  // display the arrows.
  u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
  if(lower_offset > 1)
    u8g2.drawStr(0, 14, "M"); // only display this if there are more 
  if(upper_offset < pattern[currentPattern][0])
    u8g2.drawStr(114, 14, "N");
  else if(pattern[currentPattern][0] < 20) {
    // if we're at the end of the set values display a +
    if(menu_position == pattern[currentPattern][0]+3)
    {
      u8g2.setCursor(105, 20);
      u8g2.setFont(u8g2_font_fub20_tn);
    } else {
      u8g2.setCursor(110, 16);
      u8g2.setFont(u8g2_font_fub11_tn);
    }
    u8g2.print("+"); // The next character. Only display if we're not at 20 count.
  }

  // display the digits.
  u8g2.setCursor(58, 20);
  for(int i = lower_offset; i <= upper_offset; i++) {
    if(menu_position-2 == i)
      u8g2.setFont(u8g2_font_fub20_tn);
    else
      u8g2.setFont(u8g2_font_fub11_tn);
    //u8g2.print(temp_program[i]);
    u8g2.print(pattern[currentPattern][i]+1);
  }
  

  servonum = menu_position-2;
  if(servonum < 0)
    servonum = 0;

  //Serial.println(menu_position);
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

// All the displays
// terrible Idea.  I regret it already.
void display_page() {
  u8g2.firstPage();
  do {

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
      }
      /*
       * else if( menu_page == 4 ) {
        // basic options.
        // disable the buzzer, disable errors.
        
      } else if( menu_page == 5 ) {
        // Update
        // check if new version is 
        
      }
      */

  } while ( u8g2.nextPage() );

}