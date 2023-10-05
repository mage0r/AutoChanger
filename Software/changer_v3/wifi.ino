const char* host = "AutoChanger";
//const char* ssid = "magenet";
//const char* password = "worldsgreates";

const char* start_update = 
"<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update'>"
  "<input type='submit' value='Update'>"
"</form>";

const char* serverIndex = 
"<html>"
  "<head>"
    "<meta http-equiv='refresh' content='5'/>"
    "<title>ESP32 Demo</title>"
    "<style>body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }"
    "</style>"
  "</head>"
  "<body>"
    "<h1>Hello from ESP32!</h1>"
    "<p>Uptime: </p>"
    "<img src=\"/test.svg\" />"
  "</body>"
"</html>";

void setup_wifi() {

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP    
    // put your setup code here, to run once:
    Serial.begin(115200);
    
    //reset settings - wipe credentials for testing
    //wm.resetSettings();

    wm.setConfigPortalBlocking(false);

    //automatically connect using saved credentials if they exist
    //If connection fails it starts an access point with the specified name
    if(wm.autoConnect("AutoChanger")){
        Serial.println("connected...yeey :)");
    }
    else {
        Serial.println("Configportal running");
    }


    update_over_wifi();
}

void update_over_wifi() {
  //if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      Serial.println("Wireless conencted");
        MDNS.begin(host);
        server.on("/", HTTP_GET, []() {
          server.sendHeader("Connection", "close");
          server.send(200, "text/html", serverIndex);
        });
        server.on("/test.svg", drawGraph);
        
        server.on("/start_update", HTTP_GET, []() {
          server.sendHeader("Connection", "close");
          server.send(200, "text/html", start_update);
        });
        server.on("/update", HTTP_POST, []() {
          server.sendHeader("Connection", "close");
          server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
          ESP.restart();
        }, []() {
          HTTPUpload& upload = server.upload();
          if (upload.status == UPLOAD_FILE_START) {
            Serial.setDebugOutput(true);
            Serial.printf("Update: %s\n", upload.filename.c_str());
            if (!Update.begin()) { //start with max available size
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) { //true to set the size to the current progress
              Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            } else {
              Update.printError(Serial);
            }
            Serial.setDebugOutput(false);
          } else {
            Serial.printf("Update Failed Unexpectedly (likely broken connection): status=%d\n", upload.status);
          }
        });

        
        server.begin();
        MDNS.addService("http", "tcp", 80);
    
        Serial.printf("Ready! Open http://%s.local in your browser\n", host);
     //} else {
     //   Serial.println("WiFi Failed");
     //}
}

void drawGraph() {
  String out = "";
  char temp[100];
  out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
  out += "<rect width=\"400\" height=\"150\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
  out += "<g stroke=\"black\">\n";
  int y = rand() % 130;
  for (int x = 10; x < 390; x += 10) {
    int y2 = rand() % 130;
    sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", x, 140 - y, x + 10, 140 - y2);
    out += temp;
    y = y2;
  }
  out += "</g>\n</svg>\n";

  server.send(200, "image/svg+xml", out);
}
