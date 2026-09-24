/* Define all the interrupts and their actions
 *  
 *  hey, fun fact, one interrupt function per interrupt apparently.  Yay!
 */

 

 void setup_interrupts() {
  if(DEBUG)
    Serial.println(F("Configuring Interrupts."));

  pinMode(ARM, INPUT);
    
  //attachInterrupt(digitalPinToInterrupt(armPin), arm_sensor_handler, RISING);
  // Because the IR LED is on all the time, we get a bit more response if it's FALLING.
  attachInterrupt(digitalPinToInterrupt(ARM), arm_sensor_handler, FALLING);
 }

 // what happens when the carriage goes past the sensor
 // well, turns out interrupts have to be tiny, or they cause the system to crash.  yay!
 void IRAM_ATTR arm_sensor_handler() {

  armTrigger = true;

 }
