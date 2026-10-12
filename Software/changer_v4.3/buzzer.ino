void setup_buzzer() {
  if(DEBUG)
    weblog.println(F("Configuring Buzzer."));
    
  pinMode(BUZZER_PIN, OUTPUT);
}

// Turns the buzzer on or off and saves the setting - used by the serial
// BUZZER_ENABLE command, the OLED's buzzer page, and the web UI.
void setBuzzerEnabled(boolean enabled) {
  BUZZER = enabled;
  save_config(LittleFS, "/config.ini");
}

// Sets the buzzer's tone frequency (Hz) and saves it - same three callers as above.
// Clamped to 100-5000 Hz: below that a piezo buzzer is barely audible, above it
// starts cutting into ultrasonic territory most people (and the hardware) won't
// reproduce usefully.
void setBuzzerFrequency(double freq) {
  if(freq < 100)
    freq = 100;
  if(freq > 5000)
    freq = 5000;
  buzzerNote = freq;
  save_config(LittleFS, "/config.ini");
}

void check_tone() {
  if(tone_active && millis() - tone_start >= tone_len) {
    // turn off the buzzer
    tone(BUZZER_PIN, 0, 0);
  }
}

void tone(byte pin, double freq, int tone_length) {
  // 10-bit resolution, the same as ledcWriteTone() itself uses. It was 16-bit, and at
  // 16 bits the LEDC timer can't go above ~1.2kHz (80MHz / 65536), so setting up a
  // 2kHz channel failed - the source of "ledc_get_duty: LEDC is not initialized" in
  // the log on every beep. ledcWriteTone() then set the channel up again itself,
  // which is why the buzzer still sounded.
  ledcSetup(0, 2000, 10); // setup beeper on LEDC channel 0
  ledcAttachPin(pin, 0);  // attach to channel 0
  ledcWriteTone(0, freq); // play tone on channel 0, matching the above
  // start + duration rather than an absolute end time, so it survives millis() rollover
  tone_start = millis();
  tone_len = tone_length;
  tone_active = (freq > 0 && tone_length > 0);
}