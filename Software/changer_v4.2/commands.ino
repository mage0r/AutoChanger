/*
 * Command interpreter.
 *
 * Transport-agnostic: processCommand() takes a single command line and dispatches it.
 * Two transports feed it, both from loop() (see the notes on cmdOut and i2c.ino for
 * why it has to be loop() and not an interrupt/callback context):
 *   - Serial:    readSerialCommands(), byte-stream framed on '\n'.
 *   - Secondary
 *     I2C bus:   readI2CCommands(), one command per I2C write transaction (see i2c.ino).
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

// Every cmd_*() function, and commandOK()/commandError(), write their response through
// this instead of directly to Serial - that's what lets the exact same interpreter
// serve two transports. It's pointed at &Serial normally; readI2CCommands() in i2c.ino
// points it at a small in-RAM buffer for the duration of one command, so that command's
// output lands there instead of going out over USB, then puts it back afterwards.
Print *cmdOut = &weblog; // weblog tees to the real Serial too - see changer_v4_2.ino

// True for the duration of a command dispatched from a transport whose messages are
// single, bounded, one-shot requests rather than a continuous byte stream - I2C (see
// i2c.ino's readI2CCommands()) and the web console (see /command in webserver.ino).
// Lets a command that genuinely can't work that way - see cmd_put() - refuse cleanly
// instead of hanging. A plain bool, not Print-based like cmdOut, because unlike
// output there's no generic way to redirect PUT's multi-byte transfer onto a
// transport like these - it has to be refused outright, not best-effort redirected.
bool processingViaSingleShot = false;

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
  {"I2C_ENABLE ON|OFF", "Turns the secondary I2C bus on or off and saves the setting."},
  {"SENSOR_ENABLE ON|OFF", "Turns automatic response to the arm sensor on or off and saves the setting - useful alongside S/R/L so AYAB can drive arms directly without the automatic sequence also reacting to every carriage pass."},
  {"INFO", "Shows project/build info, IP, chip model/revision, flash size, sketch size/free space, RAM, PSRAM, and SPIFFS usage."},
  {"LS", "Lists files in SPIFFS with their sizes."},
  {"CAT <filename>", "Prints a file's contents."},
  {"PUT <filename> <size>", "Writes a file - exactly <size> raw bytes must follow this line immediately. Used by the deploy tool to restore a backup."},
  {"RM <filename>", "Deletes a file. No confirmation - this is permanent."},
  {"FIX", "Restores any missing default files (config.ini, patterns.txt, servos.txt, index.html, autochanger.svg, manage.html, ok.html, edit.html, failed.html, join.html, joining.html). Leaves existing files untouched."},
  {"REBOOT", "Restarts the device immediately."},
  {"EJECT_ALL", "Moves every arm to its EJECT position."},
  {"S<arm>", "AYAB-style: lowers whichever arm(s) are up and raises <arm> (0-3). Returns OK."},
  {"R<arm>", "AYAB-style: raises <arm> (0-3) without lowering anything else. Returns OK."},
  {"L<arm>", "AYAB-style: lowers <arm> (0-3). Returns OK."},
  {"ARMS", "AYAB-style: returns 4 characters, one per arm in order, '+' for up (HIGH) or '_' for down, then OK."},
  {"P<pattern> <sequence>", "AYAB-style: replaces pattern <pattern> (0-3) with <sequence> (digits 1-4, up to 99 long). Returns OK."},
  {"DEBUG <level>", "Sets the debug output level: 0=off, 1=normal, 2=verbose/millis()-timestamped. Saved to config.ini."},
};
const byte commandTableSize = sizeof(commandTable) / sizeof(commandTable[0]);

// Echoes the command that was sent, followed by :OK or :ERR <reason>.
void commandOK() {
  cmdOut->print(currentCommandLine);
  cmdOut->println(F(":OK"));
}

void commandError(const String &reason) {
  cmdOut->print(currentCommandLine);
  cmdOut->print(F(":ERR "));
  cmdOut->println(reason);
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
  } else if(command == "I2C_ENABLE") {
    cmd_i2cEnable(args);
  } else if(command == "SENSOR_ENABLE") {
    cmd_sensorEnable(args);
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
  } else if(command == "EJECT_ALL") {
    cmd_ejectAll();
  } else if(command.length() == 2 && command.charAt(0) == 'S' && isDigit(command.charAt(1))) {
    cmd_switchArm(command.charAt(1) - '0');
  } else if(command.length() == 2 && command.charAt(0) == 'R' && isDigit(command.charAt(1))) {
    cmd_raiseArm(command.charAt(1) - '0');
  } else if(command.length() == 2 && command.charAt(0) == 'L' && isDigit(command.charAt(1))) {
    cmd_lowerArm(command.charAt(1) - '0');
  } else if(command.length() == 2 && command.charAt(0) == 'P' && isDigit(command.charAt(1))) {
    cmd_setPatternCmd(command.charAt(1) - '0', args);
  } else if(command == "ARMS") {
    cmd_arms();
  } else if(command == "DEBUG") {
    cmd_setDebug(args);
  } else {
    commandError(F("unknown command - send HELP for the list"));
  }
}

void cmd_help() {
  for(byte i = 0; i < commandTableSize; i++) {
    cmdOut->print(commandTable[i].syntax);
    cmdOut->print(F(" - "));
    cmdOut->println(commandTable[i].description);
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

  setWifiEnabled(turnOn);

  commandOK();
}

void cmd_i2cEnable(String args) {
  args.trim();
  args.toUpperCase();

  boolean turnOn;
  if(args == "ON" || args == "1") {
    turnOn = true;
  } else if(args == "OFF" || args == "0") {
    turnOn = false;
  } else {
    commandError(F("usage: I2C_ENABLE ON|OFF"));
    return;
  }

  setI2CEnabled(turnOn);

  commandOK();
}

// INFO - same figures/formatting as the web manager's System Status table (see processor()
// and convertFileSize() in webserver.ino), just laid out for a plain serial terminal.
void cmd_info() {
  cmdOut->print(F("Project: "));
  cmdOut->print(PROJECT);
  cmdOut->print(F(" - "));
  cmdOut->println(VERSION);

  cmdOut->print(F("Build: "));
  cmdOut->print(__DATE__);
  cmdOut->print(F(" "));
  cmdOut->println(__TIME__);

  cmdOut->print(F("IP: "));
  if(!wifi_enabled) {
    cmdOut->println(F("off"));
  } else if(!wifi_connected) {
    cmdOut->println(F("connecting..."));
  } else if(WiFi.getMode() == WIFI_AP) {
    cmdOut->println(WiFi.softAPIP());
  } else {
    cmdOut->println(WiFi.localIP());
  }

  cmdOut->print(F("Chip: "));
  cmdOut->print(ESP.getChipModel());
  cmdOut->print(F(" rev"));
  cmdOut->println(ESP.getChipRevision());

  cmdOut->print(F("Flash Size: "));
  cmdOut->print(ESP.getFlashChipSize() / 1024 / 1024);
  cmdOut->println(F(" MB"));

  cmdOut->print(F("Sketch Size: "));
  cmdOut->print(ESP.getSketchSize() / 1024);
  cmdOut->print(F(" KB, Free Sketch Space: "));
  cmdOut->print(ESP.getFreeSketchSpace() / 1024);
  cmdOut->println(F(" KB"));

  cmdOut->print(F("RAM: "));
  cmdOut->print(convertFileSize(ESP.getFreeHeap()));
  cmdOut->print(F(" / "));
  cmdOut->println(convertFileSize(ESP.getHeapSize()));

  cmdOut->print(F("PSRAM: "));
  cmdOut->print(convertFileSize(ESP.getFreePsram()));
  cmdOut->print(F(" / "));
  cmdOut->println(convertFileSize(ESP.getPsramSize()));

  cmdOut->print(F("SPIFFS: Total "));
  cmdOut->print(convertFileSize(SPIFFS.totalBytes()));
  cmdOut->print(F(", Used "));
  cmdOut->print(convertFileSize(SPIFFS.usedBytes()));
  cmdOut->print(F(", Free "));
  cmdOut->println(convertFileSize(SPIFFS.totalBytes() - SPIFFS.usedBytes()));

  commandOK();
}

// LS - lists every file in SPIFFS (flat filesystem, no real directories) with its size.
void cmd_ls() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while(file) {
    cmdOut->print(file.name());
    cmdOut->print(F("  "));
    cmdOut->println(convertFileSize(file.size()));
    file = root.openNextFile();
  }
  commandOK();
}

// PUT <filename> <size>
void cmd_put(String args) {
  // The raw bytes that follow a PUT command line are read by readSerialCommands()
  // directly from the Serial stream (see putBytesRemaining there) - there's no
  // equivalent for I2C or the web console, where each request is one bounded
  // transaction with no natural way to stream an arbitrary-length follow-up
  // payload the same way. Refuse cleanly rather than setting up state nothing
  // will ever complete.
  if(processingViaSingleShot) {
    commandError(F("PUT is not supported over I2C or the web console - use Serial"));
    return;
  }

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
    cmdOut->write(file.read());
  }
  file.close();

  cmdOut->println();
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

// EJECT_ALL - moves every arm to its EJECT position (servos[x][2]), the same
// position TEST/EJECT on the arm-adjust menu moves the selected arm to.
// SENSOR_ENABLE ON|OFF
void cmd_sensorEnable(String args) {
  args.trim();
  args.toUpperCase();

  boolean turnOn;
  if(args == "ON" || args == "1") {
    turnOn = true;
  } else if(args == "OFF" || args == "0") {
    turnOn = false;
  } else {
    commandError(F("usage: SENSOR_ENABLE ON|OFF"));
    return;
  }

  setSensorEnabled(turnOn);

  commandOK();
}

void cmd_ejectAll() {
  for(int x = 0; x < maxServo; x++) {
    moveServo(x, 2);
  }
  commandOK();
}

// AYAB-facing commands (S<arm>, R<arm>, L<arm>, ARMS, P<pattern>) - compact,
// fixed-format, matching the style AYAB's own protocol uses, rather than this
// project's usual named-keyword commands. Dispatched in processCommand() by
// checking the command token's shape (a single letter followed by one digit, or
// the fixed word ARMS) rather than an exact string match, since there are 16
// possible S/R/L/P tokens and listing each as its own keyword would be repetitive -
// see the four else-if branches that call these, just above the ARMS branch.

void cmd_raiseArm(int arm) {
  if(arm < 0 || arm > 3) {
    commandError(F("arm must be 0-3"));
    return;
  }
  moveServo(arm, 1);
  commandOK();
}

void cmd_lowerArm(int arm) {
  if(arm < 0 || arm > 3) {
    commandError(F("arm must be 0-3"));
    return;
  }
  moveServo(arm, 0);
  commandOK();
}

// Lowers whichever arm(s) are currently up (normally just one, but this covers
// every arm armUp[] reports regardless, so the end state is always correct even
// after an unusual R/L sequence) and raises the requested one.
void cmd_switchArm(int arm) {
  if(arm < 0 || arm > 3) {
    commandError(F("arm must be 0-3"));
    return;
  }
  for(int i = 0; i < 4; i++) {
    if(armUp[i] && i != arm) {
      moveServo(i, 0);
    }
  }
  moveServo(arm, 1);
  commandOK();
}

// Reports which arms are currently at their HIGH (active) position, one character
// per arm in order - '+' up, '_' down. EJECT counts as "down" here: it's not the
// active knitting position, which is the only distinction this query makes.
void cmd_arms() {
  String result;
  for(int i = 0; i < 4; i++) {
    result += armUp[i] ? '+' : '_';
  }
  cmdOut->println(result);
  commandOK();
}

// P<pattern> <sequence> - see setPatternSteps() in patterns.ino, which this and
// the web UI's /setpattern both call.
void cmd_setPatternCmd(int n, String steps) {
  String error = setPatternSteps(n, steps);
  if(error.length()) {
    commandError(error);
    return;
  }
  commandOK();
}

void cmd_setDebug(String args) {
  args.trim();
  int level = args.toInt();
  if(level < 0 || (level == 0 && args != "0")) {
    commandError(F("usage: DEBUG <level> - 0=off, 1=normal, 2=verbose"));
    return;
  }
  DEBUG = constrain(level, 0, 10);
  save_config(SPIFFS, "/config.ini");
  commandOK();
}

void cmd_fix() {
  byte restored = 0;

  if(!SPIFFS.exists("/config.ini")) {
    save_config(SPIFFS, "/config.ini");
    cmdOut->println(F("Restored /config.ini"));
    restored++;
  }

  if(!SPIFFS.exists("/patterns.txt")) {
    RestoreDefault(0);
    RestoreDefault(1);
    RestoreDefault(2);
    RestoreDefault(3);
    save_patterns(SPIFFS, "/patterns.txt");
    cmdOut->println(F("Restored /patterns.txt"));
    restored++;
  }

  if(!SPIFFS.exists("/servos.txt")) {
    default_servos(); // sets defaults, moves arms to eject, and saves
    cmdOut->println(F("Restored /servos.txt"));
    restored++;
  }

  const char* htmlPath[] = {"/index.html", "/autochanger.svg", "/manage.html", "/ok.html",
                            "/edit.html", "/failed.html", "/join.html", "/joining.html"};
  const char* htmlContent[] = {index_html, autochanger_svg, manager_html, ok_html,
                               edit_html, failed_html, wifi_join_html, wifi_joining_html};
  for(byte i = 0; i < sizeof(htmlPath) / sizeof(htmlPath[0]); i++) {
    if(!SPIFFS.exists(htmlPath[i])) {
      cmdOut->print(F("Restored "));
      cmdOut->println(htmlPath[i]);
      restored++;
    }
    save_html(SPIFFS, htmlPath[i], htmlContent[i]); // no-op if it already exists
  }

  if(restored == 0) {
    cmdOut->println(F("Nothing missing."));
  }

  commandOK();
}