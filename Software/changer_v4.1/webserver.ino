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