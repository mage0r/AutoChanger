/*
 * Standalone test: press a button on IO4 to run the servo sequence
 * test (cycles servos 0-3 between tick values 150 and 550, 10 times
 * through, one servo at a time).
 *
 * Assumes a normally-open button between IO4 and GND, using the
 * internal pull-up (same wiring convention as PGRM_BTN in the main
 * project) - so a press reads as a falling edge.
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Bounce2.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

const int NUM_SERVOS = 4;
const int POS_LOW = 200;
const int POS_HIGH = 550;
const int CYCLES = 10;
const int MOVE_DELAY = 500; // ms to wait after each move
const int TRIGGER_PIN = 4;  // IO4

Bounce triggerButton = Bounce();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Servo sequence test ready - press the button on IO4 to run.");

  pwm.begin();
  pwm.setPWMFreq(60);

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  triggerButton.attach(TRIGGER_PIN);
  triggerButton.interval(25);
}

void loop() {
  triggerButton.update();

  if (triggerButton.fell()) {
    runSequenceTest();
  }
}

void runSequenceTest() {
  Serial.println("Button pressed - starting sequence test.");

  for (int cycle = 1; cycle <= CYCLES; cycle++) {
    Serial.print("Cycle ");
    Serial.print(cycle);
    Serial.print(" of ");
    Serial.println(CYCLES);

    for (int servo = 0; servo < NUM_SERVOS; servo++) {
      Serial.print("  Servo ");
      Serial.print(servo);
      Serial.println(" -> 150");
      pwm.setPWM(servo, 0, POS_LOW);
      delay(MOVE_DELAY);

      Serial.print("  Servo ");
      Serial.print(servo);
      Serial.println(" -> 550");
      pwm.setPWM(servo, 0, POS_HIGH);
      delay(MOVE_DELAY);
    }
  }

  Serial.println("Sequence test complete. Press button to run again.");
}