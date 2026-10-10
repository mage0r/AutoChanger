// Sets new WiFi credentials, saves them, and (if WiFi is enabled) forces a fresh
// connection attempt rather than waiting on whatever was already happening.
// Shared by the serial WIFI command and the web /join page, so both behave
// identically and there's only one place that does this.
void applyWifiCredentials(String newSsid, String newPassword) {
  ssid = newSsid;
  wifi_password = newPassword;
  save_config(SPIFFS, "/config.ini");

  if(wifi_enabled) {
    // Force a reconnect with the new credentials rather than waiting on the old one.
    // WiFi.disconnect() also drops the AP if one is currently running - setup_wifi()
    // will switch the radio to WIFI_STA on its next 1-second pass.
    WiFi.disconnect(true);
    wifi_connected = false;
    wifi_counter = millis(); // fresh 30s window to connect before falling back to AP mode
  }
}

// Turns WiFi on or off, saves the setting, and handles the radio state either way -
// used by the serial WIFI_ENABLE command, the OLED's WiFi page, and the web UI's
// Disable WiFi button, so all three agree on exactly what "off" means rather than
// each reimplementing it slightly differently.
void setWifiEnabled(boolean enabled) {
  wifi_enabled = enabled;
  if(wifi_enabled) {
    wifi_counter = millis(); // fresh 30s window to connect before falling back to AP mode
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_connected = false;
  }
  save_config(SPIFFS, "/config.ini");
}

void setup_wifi() 
{
  if(ssid == PROJECT) {
    // We're not configured to connect anywhere else!
    // Fire up the AP.
    setup_AP();
    return;
  }

  weblog.print(F("Connecting to WiFi - "));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);

  if(WiFi.waitForConnectResult() != WL_CONNECTED) {
    weblog.println(F("Failed!  Try again in 1 Second."));
  } else {
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    ArduinoOTA.setHostname(PROJECT);

    // Same password as the web interface (http_password, from config.ini), so the
    // Arduino IDE asks for it before a network upload. /password updates it too.
    ArduinoOTA.setPassword(http_password.c_str());

    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        weblog.print(F("Start updating "));
        weblog.println(type);
      })
      .onEnd([]() {
        weblog.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) weblog.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) weblog.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) weblog.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) weblog.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR) weblog.println(F("End Failed"));
      });

    ArduinoOTA.begin();

    weblog.println(F("Success!"));
    weblog.print(F("IP address: "));
    weblog.println(WiFi.localIP());

    MDNS.begin(PROJECT);
    Serial.printf("Host: http://%s.local/manage\n", PROJECT);

    setupAsyncServer();

    wifi_connected = true;
  }
}

void setup_AP() {
  weblog.print(F("Configuring access point..."));

  // You can remove the password parameter if you want the AP to be open.
  // a valid password must have more than 7 characters
  if (!WiFi.softAP(PROJECT)) {
    weblog.println(F("Soft AP creation failed."));
    while(1);
  }

  weblog.println(F("Success!"));
  weblog.print(F("IP address: "));
  weblog.println(WiFi.softAPIP());

  MDNS.begin(PROJECT);
  Serial.printf("Host: http://%s.local/manager\n", PROJECT);

  setupAsyncServer();

  wifi_connected = true;
}

void ota_loop() {
  ArduinoOTA.handle();
}