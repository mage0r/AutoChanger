// Web requests that move arms, change the pattern state, or touch the radio, I2C bus
// or filesystem don't act inside the web handler - they're queued here and carried
// out by loop() (WebActions::runPending()).
//
// Web handlers run on the async_tcp task, concurrently with loop(). Acting there meant
// a /next arriving while the carriage sensor was mid-switchRods() could advance the
// step twice or leave two arms up; long jobs (a format) risked async_tcp's watchdog;
// and /wifi_off or a REBOOT command could cut the connection before the response got
// out. Now loop() is the only place any of this happens, one action at a time.
//
// Handlers still validate their input and answer straight away (400 on bad input,
// 503 if the queue is somehow full); the action itself follows within a loop pass.
// Settings that only set a value and save it (sensor, buzzer, debug, password...)
// stay in their handlers - SafeFile serialises the save itself.

#define WEB_ACTION_QUEUE_LEN 8

// Defined in commands.ino. Declared here too because the IDE joins the .ino files in
// name order, and a file sorting ahead of commands.ino wouldn't otherwise see it.
extern bool processingViaSingleShot;

class WebActions {
public:
  enum Type : uint8_t {
    PRESS,          // a = arm
    SWITCH,         // a = pattern
    NEXT,
    BACK,
    SET_PATTERN,    // a = pattern, text = steps as 1-4 (already validated)
    SERVO_SAVE,     // a = arm, b/c/d = low/high/eject (already clamped)
    SERVO_PREVIEW,  // a = arm, b = value
    SERVO_TEST,     // a = arm, b = 1 for all arms
    COMMAND,        // text = command line
    JOIN,           // text = ssid, text2 = password
    WIFI_OFF,
    I2C_ENABLE,     // a = 1 on / 0 off
    I2C_ADDRESS,    // a = address
    FORMAT
  };

private:
  struct Action {
    uint8_t type;
    int a, b, c, d;
    char text[128];
    char text2[72];
  };

  static QueueHandle_t queue() {
    static QueueHandle_t q = xQueueCreate(WEB_ACTION_QUEUE_LEN, sizeof(Action));
    return q;
  }

  // Lets the HTTP response that queued this get out before the connection drops.
  static void letResponseFlush() {
    delay(300);
  }

  static void run(const Action &act) {
    switch(act.type) {
      case PRESS:
        webPressButton(act.a);
        break;
      case SWITCH:
        change_program(act.a);
        break;
      case NEXT:
        if(patterns[currentPattern].length)
          switchRods();
        break;
      case BACK:
        if(patterns[currentPattern].length)
          switchRodsBack();
        break;
      case SET_PATTERN:
        applyPatternSteps(act.a, String(act.text));
        break;
      case SERVO_SAVE:
        servos[act.a][0] = act.b;
        servos[act.a][1] = act.c;
        servos[act.a][2] = act.d;
        save_servos(LittleFS, "/servos.txt");
        break;
      case SERVO_PREVIEW:
        previewServo(act.a, act.b);
        break;
      case SERVO_TEST:
        startArmTest(act.a, act.b != 0);
        break;
      case COMMAND:
        processingViaSingleShot = true;
        processCommand(String(act.text));
        processingViaSingleShot = false;
        break;
      case JOIN:
        letResponseFlush();
        applyWifiCredentials(String(act.text), String(act.text2));
        break;
      case WIFI_OFF:
        letResponseFlush();
        setWifiEnabled(false);
        break;
      case I2C_ENABLE:
        setI2CEnabled(act.a != 0);
        break;
      case I2C_ADDRESS:
        i2c2_address = act.a;
        save_config(LittleFS, "/config.ini");
        if(i2c_enabled) {
          // apply it immediately rather than needing a reboot
          teardown_i2c_secondary();
          setup_i2c_secondary();
        }
        break;
      case FORMAT:
        letResponseFlush();
        weblog.println(F("Formatting the filesystem and restarting..."));
        LittleFS.format();
        ESP.restart();
        break;
    }
  }

public:
  // Queues an action for loop(). text/text2 may be nullptr. Returns false if the
  // queue is full or the text won't fit - the handler should answer 503/400.
  static bool push(Type type, int a, int b, int c, int d, const char *text, const char *text2) {
    Action act;
    memset(&act, 0, sizeof(act));
    act.type = type;
    act.a = a;
    act.b = b;
    act.c = c;
    act.d = d;
    if(text) {
      if(strlen(text) >= sizeof(act.text))
        return false;
      strcpy(act.text, text);
    }
    if(text2) {
      if(strlen(text2) >= sizeof(act.text2))
        return false;
      strcpy(act.text2, text2);
    }
    return xQueueSend(queue(), &act, 0) == pdTRUE;
  }

  static bool push(Type type) {
    return push(type, 0, 0, 0, 0, nullptr, nullptr);
  }

  static bool push(Type type, int a) {
    return push(type, a, 0, 0, 0, nullptr, nullptr);
  }

  static bool push(Type type, int a, int b) {
    return push(type, a, b, 0, 0, nullptr, nullptr);
  }

  // Called every loop() - carries out everything queued since the last pass.
  static void runPending() {
    Action act;
    while(xQueueReceive(queue(), &act, 0) == pdTRUE)
      run(act);
  }
};

// Called from loop(). A plain function (not WebActions:: directly) because loop() is
// in the main .ino, which the Arduino IDE compiles before this file's class exists.
void run_web_actions() {
  WebActions::runPending();
}