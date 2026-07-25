/*
 * Interactive tool to find PWM tick values for a standard 180-degree
 * positional servo's key angles (closed, open, eject, etc.) on the
 * Adafruit PCA9685.
 *
 * Type:  <channel> <value>   and press Enter, e.g.:
 *   0 300
 * moves the servo on channel 0 to tick value 300 and holds it there.
 * Watch the physical position after each command and narrow in on the
 * values you want for each position. This tool doesn't save anything -
 * just note down the numbers as you find them.
 *
 * Type just "s" to print the last value sent to each channel, as a
 * quick reference while you're working through all 4 servos.
 *
 * Start with values roughly in the 100-600 range (typical for standard
 * servo timing at 60Hz) and adjust from there.
 * 
 * for loading Autochanger, set the minimum to 200 and adjust down.
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

int lastValue[4] = {-1, -1, -1, -1};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Servo position calibration tool.");
  Serial.println("Type: <channel> <value>  e.g.  0 300");
  Serial.println("Channels 0-3. Try values 100-600 to start.");
  Serial.println("Type 's' to show the last value sent to each channel.");

  pwm.begin();
  pwm.setPWMFreq(60);
}

void loop() {
  if (Serial.available()) {

    if (Serial.peek() == 's' || Serial.peek() == 'S') {
      Serial.read();
      printStatus();
    } else {
      int channel = Serial.parseInt();
      int value = Serial.parseInt();

      if (channel >= 0 && channel <= 3 && value > 0) {
        Serial.print("Servo ");
        Serial.print(channel);
        Serial.print(" -> tick value ");
        Serial.println(value);
        pwm.setPWM(channel, 0, value);
        lastValue[channel] = value;
      } else {
        Serial.println("Enter as: <channel 0-3> <value>, e.g. 0 300  (or 's' for status)");
      }
    }

    // clear any trailing whitespace/newline so it's not misread next time
    while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r' || Serial.peek() == ' ')) {
      Serial.read();
    }
  }
}

void printStatus() {
  Serial.println("Last value sent per channel:");
  for (int i = 0; i < 4; i++) {
    Serial.print("  Servo ");
    Serial.print(i);
    Serial.print(": ");
    if (lastValue[i] == -1) {
      Serial.println("(not set yet)");
    } else {
      Serial.println(lastValue[i]);
    }
  }
}