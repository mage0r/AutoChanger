/*
 * Standalone test: press a button on IO4 to run a burn-in test -
 * servos 0-3 each move low -> high, 10 cycles through, one servo at
 * a time. When all 10 cycles finish, every servo is returned to its
 * low position. A running total cycle count is tracked and printed.
 *
 * Low/high positions are per-servo and are loaded from / saved to
 * "/servos.txt" on SPIFFS, using the exact same format as the main
 * AutoChanger project's load_servos() / save_servos() (see servos.ino):
 *
 *   <moveCount>,<low>,<high>,<eject>\n     (one line per servo)
 *
 * Over Serial (115200 baud) you can view and change the low/high
 * values. L/H immediately move the servo to the new position so you
 * can see/feel it, but nothing is written to SPIFFS until you send S:
 *
 *   L<servo> <value>   e.g. "L0 200"  - set & move servo 0 to low position
 *   H<servo> <value>   e.g. "H0 550"  - set & move servo 0 to high position
 *   A<value>           e.g. "A300"    - park ALL servos at this position (not saved)
 *   R                  - run a burn-in cycle (same as pressing IO4)
 *   P                  - print current values
 *   S                  - save current values to SPIFFS
 *
 * Assumes a normally-open button between IO4 and GND, using the
 * internal pull-up (same wiring convention as PGRM_BTN in the main
 * project) - so a press reads as a falling edge.
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Bounce2.h>
#include "FS.h"
#include "SPIFFS.h"

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

const int NUM_SERVOS = 4;
const int CYCLES = 10;
const int MOVE_DELAY = 500; // ms to wait after each move
const int TRIGGER_PIN = 4;  // IO4

#define FORMAT_SPIFFS_IF_FAILED true

// [servo][0] = low, [servo][1] = high, [servo][2] = eject.
// Same layout as "servos" in the main project - eject isn't used by
// this burn-in test but is preserved so the file stays compatible.
int servos[4][3];
unsigned int servoCount[4];

unsigned long burnInCycleCount = 0;

Bounce triggerButton = Bounce();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Servo burn-in test ready."));
  Serial.println(F("Press the button on IO4 to run one burn-in cycle."));
  Serial.println(F("Serial commands: L<servo> <val>, H<servo> <val> (moves servo), A<val> (park all), R (run cycle), P (print), S (save)"));

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println(F("SPIFFS mount failed!"));
  }

  pwm.begin();
  pwm.setPWMFreq(60);

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  triggerButton.attach(TRIGGER_PIN);
  triggerButton.interval(25);

  load_servos(SPIFFS, "/servos.txt");
  printServos();
}

void loop() {
  triggerButton.update();

  if (triggerButton.fell()) {
    runBurnInCycle();
  }

  if (Serial.available()) {
    handleSerialCommand();
  }
}

void runBurnInCycle() {
  Serial.println(F("Button pressed - starting burn-in test."));

  for (int cycle = 1; cycle <= CYCLES; cycle++) {
    burnInCycleCount++;
    Serial.print(F("Cycle "));
    Serial.print(cycle);
    Serial.print(F(" of "));
    Serial.print(CYCLES);
    Serial.print(F(" (total: "));
    Serial.print(burnInCycleCount);
    Serial.println(F(")"));

    for (int servo = 0; servo < NUM_SERVOS; servo++) {
      Serial.print(F("  Servo "));
      Serial.print(servo);
      Serial.print(F(" -> "));
      Serial.println(servos[servo][0]);
      pwm.setPWM(servo, 0, servos[servo][0]);
      servoCount[servo]++;
      delay(MOVE_DELAY);

      Serial.print(F("  Servo "));
      Serial.print(servo);
      Serial.print(F(" -> "));
      Serial.println(servos[servo][1]);
      pwm.setPWM(servo, 0, servos[servo][1]);
      servoCount[servo]++;
      delay(MOVE_DELAY);
    }
  }

  Serial.println(F("Returning all servos to low."));
  for (int servo = 0; servo < NUM_SERVOS; servo++) {
    pwm.setPWM(servo, 0, servos[servo][0]);
    delay(MOVE_DELAY);
  }

  Serial.println(F("Burn-in test complete. Press button to run again."));
}

// ---- Serial command handling ----

void handleSerialCommand() {
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  char cmd = toupper(line.charAt(0));

  if (cmd == 'P') {
    printServos();
    return;
  }

  if (cmd == 'S') {
    save_servos(SPIFFS, "/servos.txt");
    return;
  }

  if (cmd == 'R') {
    runBurnInCycle();
    return;
  }

  if (cmd == 'A') {
    long value = 0;
    if (sscanf(line.c_str() + 1, "%ld", &value) == 1) {
      parkAllServos(value);
    } else {
      Serial.println(F("Usage: A<value>  e.g. A300"));
    }
    return;
  }

  if (cmd == 'L' || cmd == 'H') {
    int servoIdx = -1;
    long value = 0;
    if (sscanf(line.c_str() + 1, "%d %ld", &servoIdx, &value) == 2) {
      if (servoIdx >= 0 && servoIdx < NUM_SERVOS) {
        int field = (cmd == 'L') ? 0 : 1;
        servos[servoIdx][field] = value;
        pwm.setPWM(servoIdx, 0, value);
        servoCount[servoIdx]++;
        Serial.print(cmd == 'L' ? F("Low") : F("High"));
        Serial.print(F(" set for servo "));
        Serial.print(servoIdx);
        Serial.print(F(" -> "));
        Serial.print(value);
        Serial.println(F(" (not saved - send S to save)"));
      } else {
        Serial.println(F("Invalid servo number (0-3)."));
      }
    } else {
      Serial.println(F("Usage: L<servo> <value>  or  H<servo> <value>  e.g. L0 200"));
    }
    return;
  }

  Serial.println(F("Unknown command. Use L<servo> <val>, H<servo> <val>, A<val>, R, P, or S."));
}

// Parks every servo at an arbitrary position. Doesn't touch the
// stored low/high config or SPIFFS - purely a physical move.
void parkAllServos(long value) {
  Serial.print(F("Parking all servos at "));
  Serial.println(value);
  for (int servo = 0; servo < NUM_SERVOS; servo++) {
    pwm.setPWM(servo, 0, value);
  }
}

void printServos() {
  Serial.println(F("Current Servo Config (count,low,high,eject):"));
  for (int x = 0; x < NUM_SERVOS; x++) {
    Serial.print(F("  Servo "));
    Serial.print(x);
    Serial.print(F(": "));
    Serial.print(servoCount[x]);
    Serial.print(F(","));
    Serial.print(servos[x][0]);
    Serial.print(F(","));
    Serial.print(servos[x][1]);
    Serial.print(F(","));
    Serial.println(servos[x][2]);
  }
}

// ---- SPIFFS load/save - same file format as servos.ino ----

void load_servos(fs::FS &fs, const char * path) {
  Serial.print(F("Loading Servos: "));
  Serial.print(path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println(F(" - failed to open file for reading"));
    Serial.println(F("Creating Default servos."));
    default_servos();
    return;
  } else {
    Serial.println(F(" - Success!"));
  }

  byte counter1 = 0;
  byte counter2 = 0;
  int counter3 = 0;

  while (file.available()) {
    char temp = file.read();

    if (temp == ',') {
      if (counter2 == 0) // it's the first number: move count for this servo.
        servoCount[counter1] = counter3;
      else
        servos[counter1][counter2 - 1] = counter3;
      counter2++;
      counter3 = 0;
    } else if (temp == '\n') {
      servos[counter1][counter2 - 1] = counter3;
      counter1++;
      counter2 = 0;
      counter3 = 0;
    } else if (temp == '\r') {
      // skip carriage return
    } else {
      counter3 = counter3 * 10;
      counter3 = counter3 + temp - 48;
    }
  }

  if (counter3 != 0) {
    servos[counter1][counter2 - 1] = counter3;
  }

  file.close();

  Serial.println(F("Servo Load Complete."));
}

void default_servos() {
  for (int x = 0; x < NUM_SERVOS; x++) {
    servos[x][0] = 150; // low
    servos[x][1] = 520; // high
    servos[x][2] = 600; // eject - unused here, kept for file compatibility
  }

  save_servos(SPIFFS, "/servos.txt");
}

void save_servos(fs::FS &fs, const char * path) {
  Serial.print(F("Saving Servo Data: "));
  Serial.print(path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println(F(" - failed to open file for writing"));
    return;
  } else {
    Serial.println(F(" - Success!"));
  }

  for (int x = 0; x < NUM_SERVOS; x++) {
    file.print(servoCount[x]);
    for (int y = 0; y < 3; y++) {
      file.print(F(","));
      file.print(servos[x][y]);
    }
    file.println();
  }

  file.close();
}