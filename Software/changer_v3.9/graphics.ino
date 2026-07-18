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
void common_display() {
      // The current program number 
      u8g2.setFont(u8g2_font_fub17_tn);
      u8g2.setCursor(0, 32);
      u8g2.print(currentPattern+1); // This is our program number
      
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
    u8g2.drawStr(24,25, "RES");
  }

  if(menu_position == 1) {
    u8g2.setFont(u8g2_font_t0_15b_tf);
    u8g2.drawStr(35,25, "NEW");
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(39,25, "NEW");
  }

  u8g2.setFontDirection(0);

  // display pattern here doesn't repeat.
  // We have seven characters we can show.
  // First set offsets.
  int upper_offset = pattern[currentPattern][0];
  if(pattern[currentPattern][0] > 6) {
    if(menu_position < 6)
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
    if(menu_position == pattern[currentPattern][0]+2)
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
  u8g2.setCursor(42, 20);
  for(int i = lower_offset; i <= upper_offset; i++) {
    if(menu_position-1 == i)
      u8g2.setFont(u8g2_font_fub20_tn);
    else
      u8g2.setFont(u8g2_font_fub11_tn);
    //u8g2.print(temp_program[i]);
    u8g2.print(pattern[currentPattern][i]+1);
  }
  

  servonum = menu_position-1;
  if(servonum < 0)
    servonum = 0;

  //Serial.println(menu_position);
}

void admin_menu(String menu_name, int admin) {
        // display the back and forward icons.
        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(12, 20, "M");
        u8g2.drawStr(110, 20, "N");  

        
        u8g2.setFont(u8g2_font_fub11_tr);

        u8g2.setCursor(30, 17);
        u8g2.print(menu_name);

        if (menu_position % 2) {
          u8g2.drawStr(30,20, "_____");
        } else {
          u8g2.drawStr(80,20, "___");
        }

        u8g2.setCursor(80, 17);
        u8g2.print(admin);

}

// display the page for opening the pins right up to change them.
void display_open() {
  u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(10, 20, "M");

        u8g2.setFont(u8g2_font_fub11_tr);
        
        u8g2.drawStr(30,17, "OPEN");
        
        if (menu_position == 2) {
          u8g2.drawStr(30,20, "_____");
        }

        u8g2.setCursor(80, 17);
        u8g2.print(servos[currentPattern][2]);

        if (menu_position == 3) {
          u8g2.drawStr(80,20, "___");
        }

        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(110, 20, "N");
}

// All the displays
// terrible Idea.  I regret it already.
void display_page() {
  u8g2.firstPage();
  do {

      // This is used on the main page
      // displays the number pattern.
      if( menu_page == 0 ) {
        main_display();
      } else if( menu_page == 1 ) {
        // Reprogram the patterns
        program_display();
      } 
      /*
       * else if( menu_page == 2 ) {
        // Adjust the arms


      } else if( menu_page == 3 ) {
        // basic options.
        // disable the buzzer, disable errors.
        
      } else if( menu_page == 4 ) {
        // Network Config
        
      } else if( menu_page == 5 ) {
        // Update
        // check if new version is 
        
        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(12, 20, "M");

        u8g2.setFont(u8g2_font_fub11_tr);
        
        u8g2.drawStr(30,17, "LOW");
        if (menu_position == 6) {
          u8g2.drawStr(30,20, "_____");
        }

        u8g2.setCursor(80, 17);
        u8g2.print(servos[currentPattern][1]);
        if (menu_position == 7) {
          u8g2.drawStr(80,20, "___");
        }

        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(110, 20, "N");
        
      }
      */

      common_display();

  } while ( u8g2.nextPage() );

}
