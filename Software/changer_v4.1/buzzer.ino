

void setup_buzzer() {
  if(DEBUG)
    Serial.println(F("Configuring Buzzer."));
    
  pinMode(BUZZER_PIN, OUTPUT);
}

void check_tone() {
  if(tone_off && millis() > tone_off) {
    // turn off the buzzer
    tone(BUZZER_PIN, 0, 0);
    tone_off = 0;
  }
}

void tone(byte pin, double freq, int tone_length) {
  ledcSetup(0, 2000, 16); // setup beeper
  ledcAttachPin(pin, 1); // attach beeper
  ledcWriteTone(1, freq); // play tone
  tone_off = millis() + tone_length;
}
