
void read_encoder() {
  long newPosition = myEnc.read();

  //Serial.print(oldPosition);
  //Serial.print(" - ");
  //Serial.println(newPosition);
  
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
      
    } else if(admin_mode) {
      // we're in the menu.

      //Serial.print(oldPosition);
      //Serial.print(" - ");
      //Serial.println(newPosition);
      
      if(oldPosition < newPosition)
        temp_menu_position++;
      else if(oldPosition > newPosition)
        temp_menu_position--;

      if(temp_menu_position <= -3){
        menu_position--;
        temp_menu_position = 0;
      } else if(temp_menu_position >= 3) {
        menu_position++;
        temp_menu_position = 0;
      }

      Serial.println(temp_menu_position);

      if(menu_position > 7)
        menu_position = 0;
      else if (menu_position <= 0)
        menu_position = 7;

      if(menu_position < 2)
        menu_page = 2;
      else if(menu_position < 4)
        menu_page = 3;
      else if (menu_position < 6)
        menu_page = 4;
      else
        menu_page = 5;
      
      
    }

    oldPosition = newPosition;
    //Serial.println(newPosition);
  }
}
