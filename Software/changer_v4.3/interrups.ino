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

 // what happens when the carriage goes past the sensor.
 // Kept tiny (interrupts have to be): count the edge, and treat it as a new pass only
 // if the sensor has been quiet for debounceDelay beforehand. A single pass can
 // produce dozens of edges over most of a second (the carriage's underside isn't one
 // clean surface), so the window restarts on EVERY edge: however long the chatter
 // lasts, it's one pass, and the next pass counts as soon as the sensor has been
 // quiet for debounceDelay - so the setting can stay short and fast knitting still
 // registers. Timed here, from the edge itself, so a busy loop() can't affect it -
 // see process_sensor() in arm.ino for what happens next.
 void IRAM_ATTR arm_sensor_handler() {
  unsigned long now = millis();
  sensorEdges++;
  if(now - lastEdgeAt > debounceDelay)
    sensorPasses++;
  lastEdgeAt = now;
 }