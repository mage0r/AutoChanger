void setup_config() {
  // load the config from the config.ini file on the SPIFFS file system.
  // if the file doesn't exist, load defaults.
  // There's no reason not to add your own options.

  Serial.println(F("Loading Defaults"));

  // Set all our defaults.
  // if we have a config file these will immediately be overridden.
  // but does that matter?  not really.
  
  ssid = PROJECT;
  wifi_password = "";

  http_username = "admin";
  http_password = "admin";

  allowedExtensionsForEdit = "txt, h, htm, html, css, cpp, js, ini, svg";

  jquery = "/jquery-3.6.3.min.js";

  test_run = 10;

  wifi_enabled = true;

}

void load_config(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Config: "));
  Serial.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      Serial.println(F("- failed to open file for reading"));
      Serial.println(F("Creating Default Configuration."));
      save_config(SPIFFS, path);
      save_html(SPIFFS, "/index.html", index_html);
      save_html(SPIFFS, "/autochanger.svg", autochanger_svg);
      save_html(SPIFFS, "/manage.html", manager_html);
      save_html(SPIFFS, "/ok.html", ok_html);
      save_html(SPIFFS, "/edit.html", edit_html);
      save_html(SPIFFS, "/failed.html", failed_html);
      return;
  } else {
    Serial.println(F(" - Success!"));
  }

  byte counter1 = 0;

  String temp_name; // lazy and using strings
  String temp_value;
  
  while(file.available()){

      byte temp = file.read();

      if(temp == '=' && counter1 == 0) {
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
      } else {
        // everything after the first '=' is the value, including any further '=' (e.g. base64 padding).
        temp_value += char(temp);
      }
  }

  // if there isn't a \n at the end of the file
  // the last config option is skipped.
  if(temp_name != "")
    assign_config(temp_name, temp_value);

  file.close();

  Serial.println(F("Config Load Complete."));

  if(save) {
    Serial.println(F("Updating Wifi Password."));
    save_config(SPIFFS, path);
    save = false;
  }
}

void assign_config(String name, String value) {
  // Just a pity we can't automatically do this.

  Serial.print(F("Updating "));
  Serial.println(name);

  if(name == "ssid") {
    ssid = value;
  } else if (name == "wifi_password") {
    wifi_password = decryptXorBase64(value);
  } else if (name == "http_username") {
    http_username = value;
  }else if (name == "http_password") {
    http_password = value;
  }else if (name == "allowedExtensionsForEdit") {
    allowedExtensionsForEdit = value;
  }else if (name == "jquery") {
    jquery = value;
  }else if(name == "test_run") {
    test_run = value.toInt();
  } else if(name == "wifi_enabled") {
    wifi_enabled = value.toInt();
  } else if(name == "debounceDelay") {
    debounceDelay = value.toInt();
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
  temp_message += "wifi_password="+encryptXorBase64(wifi_password)+"\n";
  temp_message += "http_username="+http_username+"\n";
  temp_message += "http_password="+http_password+"\n";
  temp_message += "allowedExtensionsForEdit="+allowedExtensionsForEdit+"\n";
  temp_message += "jquery="+jquery+"\n";
  temp_message += "test_run="+(String)test_run+"\n";
  temp_message += "wifi_enabled="+(String)wifi_enabled+"\n";
  temp_message += "debounceDelay="+(String)debounceDelay+"\n";

  file.print(temp_message);
  file.close();

}

void save_html(fs::FS &fs, const char *path, const char *html) {
  // If index.html doesn't exist, create it
  File file = fs.open(path);
  if(!file  || file.isDirectory()) {
    file.close();
    Serial.print("Default ");
    Serial.print(path);
    Serial.println(" does not exist, creating.");
    file = fs.open(path, FILE_WRITE);
    file.print(html);
  }

  file.close();
}

String decryptXorBase64(const String &stored) {
  if (stored.length() == 0) return String();

  // If it doesn't start with ENC_PREFIX, treat as plain text
  if (stored[0] != ENC_PREFIX) {
    save = true;
    return stored;
  }

  // Strip prefix
  String b64 = stored.substring(1);

  // Copy to a mutable C string for the library
  int inLen = b64.length();
  char inBuf[inLen + 1];
  b64.toCharArray(inBuf, inLen + 1);

  // Base64 decode buffer length
  unsigned int decodedLen = decode_base64_length((unsigned char *)inBuf);
  uint8_t decoded[decodedLen];

  unsigned int outLen = decode_base64((unsigned char *)inBuf, decoded);

  // XOR back to get original
  String result;
  result.reserve(outLen);
  for (unsigned int i = 0; i < outLen; i++) {
    result += char(decoded[i] ^ XOR_KEY);
  }

  return result;
}

String encryptXorBase64(const String &plain) {
  if (plain.length() == 0) return String();

  int len = plain.length();
  uint8_t xored[len];

  // XOR
  for (int i = 0; i < len; i++) {
    xored[i] = (uint8_t)plain[i] ^ XOR_KEY;
  }

  // Densaugeo base64: need output length
  unsigned int b64Len = encode_base64_length(len);
  unsigned char b64[b64Len + 1];  // +1 for safety/null

  unsigned int outLen = encode_base64(xored, len, b64);
  b64[outLen] = '\0';

  // Add marker prefix so we know it’s encrypted
  return String(ENC_PREFIX) + String((char *)b64);
}