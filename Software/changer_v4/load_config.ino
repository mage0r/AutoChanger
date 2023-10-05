void setup_config() {
  // load the config from the config.ini file on the SPIFFS file system.
  // if the file doesn't exist, load defaults.

  Serial.println(F("Loading Defaults"));

  ssid = "ESP32-Webserver";
  wifi_password = "";

  http_username = "admin";
  http_password = "admin";

  host = "esp32-filemanager";

  allowedExtensionsForEdit = "txt, h, htm, html, css, cpp, js, ini";

  jquery = "/jquery-3.6.3.min.js";

}

void load_config(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Config: "));
  Serial.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      Serial.println(F("- failed to open file for reading"));
      Serial.println(F("Creating Default Configuration."));
      save_config(SPIFFS, path);
      return;
  } else {
    Serial.println(F(" - Success!"));
  }

  byte counter1 = 0;

  String temp_name; // lazy and using strings
  String temp_value;
  
  while(file.available()){

      byte temp = file.read();

      //Serial.print((char)temp);

      if(temp == '=') {
        counter1++;
      } else if(temp == '\n') {
        // run an interpretation.
        if(temp_name != "")
          assign_config(temp_name, temp_value);
        temp_name = ""; // reset our variables.
        temp_value = "";
        counter1 = 0;
      } else if (temp == '\r') {
        // skip carriage return
      } else if(counter1 == 0) {
        // append to the service name.
        temp_name += char(temp);
      } else if (counter1 == 1) {
        // append to the variable.
        temp_value += char(temp);
      }

      //display_print(F("."));
  }

  // if there isn't a \n at the end of the file
  // the last config option is skipped.
  if(temp_name != "")
    assign_config(temp_name, temp_value);

  file.close();

  Serial.println(F("Config Load Complete."));
}

void assign_config(String name, String value) {
  // Just a pity we can't automatically do this.

  Serial.print(F("Updating "));
  Serial.println(name);

  if(name == "ssid") {
    ssid = value;
  } else if (name == "wifi_password") {
    wifi_password = value;
  } else if (name == "http_username") {
    http_username = value;
  }else if (name == "http_password") {
    http_password = value;
  }else if (name == "host") {
    host = value;
  }else if (name == "allowedExtensionsForEdit") {
    allowedExtensionsForEdit = value;
  }else if (name == "jquery") {
    jquery = value;
  }
}

// We use this function to effectively create a default
// config.ini file.
void save_config(fs::FS &fs, const char * path) {

  //fs.remove(path);
  File file = fs.open(path, FILE_WRITE);
  if(!file){
      Serial.println(F("Write failed"));
      return;
  }
  
  String temp_message = "";
  temp_message += "ssid="+ssid+"\n";
  temp_message += "wifi_password="+wifi_password+"\n";
  temp_message += "http_username="+http_username+"\n";
  temp_message += "http_password="+http_password+"\n";
  temp_message += "host="+host+"\n";
  temp_message += "allowedExtensionsForEdit="+allowedExtensionsForEdit+"\n";
  temp_message += "jquery="+jquery+"\n";

  file.print(temp_message);
  file.close();

}