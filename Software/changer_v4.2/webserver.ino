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
    json += "\"active\":" + String(activeArm) + ",";
    json += "\"patterns\":[";
    for(int i = 0; i < 4; i++) {
      if(i > 0) json += ",";
      json += "\"" + String(patterns[i].steps) + "\"";
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
      if(steps[i] < '0' || steps[i] > '3') {
        request->send(400, "text/plain", "sequence must only contain digits 0-3");
        return;
      }
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