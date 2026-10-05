String processor(const String& var)
{
  if(var == "SPIFFS_FREE_BYTES")
    return convertFileSize((SPIFFS.totalBytes() - SPIFFS.usedBytes()));
  if(var == "SPIFFS_USED_BYTES")
    return convertFileSize(SPIFFS.usedBytes());
  if(var == "SPIFFS_TOTAL_BYTES")
    return convertFileSize(SPIFFS.totalBytes());
  if(var == "LISTEN_FILES")
    return listDir(SPIFFS, "/", 0);

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
    if(WiFi.getMode() == WIFI_AP)
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

void setupAsyncServer() {
  server.on("/manage", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    request->send(SPIFFS, "/manage.html", String(), false, processor);
  });

 
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    rebooting = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(
      SPIFFS,
      rebooting ? "/ok.html" : "/failed.html",
      "text/html"
    );

    response->addHeader("Connection", "close");
    request->send(response);
  },
  [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
  {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    if(!index) {
      Serial.print(F("Updating: "));
      Serial.println(filename.c_str());

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
        Serial.print(F("The update is finished: "));
        Serial.println(convertFileSize(index + len));
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
      textareaContent = readFile(SPIFFS, inputMessage.c_str());
    }
    request->send(SPIFFS, "/edit.html", String(), false, edit_processor);
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
    writeFile(SPIFFS, savePath.c_str(), inputMessage.c_str());

    request->redirect("/manage");
  });


  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String inputMessage = "/" + request->getParam("delete_path")->value();

    if(inputMessage !="choose") {
      SPIFFS.remove(inputMessage.c_str());
    }
    request->redirect("/manage");
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    String inputMessage = "/" + request->getParam("download_path")->value();

    
    request->send(SPIFFS, inputMessage, "application/octet-stream", true);

    request->redirect("/manage");
  });


  server.on("/format", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    SPIFFS.format();
    request->send(200);
    ESP.restart();
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
    request->send(SPIFFS, "/join.html", "text/html");
  });

  // Root page: the WiFi join page while we're our own access point (nothing configured,
  // or couldn't reach what was configured), the normal status page otherwise. Registered
  // before serveStatic's default-file handling below, so it takes priority for "/" while
  // every other path (including a direct request for /index.html) still falls through to
  // serveStatic as normal.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(WiFi.getMode() == WIFI_AP) {
      request->send(SPIFFS, "/join.html", "text/html"); // no processor - see the /join.html route above
    } else {
      request->send(SPIFFS, "/index.html", String(), false, processor);
    }
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

    applyWifiCredentials(newSsid, newPassword);

    request->send(SPIFFS, "/joining.html", String(), false, processor);
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
  // This only runs while already connected over STA - scanning from pure AP mode
  // needs the radio in WIFI_AP_STA first, which isn't worth the complexity for a
  // feature that's only really useful when you're already on the network you'd be
  // switching away from.
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
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

    // n >= 0: results are ready. The same network's SSID is often seen more than
    // once (multiple APs/mesh nodes sharing one name) - keep only the strongest
    // signal per SSID, then sort strongest-first, the way a phone's WiFi picker
    // would show it.
    wifiScanStarted = false;
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
    request->send(200, "application/json", json);
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
      webPressButton(request->getParam("arm")->value().toInt());
    }
    request->send(200);
  });

  // Switches the active pattern, same as holding a number button 2s on the main page.
  server.on("/switch", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("pattern")) {
      int n = request->getParam("pattern")->value().toInt();
      if(n >= 0 && n <= 3) {
        change_program(n);
      }
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
    if(patterns[currentPattern].length) {
      switchRodsBack();
    }
    request->send(200);
  });
  server.on("/next", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(patterns[currentPattern].length) {
      switchRods();
    }
    request->send(200);
  });

  // Sets one pattern's step sequence directly - ?n=0-3&steps=0123 (digits 0-3 only).
  // Re-syncs the arm if the pattern being edited is the currently active one.
  server.on("/setpattern", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->hasParam("n") || !request->hasParam("steps")) {
      request->send(400, "text/plain", "missing n or steps");
      return;
    }

    int n = request->getParam("n")->value().toInt();
    if(n < 0 || n > 3) {
      request->send(400, "text/plain", "n must be 0-3");
      return;
    }

    String steps = request->getParam("steps")->value();
    if(steps.length() > MAX_PATTERN_LENGTH) {
      request->send(400, "text/plain", "sequence too long");
      return;
    }
    for(unsigned int i = 0; i < steps.length(); i++) {
      if(steps[i] < '1' || steps[i] > '4') {
        request->send(400, "text/plain", "sequence must only contain digits 1-4");
        return;
      }
    }

    // convert the logical 1-4 the page shows back to the stored 0-3 array index.
    for(unsigned int i = 0; i < steps.length(); i++) {
      steps[i] = steps[i] - 1;
    }

    steps.toCharArray(patterns[n].steps, MAX_PATTERN_LENGTH+1);
    patterns[n].length = steps.length();
    save_patterns(SPIFFS, "/patterns.txt");

    if(n == currentPattern) {
      // re-sync the physical arm to match the freshly edited sequence.
      for (int x = 0; x < 4; x++) {
        moveServo(x, 0);
      }
      if(patterns[currentPattern].length) {
        servonum = 1;
        moveServo(patterns[currentPattern].steps[0] - '0', 1);
      } else {
        servonum = -1;
      }
    }

    request->send(200);
  });

  // Default share out any file we have in SPIFFS.
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setTemplateProcessor(processor);

  server.onNotFound(notFound);
  server.begin();
}


void notFound(AsyncWebServerRequest *request) {
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
    Serial.print(path);
    Serial.println(F(": File Failed to open"));
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
    Serial.println(F("Write Failed"));
    return;
  }
  file.print(message);
  file.close();

  if(path == "config.ini") {
    // if we just edited the config.ini file, reload it.
    load_config(SPIFFS, "/config.ini");
  }
}

void uploadFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) 
{
  if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
    return request->requestAuthentication();
  }
  if(!index) {
    request->_tempFile = SPIFFS.open("/" + filename, "w");
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