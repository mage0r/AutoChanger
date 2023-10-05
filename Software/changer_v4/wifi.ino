void setup_wifi() 
{
  if(ssid == "Web-Server") {
    // We're not configured to connect anywhere else!
    // Fire up the AP.
    setup_AP();
    return;
  }

  Serial.print(F("Connecting to WiFi - "));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);

  if(WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("Failed!  Try again in 1 Second."));
  } else {
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    ArduinoOTA.setHostname(host.c_str());

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.print(F("Start updating "));
        Serial.println(type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
      });

    ArduinoOTA.begin();

    Serial.println(F("Success!"));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());

    MDNS.begin(host);
    Serial.printf("Host: http://%s.local/manager\n", host.c_str());

    setupAsyncServer();

    wifi_enabled = true;
  }
}

void setup_AP() {
  Serial.print(F("Configuring access point..."));

  // You can remove the password parameter if you want the AP to be open.
  // a valid password must have more than 7 characters
  if (!WiFi.softAP("Web-Server")) {
    Serial.println(F("Soft AP creation failed."));
    while(1);
  }

  Serial.println(F("Success!"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.softAPIP());

  MDNS.begin(host);
  Serial.printf("Host: http://%s.local/manager\n", host.c_str());

  setupAsyncServer();

  wifi_enabled = true;
}

void ota_loop() {
  ArduinoOTA.handle();
}