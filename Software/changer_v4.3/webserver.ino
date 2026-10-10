String processor(const String& var)
{
  // FS_* since 4.3; the old SPIFFS_* names still work for pages restored from older backups
  if(var == "FS_FREE_BYTES" || var == "SPIFFS_FREE_BYTES")
    return convertFileSize((LittleFS.totalBytes() - LittleFS.usedBytes()));
  if(var == "FS_USED_BYTES" || var == "SPIFFS_USED_BYTES")
    return convertFileSize(LittleFS.usedBytes());
  if(var == "FS_TOTAL_BYTES" || var == "SPIFFS_TOTAL_BYTES")
    return convertFileSize(LittleFS.totalBytes());
  if(var == "LISTEN_FILES")
    return listDir(LittleFS, "/", 0);

  if(var == "BUILDDATE")
    return __DATE__;
  if(var == "BUILDTIME")
    return __TIME__;
  if(var == "GETFREEHEAP")
    return convertFileSize(ESP.getFreeHeap());
  if(var == "GETTOTALHEAP")
    return convertFileSize(ESP.getHeapSize());
  if(var == "GETFREEPSRAM")
    return convertFileSize(ESP.getFreePsram());
  if(var == "GETTOTALPSRAM")
    return convertFileSize(ESP.getPsramSize());
  if(var == "PROJECT")
    return PROJECT;
  if(var == "VERSION")
    return VERSION;

  if(var == "DEBUG_LEVEL")
    return String(DEBUG);
  if(var == "I2C_ENABLED")
    return i2c_enabled ? "ON" : "OFF";
  if(var == "I2C_TOGGLE_LABEL")
    return i2c_enabled ? "Disable I2C" : "Enable I2C";
  if(var == "I2C_TOGGLE_ACTION")
    return i2c_enabled ? "/i2c_off" : "/i2c_on";
  if(var == "I2C_CHECKED")
    return i2c_enabled ? "checked" : "";
  if(var == "SENSOR_ENABLED")
    return sensor_enabled ? "ON" : "OFF";
  if(var == "SENSOR_DEBOUNCE")
    return String(debounceDelay);
  if(var == "BUZZER_ENABLED")
    return BUZZER ? "ON" : "OFF";
  if(var == "BUZZER_TOGGLE_LABEL")
    return BUZZER ? "Disable Buzzer" : "Enable Buzzer";
  if(var == "BUZZER_TOGGLE_ACTION")
    return BUZZER ? "/buzzer_off" : "/buzzer_on";
  if(var == "BUZZER_CHECKED")
    return BUZZER ? "checked" : "";
  if(var == "BUZZER_FREQ")
    return String((int)buzzerNote);
  if(var == "SENSOR_TOGGLE_LABEL")
    return sensor_enabled ? "Disable Sensor" : "Enable Sensor";
  if(var == "SENSOR_TOGGLE_ACTION")
    return sensor_enabled ? "/sensor_off" : "/sensor_on";
  if(var == "SENSOR_CHECKED")
    return sensor_enabled ? "checked" : "";
  if(var == "I2C_ADDRESS") {
    String hex = String(i2c2_address, HEX);
    if(hex.length() < 2)
      hex = "0" + hex; // zero-pad to 2 digits, matching the OLED's I2C page
    return hex;
  }

  if(var == "CHIPMODEL")
    return String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision());
  if(var == "FLASHSIZE")
    return String(ESP.getFlashChipSize() / 1024 / 1024) + " MB";
  if(var == "SKETCHSIZE")
    return String(ESP.getSketchSize() / 1024) + " KB";
  if(var == "FREESKETCHSPACE")
    return String(ESP.getFreeSketchSpace() / 1024) + " KB";
  if(var == "IPADDRESS") {
    if(!wifi_enabled)
      return "off";
    if(!wifi_connected)
      return "connecting...";
    if(hotspot_mode())
      return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
  }

  return String();
}

String edit_processor(const String& var) {
  // We need a separate processor for when we are editing a file
  // otherwise the template system wipes out the template declarations
  if(var == "TEXTAREA_CONTENT")
    return textareaContent;
  if(var == "ALLOWED_EXTENSIONS_EDIT")
    return allowedExtensionsForEdit;

  if(var == "SAVE_PATH_INPUT") {
    if(savePath == "new.txt") {
      savePathInput = "<input type=\"text\" id=\"save_path\" name=\"save_path\" value=\"" + savePath + "\" >";
    } else {
      savePathInput = "";
    }
    return savePathInput;
  }

  // so, if we try to use '%' signs, the processor goes in to a feedback loop.
  // fortunately, using the http codes get translated by the edit screen and
  // converted back to the sign.
  return "&#37" + var + "&#37";
}

// Tracks an in-progress /scan (see that route below). scanComplete() has a known quirk:
// right after WiFi.scanNetworks(true) starts one, it can briefly still report
// WIFI_SCAN_FAILED for a moment before settling into WIFI_SCAN_RUNNING - treating every
// FAILED reading as "nothing started, kick one off" causes a retrigger loop that never
// lets the scan finish. These let /scan tell "never started" apart from "started but
// hasn't shown RUNNING yet", and give up and allow a fresh retry if one genuinely hangs.
bool wifiScanStarted = false;
unsigned long wifiScanStartTime = 0;
#define WIFI_SCAN_TIMEOUT_MS 15000

// Results of the scan run just before the hotspot came up, ready for the join page's
// first request - so the list appears instantly. Empty once used (or if there's none).
String prescanJson = "";

// Turns the finished scan's n results into the JSON the join page expects, and frees
// them. Used by /scan, and by the scan run just before the hotspot starts (see
// start_hotspot() in wifi.ino).
String build_scan_json(int n) {
  // The same network's SSID is often seen more than
  // once (multiple APs/mesh nodes sharing one name) - keep only the strongest
  // signal per SSID, then sort strongest-first, the way a phone's WiFi picker
  // would show it.
  const int MAX_NETWORKS = 32;
  String ssids[MAX_NETWORKS];
  int32_t rssis[MAX_NETWORKS];
  bool secures[MAX_NETWORKS];
  int unique = 0;

  for(int i = 0; i < n && unique < MAX_NETWORKS; i++) {
    String s = WiFi.SSID(i);
    if(s.length() == 0)
      continue; // hidden network broadcasting no name - nothing to offer here

    int existing = -1;
    for(int j = 0; j < unique; j++) {
      if(ssids[j] == s) {
        existing = j;
        break;
      }
    }
    if(existing == -1) {
      ssids[unique] = s;
      rssis[unique] = WiFi.RSSI(i);
      secures[unique] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      unique++;
    } else if(WiFi.RSSI(i) > rssis[existing]) {
      rssis[existing] = WiFi.RSSI(i);
      secures[existing] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
  }

  // Insertion sort, strongest (least negative dBm) first - unique is at most
  // MAX_NETWORKS, nowhere near large enough to need anything fancier.
  for(int i = 1; i < unique; i++) {
    String sSsid = ssids[i];
    int32_t sRssi = rssis[i];
    bool sSecure = secures[i];
    int j = i - 1;
    while(j >= 0 && rssis[j] < sRssi) {
      ssids[j + 1] = ssids[j];
      rssis[j + 1] = rssis[j];
      secures[j + 1] = secures[j];
      j--;
    }
    ssids[j + 1] = sSsid;
    rssis[j + 1] = sRssi;
    secures[j + 1] = sSecure;
  }

  String json = "{\"status\":\"done\",\"networks\":[";
  for(int i = 0; i < unique; i++) {
    if(i > 0)
      json += ",";
    json += "{\"ssid\":\"" + jsonEscape(ssids[i]) + "\",\"rssi\":" + String(rssis[i])
          + ",\"secure\":" + (secures[i] ? "true" : "false") + "}";
  }
  json += "]}";

  WiFi.scanDelete();
  return json;
}

void setupAsyncServer() {
  server.on("/manage", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    request->send(LittleFS, "/manage.html", String(), false, processor);
  });

 
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    rebooting = !Update.hasError();
    // Served from the firmware, not the copy on the filesystem: that copy only gets
    // refreshed by Fix after the new firmware is running, so it could be a version
    // behind (e.g. still pointing Return at the old /manage page).
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html",
      rebooting ? ok_html : failed_html);

    response->addHeader("Connection", "close");
    request->send(response);
  },
  [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
  {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!index) {
      weblog.print(F("Updating: "));
      weblog.println(filename.c_str());

      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000))
	    {
        Update.printError(Serial);
      }
    }
    if(!Update.hasError())
	  { if(Update.write(data, len) != len) {
        Update.printError(Serial);
      }
    }
    if(final) {
      if(Update.end(true)) {
        weblog.print(F("The update is finished: "));
        weblog.println(convertFileSize(index + len));
      } else {
        Update.printError(Serial);
      }
    }
  });


  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    request->send(200);
  }, uploadFile);


  server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String inputMessage = request->getParam("edit_path")->value();

    if(inputMessage =="new") {
      textareaContent = "";
      savePath = "new.txt";
    } else {
      savePath = inputMessage;
      textareaContent = readFile(LittleFS, inputMessage.c_str());
    }
    request->send(LittleFS, "/edit.html", String(), false, edit_processor);
  });


  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String inputMessage = "";
    if (request->hasParam("edit_textarea")) {
      inputMessage = request->getParam("edit_textarea")->value();
    }
    if (request->hasParam("save_path")) {
      savePath = request->getParam("save_path")->value();
    }
    writeFile(LittleFS, savePath.c_str(), inputMessage.c_str());

    request->redirect("/manage");
  });


  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String inputMessage = "/" + request->getParam("delete_path")->value();

    if(inputMessage !="choose") {
      LittleFS.remove(inputMessage.c_str());
    }
    request->redirect("/manage");
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String inputMessage = "/" + request->getParam("download_path")->value();

    
    request->send(LittleFS, inputMessage, "application/octet-stream", true);

    request->redirect("/manage");
  });


  server.on("/format", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!WebActions::push(WebActions::FORMAT))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "text/plain", "Formatting and restarting.");
  });

  // just an exception for our ini file so we don't accidentally share our config with the world
  server.on("/config.ini", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(403);
  });

  // join.html has no %VAR% placeholders of its own to substitute, and its JS/CSS
  // contain plain '%' characters (CSS percentages, the scan list's signal-strength
  // display) that the template processor would misread as the START of one - it
  // scans for the NEXT '%' to close the pair, which could be a stray one later in
  // this same content, or a real %VAR% elsewhere on the page, silently deleting
  // everything in between (see the warning on processor()'s fallback, above - this
  // is that exact failure mode). So this gets its own explicit route, serving it
  // with no processor at all, registered before serveStatic's global
  // .setTemplateProcessor() (further down) can apply to it - that covers every way
  // to reach this file, including a direct /join.html request (e.g. the "WiFi
  // Setup" link on index.html), not just when it's reached via "/" below.
  server.on("/join.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/join.html", "text/html");
  });

  // Root page: the WiFi join page while we're our own access point (nothing configured,
  // or couldn't reach what was configured), the normal status page otherwise. Registered
  // before serveStatic's default-file handling below, so it takes priority for "/" while
  // every other path (including a direct request for /index.html) still falls through to
  // serveStatic as normal.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(hotspot_mode()) {
      request->send(LittleFS, "/join.html", "text/html"); // no processor - see the /join.html route above
    } else {
      sendIndexPage(request); // adds the "pages out of date" banner when needed - see pages.ino
    }
  });

  // Changes the web (HTTP Basic auth) password from the System tab. Needs the current
  // password in the form as well as the browser's login, so a page elsewhere can't
  // change it through the browser's cached credentials. Takes effect immediately -
  // the browser's next request will ask for the new one.
  server.on("/password", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String current = request->hasParam("current", true) ? request->getParam("current", true)->value() : "";
    String next = request->hasParam("new", true) ? request->getParam("new", true)->value() : "";

    String error = "";
    if(current != http_password) {
      error = F("The current password is wrong.");
    } else if(next.length() < 4 || next.length() > 64) {
      error = F("The new password must be 4 to 64 characters.");
    } else {
      // printable ASCII only: config.ini is one setting per line, and browsers don't
      // agree on how to encode anything else in a Basic auth login
      for(unsigned int i = 0; i < next.length(); i++) {
        if(next[i] < 0x20 || next[i] > 0x7E) {
          error = F("Use only letters, numbers, spaces and standard punctuation.");
          break;
        }
      }
    }

    if(error.length()) {
      weblog.println(F("Web password change rejected."));
      request->send(400, "text/html", messagePage("Password not changed", error, false));
      return;
    }

    http_password = next;
    ArduinoOTA.setPassword(http_password.c_str()); // network uploads use the same password
    save_config(LittleFS, "/config.ini");
    weblog.println(F("Web password changed."));
    request->send(200, "text/html", messagePage("Password changed",
      "Your browser will ask you to log in again - use the new password.", true));
  });

  // Same page, asked for by name (bookmarks, links from other pages). Served even in
  // hotspot mode, so restore is still reachable there via /index.html#status.
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendIndexPage(request);
  });

  // The Fix button on the "pages out of date" banner. The rewrite itself happens in
  // loop() - see pages.ino.
  server.on("/fix_pages", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    pagesFixPending = true;
    request->send(200, "text/html", String(
      F("<!DOCTYPE HTML><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"3;url=/\"><title>" PROJECT "</title>"
        "<style>body{background:#f7f7f7;font-family:Arial,sans-serif;padding:20px}</style></head>"
        "<body><h2>Updating web pages...</h2><p>Back in a few seconds.</p></body></html>")));
  });

  // Submits new WiFi credentials from the join page above. No auth - if you can see
  // this unit's own hotspot to get here, you're already as "in" as auth would protect
  // against, and the join page itself has no auth either.
  server.on("/join", HTTP_POST, [](AsyncWebServerRequest *request) {
    // No auth needed for a factory-fresh unit (still on the default ssid) - that's the
    // whole point of this flow working without friction. Once real credentials exist,
    // changing them needs the same admin login as /manage, so someone who's already
    // connected can still switch networks, but a random visitor to the open join page
    // can't overwrite a working setup.
    if(ssid != PROJECT && !request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("ssid", true) || request->getParam("ssid", true)->value().length() == 0) {
      request->send(400, "text/plain", "ssid can't be empty");
      return;
    }
    String newSsid = request->getParam("ssid", true)->value();
    String newPassword = request->hasParam("password", true)
      ? request->getParam("password", true)->value() : "";

    if(!WebActions::push(WebActions::JOIN, 0, 0, 0, 0, newSsid.c_str(), newPassword.c_str()))
      return request->send(400, "text/plain", "ssid or password too long");

    request->send(LittleFS, "/joining.html", String(), false, processor);
  });

  // Turns WiFi off entirely - always needs admin auth (unlike /join, which only
  // gates once configured), since unlike changing networks, this has no "still
  // works, just different" outcome: it always disconnects whoever's using it right
  // now, and only serial or the OLED's WiFi page can turn it back on afterward.
  server.on("/wifi_off", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    // loop() turns the radio off a moment after this response has gone out
    if(!WebActions::push(WebActions::WIFI_OFF))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "text/plain",
      "WiFi disabled. Reconnect over USB serial, or use the unit's own WiFi menu page, to turn it back on.");
  });

  // I2C settings all need admin auth, same reasoning as /join: these persist a
  // config change, not just a momentary action like /next or /press. Unlike WiFi,
  // disabling this never risks cutting off whoever's making the request - the web
  // UI runs over WiFi, entirely independent of this bus - so there's no special
  // response-ordering need here the way /wifi_off has.
  server.on("/i2c_on", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!WebActions::push(WebActions::I2C_ENABLE, 1))
      return request->send(503, "text/plain", "busy, try again");
    // JSON, not a redirect - the page updates itself in place via JS.
    request->send(200, "application/json", "{\"enabled\":true}");
  });
  server.on("/i2c_off", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!WebActions::push(WebActions::I2C_ENABLE, 0))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "application/json", "{\"enabled\":false}");
  });

  // Toggles automatic response to the arm sensor - see setSensorEnabled() in
  // arm.ino. Admin auth, same reasoning as I2C: persists a config change. No
  // response-ordering concern like /wifi_off - this doesn't affect how the web UI
  // itself is reached, only whether the sensor interrupt's response runs.
  server.on("/sensor_on", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    setSensorEnabled(true);
    request->send(200, "application/json", "{\"enabled\":true}");
  });
  server.on("/sensor_off", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    setSensorEnabled(false);
    request->send(200, "application/json", "{\"enabled\":false}");
  });

  // Saves the sensor debounce delay (ms). Admin auth, same reasoning as the other
  // sensor/I2C settings. Clamped the same as the OLED's own encoder-driven editing
  // (see changer_encoder.ino) - debounceDelay is unsigned long, so an unclamped
  // negative value here would wrap to a huge number and effectively stop the
  // sensor from ever triggering.
  server.on("/sensor_debounce", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("value")) {
      request->send(400, "text/plain", "missing value");
      return;
    }
    debounceDelay = constrain(request->getParam("value")->value().toInt(), 0, 2000);
    save_config(LittleFS, "/config.ini");
    request->send(200, "application/json", "{\"debounce\":" + String(debounceDelay) + "}");
  });

  // Buzzer controls - deliberately separate from the sensor ones above, not
  // folded into the same section, even though both relate to the same arm-switch
  // event. Admin auth on all three, same reasoning as sensor/I2C.
  server.on("/buzzer_on", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    setBuzzerEnabled(true);
    request->send(200, "application/json", "{\"enabled\":true}");
  });
  server.on("/buzzer_off", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    setBuzzerEnabled(false);
    request->send(200, "application/json", "{\"enabled\":false}");
  });
  server.on("/buzzer_freq", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("value")) {
      request->send(400, "text/plain", "missing value");
      return;
    }
    setBuzzerFrequency(request->getParam("value")->value().toDouble());
    request->send(200, "application/json", "{\"freq\":" + String((int)buzzerNote) + "}");
  });

  server.on("/i2c_address", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("addr")) {
      request->send(400, "text/plain", "missing addr");
      return;
    }
    // Hex, same as the OLED's I2C page shows it and the form below asks for it -
    // not decimal, which String::toInt() would otherwise assume.
    long addr = strtol(request->getParam("addr")->value().c_str(), NULL, 16);
    if(addr < 0x08 || addr > 0x77) {
      request->send(400, "text/plain", "address must be between 08 and 77 (hex)");
      return;
    }
    if(!WebActions::push(WebActions::I2C_ADDRESS, (int)addr))
      return request->send(503, "text/plain", "busy, try again");
    // Echo back the normalized value (zero-padded, same as %I2C_ADDRESS% below) so
    // the field shows exactly what will be saved, not just whatever was typed.
    String hex = String(addr, HEX);
    if(hex.length() < 2)
      hex = "0" + hex;
    request->send(200, "application/json", "{\"address\":\"" + hex + "\"}");
  });

  // Runs a command typed into the web console (Serial Log tab) through the same
  // interpreter Serial and I2C use. No separate response handling needed here -
  // cmdOut defaults to weblog (see commands.ino), so whatever the command prints
  // lands in the same log the console is already showing. Needs admin auth: this
  // is strictly more powerful than any single action elsewhere in the web UI (it's
  // every serial command, including WIFI, RM, REBOOT), not just one setting.
  server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("cmd")) {
      request->send(400, "text/plain", "missing cmd");
      return;
    }
    String cmd = request->getParam("cmd")->value();
    if(cmd.length() >= 128)
      return request->send(400, "text/plain", "command too long");
    if(!WebActions::push(WebActions::COMMAND, 0, 0, 0, 0, cmd.c_str(), nullptr))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "text/plain", "OK"); // its output appears in the log
  });

  // Returns everything currently held in weblog's ring buffer (see changer_v4.3.ino) -
  // the same content the physical USB serial monitor would show, as plain text,
  // oldest first. No auth - this never changes anything, same as /status.
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", weblog.getContents());
  });
  server.on("/log_clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    weblog.clear();
    request->send(200, "text/plain", "Log cleared.");
  });

  // Current LOW/HIGH/EJECT PWM values for all 4 arms, plus the shared test cycle
  // count - no auth, this only reads (same as /status).
  server.on("/servo_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"low\":[";
    for(int i = 0; i < 4; i++) {
      if(i > 0) json += ",";
      json += String(servos[i][0]);
    }
    json += "],\"high\":[";
    for(int i = 0; i < 4; i++) {
      if(i > 0) json += ",";
      json += String(servos[i][1]);
    }
    json += "],\"eject\":[";
    for(int i = 0; i < 4; i++) {
      if(i > 0) json += ",";
      json += String(servos[i][2]);
    }
    json += "],\"testRun\":" + String(test_run) + "}";
    request->send(200, "application/json", json);
  });

  // Saves new LOW/HIGH/EJECT values for one arm. Admin auth - this persists a
  // config change, same bar as the I2C settings. Clamped to 0-4095 (the PCA9685's
  // documented valid range - see setPWM()'s uint16_t parameter): servos[][] is a
  // signed int with no clamping on the OLED's own encoder-driven editing, so an
  // unclamped negative value here would wrap into a huge uint16_t and could drive
  // a servo somewhere damaging, not just fail cleanly.
  server.on("/servo_save", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("arm") || !request->hasParam("low") ||
       !request->hasParam("high") || !request->hasParam("eject")) {
      request->send(400, "text/plain", "missing arm/low/high/eject");
      return;
    }
    int arm = request->getParam("arm")->value().toInt();
    if(arm < 0 || arm > 3) {
      request->send(400, "text/plain", "arm must be 0-3");
      return;
    }
    // 1-4095, the same range servos.txt loading accepts (0 would leave it unpowered)
    int low = constrain(request->getParam("low")->value().toInt(), 1, 4095);
    int high = constrain(request->getParam("high")->value().toInt(), 1, 4095);
    int eject = constrain(request->getParam("eject")->value().toInt(), 1, 4095);
    if(!WebActions::push(WebActions::SERVO_SAVE, arm, low, high, eject, nullptr, nullptr))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "application/json",
      "{\"low\":" + String(low) + ",\"high\":" + String(high) + ",\"eject\":" + String(eject) + "}");
  });

  // Moves one arm directly to a given PWM value - see previewServo() in servos.ino.
  // No auth - momentary hardware action, same bar as /next/back/press/servo_test.
  server.on("/servo_preview", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->hasParam("arm") || !request->hasParam("value")) {
      request->send(400, "text/plain", "missing arm/value");
      return;
    }
    int arm = request->getParam("arm")->value().toInt();
    if(arm < 0 || arm > 3) {
      request->send(400, "text/plain", "arm must be 0-3");
      return;
    }
    if(!WebActions::push(WebActions::SERVO_PREVIEW, arm, request->getParam("value")->value().toInt()))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "text/plain", "OK");
  });

  server.on("/servo_test", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->hasParam("arm")) {
      request->send(400, "text/plain", "missing arm");
      return;
    }
    int arm = request->getParam("arm")->value().toInt();
    if(arm < 0 || arm > 3) {
      request->send(400, "text/plain", "arm must be 0-3");
      return;
    }
    if(!WebActions::push(WebActions::SERVO_TEST, arm, 0))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "text/plain", "Test started.");
  });

  // Same idea as /servo_test above, but all four arms in sequence rather than one -
  // see startArmTest() in arm.ino. No auth, same bar as /servo_test and the other
  // momentary hardware actions.
  server.on("/servo_test_all", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!WebActions::push(WebActions::SERVO_TEST, 0, 1))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200, "text/plain", "Test started.");
  });

  // Saves the shared test cycle count. Admin auth, same reasoning as /servo_save.
  server.on("/servo_testrun", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!request->hasParam("value")) {
      request->send(400, "text/plain", "missing value");
      return;
    }
    test_run = constrain(request->getParam("value")->value().toInt(), 1, 50);
    save_config(LittleFS, "/config.ini");
    request->send(200, "application/json", "{\"testRun\":" + String(test_run) + "}");
  });

  // Scans for nearby WiFi networks for the join page's "Scan for networks" button.
  // No auth - matches /join.html itself; this only reads, never changes anything.
  //
  // One endpoint handles start/poll/results, since the scan itself has to run async
  // (WiFi.scanNetworks(true)) rather than blocking this handler for the 2-4 seconds
  // a scan takes - a blocking call here risks a watchdog timeout and stalls every
  // other request while it runs. The page's JS just polls this every second or so
  // and acts on whichever status comes back:
  //   {"status":"started"}             - kicked off just now, poll again shortly
  //   {"status":"running"}             - still in progress, poll again shortly
  //   {"status":"done","networks":[…]} - finished; each entry is
  //                                      {"ssid":…, "rssi":…, "secure":true|false}
  //
  // Works in hotspot mode too: the hotspot runs as WIFI_AP_STA (see setup_AP() in
  // wifi.ino), so scanning doesn't have to change the radio mode.
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    // The list scanned just before the hotspot started, if it hasn't been shown yet.
    if(prescanJson.length()) {
      request->send(200, "application/json", prescanJson);
      prescanJson = "";
      return;
    }
    // ?cached=1 (the join page on load) only wants that pre-loaded list - don't start
    // a scan just because someone opened the page.
    if(request->hasParam("cached")) {
      request->send(200, "application/json", "{\"status\":\"none\"}");
      return;
    }

    int n = WiFi.scanComplete();

    if(n == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "{\"status\":\"running\"}");
      return;
    }

    if(n == WIFI_SCAN_FAILED) {
      // Could genuinely mean "nothing started yet" OR "one we started is still
      // settling into RUNNING" (see the comment on wifiScanStarted above) - only
      // (re)trigger if we've never started one, or the one we did start has clearly
      // timed out rather than just not having reported RUNNING yet.
      if(!wifiScanStarted || millis() - wifiScanStartTime > WIFI_SCAN_TIMEOUT_MS) {
        WiFi.scanNetworks(true);
        wifiScanStarted = true;
        wifiScanStartTime = millis();
        request->send(200, "application/json", "{\"status\":\"started\"}");
      } else {
        request->send(200, "application/json", "{\"status\":\"running\"}");
      }
      return;
    }

    // n >= 0: results are ready.
    wifiScanStarted = false;
    request->send(200, "application/json", build_scan_json(n));
  });

  // Current arm/pattern state for the index page's live graphic. No auth - same
  // openness as index.html itself, and nothing here is sensitive or destructive.
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    int activeArm = -1;
    if(patterns[currentPattern].length && servonum >= 1) {
      activeArm = patterns[currentPattern].steps[servonum-1] - '0'; // step index -> arm index
    } else if(!patterns[currentPattern].length && servonum >= 0) {
      activeArm = servonum; // manual/loading mode - servonum already IS the arm index
    }

    String json = "{";
    json += "\"pattern\":" + String(currentPattern) + ",";
    json += "\"length\":" + String(patterns[currentPattern].length) + ",";
    json += "\"step\":" + String(patterns[currentPattern].length ? servonum : 0) + ",";
    json += "\"active\":" + String(activeArm) + ",";
    json += "\"patterns\":[";
    for(int i = 0; i < 4; i++) {
      if(i > 0) json += ",";
      String logical = patterns[i].steps; // convert stored 0-3 to the logical 1-4 the unit itself displays
      for(unsigned int j = 0; j < logical.length(); j++) {
        logical[j] = logical[j] + 1;
      }
      json += "\"" + logical + "\"";
    }
    json += "]";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Triggers the same action pressing arm button ?arm=0-3 would on the device itself.
  server.on("/press", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("arm")) {
      int arm = request->getParam("arm")->value().toInt();
      if(arm >= 0 && arm <= 3 && !WebActions::push(WebActions::PRESS, arm))
        return request->send(503, "text/plain", "busy, try again");
    }
    request->send(200);
  });

  // Switches the active pattern, same as holding a number button 2s on the main page.
  server.on("/switch", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("pattern")) {
      int n = request->getParam("pattern")->value().toInt();
      if(n >= 0 && n <= 3 && !WebActions::push(WebActions::SWITCH, n))
        return request->send(503, "text/plain", "busy, try again");
    }
    request->send(200);
  });

  // Steps the active pattern back/forward one position - see switchRodsBack() and
  // switchRods() in servos.ino. switchRods() is the same function the arm sensor
  // itself calls (via operateArm() in arm.ino) for normal forward progression; this
  // just calls it directly rather than waiting for a physical trigger. Both only
  // meaningful with an active sequence (not manual/loading mode), same condition the
  // sequence-position display itself is hidden under.
  server.on("/back", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!WebActions::push(WebActions::BACK))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200);
  });
  server.on("/next", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!WebActions::push(WebActions::NEXT))
      return request->send(503, "text/plain", "busy, try again");
    request->send(200);
  });

  server.on("/setpattern", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->hasParam("n") || !request->hasParam("steps")) {
      request->send(400, "text/plain", "missing n or steps");
      return;
    }

    int n = request->getParam("n")->value().toInt();
    String steps = request->getParam("steps")->value();
    String error = validatePatternSteps(n, steps);
    if(error.length()) {
      request->send(400, "text/plain", error);
      return;
    }
    if(!WebActions::push(WebActions::SET_PATTERN, n, 0, 0, 0, steps.c_str(), nullptr))
      return request->send(503, "text/plain", "busy, try again");

    request->send(200);
  });

  // /backup and /restore - see backup.ino
  setupBackupRoutes();

  // Default share out any file we have in LittleFS.
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setTemplateProcessor(processor);

  server.onNotFound(notFound);
  server.begin();
}


// In hotspot mode every unknown address - including the connectivity checks phones and
// laptops make right after joining (/generate_204, /hotspot-detect.html,
// /connecttest.txt...) - is redirected to the join page, which is what makes them pop
// it up automatically. See captive_portal_loop() in wifi.ino.
void notFound(AsyncWebServerRequest *request) {
  if(hotspot_mode()) {
    if(DEBUG) {
      // shows whether a device's connectivity check is reaching us at all
      weblog.print(F("Captive redirect: "));
      weblog.print(request->host());
      weblog.println(request->url());
    }
    request->redirect("http://" + WiFi.softAPIP().toString() + "/");
    return;
  }
  request->send(404, "text/plain", "Page not found");
}

String listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  String listenFiles = "";

  File root = fs.open(dirname);
  String fail = "";
  if(!root) {
    fail = " the library cannot be opened";
    return fail;
  }
  if(!root.isDirectory()) {
    fail = " this is not a library";
    return fail;
  }

  File file = root.openNextFile();
  while(file) {
    
      listenFiles += "\n            <tr>\n              <td id=\"first_td_th\">";
      listenFiles += "<a href='/";
      listenFiles += file.name();
      listenFiles += "'>";
      listenFiles += file.name();
      listenFiles += "</a>";

      listenFiles += "</td>\n              <td>Size: ";
      listenFiles += convertFileSize(file.size());
      listenFiles += "</td>\n              <td id='center_td'>";
      listenFiles += "<input type='button' onclick='window.location.href=\"/download?download_path=";
      listenFiles += file.name();
      listenFiles += "\";' value='Download' download />";
      listenFiles += "</td>\n              <td id='center_td'>";
      listenFiles += "<input type='button' onclick='window.location.href=\"/edit?edit_path=";
      listenFiles += file.name();
      listenFiles += "\";' value='Edit' />";
      listenFiles += "</td>\n              <td id='center_td'>";
      listenFiles += "<input type='button' onclick='window.location.href=\"/delete?delete_path=";
      listenFiles += file.name();
      listenFiles += "\";' value='Delete' />";
      listenFiles += "</td>\n            </tr>\n";
    
    file = root.openNextFile();

  }
  return listenFiles;  
}

String readFile(fs::FS &fs, String path) {
  String fileContent = "";
  File file = fs.open("/" + path);

  if(!file || file.isDirectory()) {
    weblog.print(path);
    weblog.println(F(": File Failed to open"));
    return fileContent;
  }

  while(file.available()) {
    fileContent+=String((char)file.read());
  }
  file.close();
  return fileContent;
}

void writeFile(fs::FS &fs, String path, const char * message)
{
  File file = fs.open("/" + path, FILE_WRITE);
  if(!file) {
    weblog.println(F("Write Failed"));
    return;
  }
  file.print(message);
  file.close();

  if(path == "config.ini") {
    // if we just edited the config.ini file, reload it.
    load_config(LittleFS, "/config.ini");
  }
}

void uploadFile(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) 
{
  if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
    return request->requestAuthentication();
  }
  if(!index) {
    request->_tempFile = LittleFS.open("/" + filename, "w");
  }
  if(len) {
    request->_tempFile.write(data, len);
  }
  if(final) {
    request->_tempFile.close();
    request->redirect("/manage");
  }
}

String convertFileSize(const size_t bytes)
{
  if(bytes < 1024) {
    return String(bytes) + " B";
  } else if (bytes < 1048576) {
    return String(bytes / 1024.0) + " kB";
  } else if (bytes < 1073741824) {
    return String(bytes / 1048576.0) + " MB";
  }
  return String(bytes / 1073741824.0) + " GB";
}

// Escapes a string for safe embedding in a hand-built JSON response (see /scan) -
// this codebase builds its JSON by concatenation rather than pulling in a JSON
// library, which is fine for our own fixed field names/values, but a network's SSID
// is free text someone else chose and could contain a quote or backslash.
String jsonEscape(const String &in)
{
  String out;
  out.reserve(in.length());
  for(unsigned int i = 0; i < in.length(); i++) {
    char c = in[i];
    if(c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if(c == '\n') {
      out += "\\n";
    } else if(c == '\r') {
      out += "\\r";
    } else if((uint8_t)c < 0x20) {
      // other control characters - skip rather than emit something that would
      // still break JSON.parse
      continue;
    } else {
      out += c;
    }
  }
  return out;
}