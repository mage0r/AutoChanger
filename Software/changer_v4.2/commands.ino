/*
 * Command interpreter.
 *
 * Transport-agnostic: processCommand() takes a single command line and dispatches it.
 * Serial is wired up today (readSerialCommands(), called from loop()); an I2C receive
 * handler can call processCommand() the same way later on.
 *
 * Command format:   COMMAND ARGS\n
 * Response:         <the command you sent>:OK, or <the command you sent>:ERR <reason>
 * (commands that print their own output, like LS or CAT, print that first, then the
 * status line last)
 *
 * Send HELP for the full list of commands - see commandTable below, which is the single
 * place to add an entry (syntax + description) whenever a new command is added.
 */

#define MAX_PUT_SIZE 262144 // 256KB - a generous sanity cap, not a real limit any file here approaches

String serialCommandBuffer = "";
String currentCommandLine = ""; // the raw line currently being processed, for commandOK()/commandError()

// State for an in-progress PUT (see cmd_put() and the top of readSerialCommands()).
// Writes land in a .tmp file first and only replace the real file once every byte
// has arrived, so a transfer that gets interrupted partway through (e.g. a USB
// disconnect) can't leave a half-written file in place of a good one.
File putFile;
long putBytesRemaining = 0;
String putTargetPath = "";
String putTempPath = "";
String putEchoLine = ""; // currentCommandLine at the time PUT started, for the final :OK/:ERR

struct CommandHelp {
  const char* syntax;
  const char* description;
};

// Add a line here for every new command - this is what HELP prints.
const CommandHelp commandTable[] = {
  {"HELP", "Lists all available commands."},
  {"WIFI <ssid>,<password>", "Sets and saves the WiFi SSID/password, and reconnects immediately if WiFi is currently enabled."},
  {"WIFI_ENABLE ON|OFF", "Turns WiFi on or off and saves the setting."},
  {"INFO", "Shows project/build info, IP, chip model/revision, flash size, sketch size/free space, RAM, PSRAM, and SPIFFS usage."},
  {"LS", "Lists files in SPIFFS with their sizes."},
  {"CAT <filename>", "Prints a file's contents."},
  {"PUT <filename> <size>", "Writes a file - exactly <size> raw bytes must follow this line immediately. Used by the deploy tool to restore a backup."},
  {"RM <filename>", "Deletes a file. No confirmation - this is permanent."},
  {"FIX", "Restores any missing default files (config.ini, patterns.txt, servos.txt, index.html, autochanger.svg, manage.html, ok.html, edit.html, failed.html, join.html, joining.html). Leaves existing files untouched."},
  {"REBOOT", "Restarts the device immediately."},
};
const byte commandTableSize = sizeof(commandTable) / sizeof(commandTable[0]);

// Echoes the command that was sent, followed by :OK or :ERR <reason>.
void commandOK() {
  Serial.print(currentCommandLine);
  Serial.println(F(":OK"));
}

void commandError(const String &reason) {
  Serial.print(currentCommandLine);
  Serial.print(F(":ERR "));
  Serial.println(reason);
}

// Call every loop() - reads whatever's arrived on Serial and dispatches complete lines,
// except while a PUT transfer is in progress, when incoming bytes are the file's raw
// content instead and go straight to putFile until the expected count is reached.
void readSerialCommands() {
  while(Serial.available()) {
    if(putBytesRemaining > 0) {
      putFile.write((uint8_t)Serial.read());
      putBytesRemaining--;
      if(putBytesRemaining == 0) {
        putFile.close();
        if(SPIFFS.exists(putTargetPath))
          SPIFFS.remove(putTargetPath);
        SPIFFS.rename(putTempPath, putTargetPath);
        currentCommandLine = putEchoLine;
        commandOK();
      }
      continue;
    }

    char c = Serial.read();
    if(c == '\n') {
      serialCommandBuffer.trim();
      if(serialCommandBuffer.length())
        processCommand(serialCommandBuffer);
      serialCommandBuffer = "";
    } else if(c != '\r') {
      serialCommandBuffer += c;
    }
  }
}

// The actual dispatcher. Takes one command line, independent of where it came from.
void processCommand(String cmd) {
  currentCommandLine = cmd;

  String command = cmd;
  String args = "";

  int spaceIndex = cmd.indexOf(' ');
  if(spaceIndex != -1) {
    command = cmd.substring(0, spaceIndex);
    args = cmd.substring(spaceIndex+1);
  }

  command.toUpperCase();

  if(command == "HELP") {
    cmd_help();
  } else if(command == "WIFI") {
    cmd_setWifi(args);
  } else if(command == "WIFI_ENABLE") {
    cmd_wifiEnable(args);
  } else if(command == "INFO") {
    cmd_info();
  } else if(command == "LS") {
    cmd_ls();
  } else if(command == "CAT") {
    cmd_cat(args);
  } else if(command == "PUT") {
    cmd_put(args);
  } else if(command == "RM") {
    cmd_rm(args);
  } else if(command == "FIX") {
    cmd_fix();
  } else if(command == "REBOOT") {
    cmd_reboot();
  } else {
    commandError(F("unknown command - send HELP for the list"));
  }
}

void cmd_help() {
  for(byte i = 0; i < commandTableSize; i++) {
    Serial.print(commandTable[i].syntax);
    Serial.print(F(" - "));
    Serial.println(commandTable[i].description);
  }
  commandOK();
}

// WIFI <ssid>,<password>
void cmd_setWifi(String args) {
  int commaIndex = args.indexOf(',');
  if(commaIndex == -1) {
    commandError(F("usage: WIFI <ssid>,<password>"));
    return;
  }

  String newSsid = args.substring(0, commaIndex);
  String newPassword = args.substring(commaIndex+1);

  if(newSsid.length() == 0) {
    commandError(F("ssid can't be empty"));
    return;
  }

  applyWifiCredentials(newSsid, newPassword);

  commandOK();
}

// WIFI_ENABLE ON|OFF
void cmd_wifiEnable(String args) {
  args.trim();
  args.toUpperCase();

  boolean turnOn;
  if(args == "ON" || args == "1") {
    turnOn = true;
  } else if(args == "OFF" || args == "0") {
    turnOn = false;
  } else {
    commandError(F("usage: WIFI_ENABLE ON|OFF"));
    return;
  }

  wifi_enabled = turnOn;
  if(wifi_enabled) {
    wifi_counter = millis(); // fresh 30s window to connect before falling back to AP mode
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_connected = false;
  }
  save_config(SPIFFS, "/config.ini");

  commandOK();
}

// INFO - same figures/formatting as the web manager's System Status table (see processor()
// and convertFileSize() in webserver.ino), just laid out for a plain serial terminal.
void cmd_info() {
  Serial.print(F("Project: "));
  Serial.print(PROJECT);
  Serial.print(F(" - "));
  Serial.println(VERSION);

  Serial.print(F("Build: "));
  Serial.print(__DATE__);
  Serial.print(F(" "));
  Serial.println(__TIME__);

  Serial.print(F("IP: "));
  if(!wifi_enabled) {
    Serial.println(F("off"));
  } else if(!wifi_connected) {
    Serial.println(F("connecting..."));
  } else if(WiFi.getMode() == WIFI_AP) {
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println(WiFi.localIP());
  }

  Serial.print(F("Chip: "));
  Serial.print(ESP.getChipModel());
  Serial.print(F(" rev"));
  Serial.println(ESP.getChipRevision());

  Serial.print(F("Flash Size: "));
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(F(" MB"));

  Serial.print(F("Sketch Size: "));
  Serial.print(ESP.getSketchSize() / 1024);
  Serial.print(F(" KB, Free Sketch Space: "));
  Serial.print(ESP.getFreeSketchSpace() / 1024);
  Serial.println(F(" KB"));

  Serial.print(F("RAM: "));
  Serial.print(convertFileSize(ESP.getFreeHeap()));
  Serial.print(F(" / "));
  Serial.println(convertFileSize(ESP.getHeapSize()));

  Serial.print(F("PSRAM: "));
  Serial.print(convertFileSize(ESP.getFreePsram()));
  Serial.print(F(" / "));
  Serial.println(convertFileSize(ESP.getPsramSize()));

  Serial.print(F("SPIFFS: Total "));
  Serial.print(convertFileSize(SPIFFS.totalBytes()));
  Serial.print(F(", Used "));
  Serial.print(convertFileSize(SPIFFS.usedBytes()));
  Serial.print(F(", Free "));
  Serial.println(convertFileSize(SPIFFS.totalBytes() - SPIFFS.usedBytes()));

  commandOK();
}

// LS - lists every file in SPIFFS (flat filesystem, no real directories) with its size.
void cmd_ls() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while(file) {
    Serial.print(file.name());
    Serial.print(F("  "));
    Serial.println(convertFileSize(file.size()));
    file = root.openNextFile();
  }
  commandOK();
}

// PUT <filename> <size>
void cmd_put(String args) {
  int spaceIndex = args.lastIndexOf(' ');
  if(spaceIndex == -1) {
    commandError(F("usage: PUT <filename> <size>"));
    return;
  }

  String filename = args.substring(0, spaceIndex);
  filename.trim();
  long size = args.substring(spaceIndex+1).toInt();

  if(filename.length() == 0) {
    commandError(F("usage: PUT <filename> <size>"));
    return;
  }
  if(size < 0 || size > MAX_PUT_SIZE) {
    commandError("size must be between 0 and " + String(MAX_PUT_SIZE) + " bytes");
    return;
  }

  String path = filename;
  if(!path.startsWith("/"))
    path = "/" + path;

  putTargetPath = path;
  putTempPath = path + ".tmp";

  putFile = SPIFFS.open(putTempPath, FILE_WRITE);
  if(!putFile) {
    commandError(F("could not open file for writing"));
    return;
  }

  if(size == 0) {
    // Nothing to wait for - finish right away. The byte-consuming loop in
    // readSerialCommands() only ever completes a transfer when putBytesRemaining
    // counts DOWN to zero; it would never fire if it started there, so an empty
    // file needs to be handled here instead, not left to that loop.
    putFile.close();
    if(SPIFFS.exists(putTargetPath))
      SPIFFS.remove(putTargetPath);
    SPIFFS.rename(putTempPath, putTargetPath);
    commandOK();
    return;
  }

  // The :OK for a non-empty file fires once the bytes have actually all arrived
  // (see readSerialCommands()), not here - this just starts the transfer.
  putEchoLine = currentCommandLine;
  putBytesRemaining = size;
}

// CAT <filename>
void cmd_cat(String args) {
  args.trim();
  if(args.length() == 0) {
    commandError(F("usage: CAT <filename>"));
    return;
  }

  String path = args;
  if(!path.startsWith("/"))
    path = "/" + path;

  File file = SPIFFS.open(path);
  if(!file || file.isDirectory()) {
    commandError(F("file not found"));
    return;
  }

  while(file.available()) {
    Serial.write(file.read());
  }
  file.close();

  Serial.println();
  commandOK();
}

// RM <filename> - no confirmation, deletes immediately.
void cmd_rm(String args) {
  args.trim();
  if(args.length() == 0) {
    commandError(F("usage: RM <filename>"));
    return;
  }

  String path = args;
  if(!path.startsWith("/"))
    path = "/" + path;

  if(SPIFFS.remove(path)) {
    commandOK();
  } else {
    commandError(F("file not found or could not be removed"));
  }
}

// FIX - restores any missing default files. Checks each one first so existing files (your
// actual patterns, servo positions, config, or any HTML you've customised) are never touched -
// only genuinely missing files get recreated.
// REBOOT - sends OK first (and makes sure it's actually out over the wire) so the
// caller gets confirmation before the connection drops, then restarts immediately.
void cmd_reboot() {
  commandOK();
  Serial.flush();
  delay(100);
  ESP.restart();
}

void cmd_fix() {
  byte restored = 0;

  if(!SPIFFS.exists("/config.ini")) {
    save_config(SPIFFS, "/config.ini");
    Serial.println(F("Restored /config.ini"));
    restored++;
  }

  if(!SPIFFS.exists("/patterns.txt")) {
    RestoreDefault(0);
    RestoreDefault(1);
    RestoreDefault(2);
    RestoreDefault(3);
    save_patterns(SPIFFS, "/patterns.txt");
    Serial.println(F("Restored /patterns.txt"));
    restored++;
  }

  if(!SPIFFS.exists("/servos.txt")) {
    default_servos(); // sets defaults, moves arms to eject, and saves
    Serial.println(F("Restored /servos.txt"));
    restored++;
  }

  const char* htmlPath[] = {"/index.html", "/autochanger.svg", "/manage.html", "/ok.html",
                            "/edit.html", "/failed.html", "/join.html", "/joining.html"};
  const char* htmlContent[] = {index_html, autochanger_svg, manager_html, ok_html,
                               edit_html, failed_html, wifi_join_html, wifi_joining_html};
  for(byte i = 0; i < sizeof(htmlPath) / sizeof(htmlPath[0]); i++) {
    if(!SPIFFS.exists(htmlPath[i])) {
      Serial.print(F("Restored "));
      Serial.println(htmlPath[i]);
      restored++;
    }
    save_html(SPIFFS, htmlPath[i], htmlContent[i]); // no-op if it already exists
  }

  if(restored == 0) {
    Serial.println(F("Nothing missing."));
  }

  commandOK();
}