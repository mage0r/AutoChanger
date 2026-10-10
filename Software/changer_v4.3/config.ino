void setup_config() {
  // load the config from the config.ini file on the LittleFS file system.
  // if the file doesn't exist, load defaults.
  // There's no reason not to add your own options.

  weblog.println(F("Loading Defaults"));

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
  i2c_enabled = false;
  i2c2_address = I2C2_ADDRESS_DEFAULT;
  sensor_enabled = true;
  BUZZER = true;
  buzzerNote = 1046.502;
  DEBUG = 1;

}

void load_config(fs::FS &fs, const char * path) {
  weblog.print(F("Loading Config: "));
  weblog.print(path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
      weblog.println(F("- failed to open file for reading"));
      weblog.println(F("Creating Default Configuration."));
      save_config(LittleFS, path);
      save_html(LittleFS, "/index.html", index_html);
      save_html(LittleFS, "/autochanger.svg", autochanger_svg);
      save_html(LittleFS, "/manage.html", manager_html);
      save_html(LittleFS, "/ok.html", ok_html);
      save_html(LittleFS, "/edit.html", edit_html);
      save_html(LittleFS, "/failed.html", failed_html);
      save_html(LittleFS, "/join.html", wifi_join_html);
      save_html(LittleFS, "/joining.html", wifi_joining_html);
      return;
  } else {
    weblog.println(F(" - Success!"));
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

  weblog.println(F("Config Load Complete."));

  if(save) {
    weblog.println(F("Updating Wifi Password."));
    save_config(LittleFS, path);
    save = false;
  }
}

void assign_config(String name, String value) {
  // Just a pity we can't automatically do this.

  weblog.print(F("Updating "));
  weblog.println(name);

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
  } else if(name == "i2c_enabled") {
    i2c_enabled = value.toInt();
  } else if(name == "i2c2_address") {
    // 0x00-0x07 and 0x78-0x7F are reserved in the 7-bit I2C address space - ignore
    // anything in those ranges (or unparseable) and keep whatever was already set
    // rather than letting a bad config.ini value take the bus down silently.
    int parsed = value.toInt();
    if(parsed >= 0x08 && parsed <= 0x77)
      i2c2_address = parsed;
  } else if(name == "debounceDelay") {
    debounceDelay = value.toInt();
  } else if(name == "debug") {
    DEBUG = constrain(value.toInt(), 0, 10);
  } else if(name == "sensor_enabled") {
    sensor_enabled = value.toInt();
  } else if(name == "buzzer_enabled") {
    BUZZER = value.toInt();
  } else if(name == "buzzer_note") {
    buzzerNote = value.toDouble();
  }
}

// We use this function to effectively create a default
// config.ini file.
// Written via SafeFile (safefile.h): a power cut mid-save leaves the previous file.
void save_config(fs::FS &fs, const char * path) {
  String temp_message = "";
  temp_message += "ssid="+ssid+"\n";
  temp_message += "wifi_password="+encryptXorBase64(wifi_password)+"\n";
  temp_message += "http_username="+http_username+"\n";
  temp_message += "http_password="+http_password+"\n";
  temp_message += "allowedExtensionsForEdit="+allowedExtensionsForEdit+"\n";
  temp_message += "jquery="+jquery+"\n";
  temp_message += "test_run="+(String)test_run+"\n";
  temp_message += "wifi_enabled="+(String)wifi_enabled+"\n";
  temp_message += "i2c_enabled="+(String)i2c_enabled+"\n";
  temp_message += "i2c2_address="+(String)i2c2_address+"\n";
  temp_message += "debounceDelay="+(String)debounceDelay+"\n";
  temp_message += "debug="+(String)DEBUG+"\n";
  temp_message += "sensor_enabled="+(String)sensor_enabled+"\n";
  temp_message += "buzzer_enabled="+(String)BUZZER+"\n";
  temp_message += "buzzer_note="+(String)buzzerNote+"\n";

  if(!SafeFile::write(fs, path, temp_message))
    weblog.println(F("Saving config FAILED"));
}

void save_html(fs::FS &fs, const char *path, const char *html) {
  // only if it doesn't exist yet - exists() rather than a test open(), which logs an
  // error for a missing file on LittleFS
  if(fs.exists(path))
    return;
  weblog.print("Default ");
  weblog.print(path);
  weblog.println(" does not exist, creating.");
  SafeFile::write(fs, path, html, strlen(html));
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