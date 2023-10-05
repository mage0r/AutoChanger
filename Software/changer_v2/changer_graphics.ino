// The auto-changer has several specific display elements that need to be loaded in.
//
// In regular operation mode, display a rolling list of which changers to engage.
//
// In Programming mode, Reset, save, alter and add new elements.
//
// In Config mode:
// Trigger N number of cycles through all the arms (used for a burn in operation).
// Adjust heights for all the arms.

// Common display elements.
void common_display() {
      // The current program number 
      u8g2.setFont(u8g2_font_fub17_tn);
      u8g2.setCursor(0, 32);
      u8g2.print(currentPattern+1); // This is our program number
      
      // This is the #/20 in the bottom Right.
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
        
        u8g2.setFont(u8g2_font_fub11_tn);
        u8g2.setCursor(22, 20);
        u8g2.print(displayPattern[0]);
        u8g2.setFont(u8g2_font_fub14_tn);
        u8g2.print(displayPattern[1]);
        u8g2.setFont(u8g2_font_fub17_tn);
        u8g2.print(displayPattern[2]);
        
        u8g2.setFont(u8g2_font_fub20_tn);
        u8g2.setCursor(60, 20);
        //u8g2.print(displayPattern[3]);
        u8g2.print(pattern[currentPattern][servonum]+1); // +1 fixes the off by one array.
  
        u8g2.setFont(u8g2_font_fub17_tn);
        u8g2.setCursor(80, 20);
        u8g2.print(displayPattern[4]);
        u8g2.setFont(u8g2_font_fub14_tn);
        u8g2.print(displayPattern[5]);
        u8g2.setFont(u8g2_font_fub11_tn);
        u8g2.print(displayPattern[6]);
}

void program_display() {
  // Display the sequence loaded in.
  // This screen is used when re-programming the unit.

  u8g2.setFont(u8g2_font_6x10_tf);
  
  u8g2.setFontDirection(3);

  u8g2.drawStr(20,20, "RES");
  u8g2.drawStr(30,20, "NEW");

  u8g2.setFont(u8g2_font_fub11_tn);
  u8g2.setFontDirection(0);

  u8g2.setCursor(35, 20);
  
  // display pattern here doesn't repeat.
  // We have seven characters we can show.
  //for(int i = 0; i < 6;i++) {
  //  if()
  //  u8g2.print(pattern[currentPattern][servonum-i]);
  //}
  u8g2.print(displayPattern[0]);
  u8g2.print(displayPattern[1]);
  u8g2.print(displayPattern[2]);
  u8g2.print(displayPattern[3]);
  u8g2.print(displayPattern[4]);
  u8g2.setFont(u8g2_font_fub11_tn);
  u8g2.print(displayPattern[5]);
  
  //u8g2.setFont(u8g2_font_fub20_tn);
  u8g2.setCursor(92, 20);
  u8g2.print(pattern[currentPattern][servonum+1]); //always off by one...
  
  if(servonum < 20) {
    //u8g2.setCursor(104, 20);
    u8g2.print("+"); // The next character. Only display if we're not at 20 count.
  }
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

// All the displays
// terrible Idea.  I regret it already.
void display_page() {
  u8g2.firstPage();
  do {

      common_display();

      // This is used on the main page
      // displays the number pattern.
      if( menu_page == 0 ) {
        main_display();
      } else if( menu_page == 1 ) {
        program_display();
      } else if( menu_page == 2 ) {
        // Admin page 1.
        // Start Demo mode.

        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(12, 20, "M");

        u8g2.setFont(u8g2_font_fub11_tr);
        
        u8g2.drawStr(30,17, "TEST");

        if (menu_position == 0) {
          u8g2.drawStr(30,20, "_____");
        }

        u8g2.setCursor(80, 17);
        u8g2.print(test_run);

        if (menu_position == 1) {
          u8g2.drawStr(80,20, "___");
        }

        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(110, 20, "N");


      } else if( menu_page == 3 ) {
        // Admin page 2.
        // Eject the arms.
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
        
      } else if( menu_page == 4 ) {
        // Admin page 3.
        // Adjust the low position.
        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(12, 20, "M");

        u8g2.setFont(u8g2_font_fub11_tr);
        
        u8g2.drawStr(30,17, "HIGH");
        if (menu_position == 4) {
          u8g2.drawStr(30,20, "_____");
        }

        u8g2.setCursor(80, 17);
        u8g2.print(servos[currentPattern][0]);
        if (menu_position == 5) {
          u8g2.drawStr(80,20, "___");
        }

        u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
        u8g2.drawStr(110, 20, "N");
        
      } else if( menu_page == 5 ) {
        // Admin page 3.
        // Adjust the low position.
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
      
  } while ( u8g2.nextPage() );
}
