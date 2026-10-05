/*
 * Secondary I2C bus (SDA on pin 2, SCL on pin 7) - lets another board run commands
 * against this one over I2C, reusing the exact same command interpreter as Serial
 * (see commands.ino). This device is the I2C peripheral; something else is the
 * controller.
 *
 * Protocol:
 *   - Write a command's text as the I2C payload, e.g. the ASCII bytes "INFO" - no
 *     trailing newline needed, one I2C write transaction is one command.
 *   - Then read back the response. The first byte tells you whether it's ready:
 *       0x00 - not ready yet (the command hasn't been picked up and processed by
 *              loop() yet) - wait a moment and read again.
 *       0x01 - ready; every byte after this one is the response text, exactly what
 *              Serial would have printed for the same command (e.g.
 *              "INFO:OK\nProject: AutoChanger - V.4.2\n...").
 *   - A ready response stays available - repeated reads return the same bytes -
 *     until the next command is written, at which point it reverts to "not ready"
 *     until that new command has been processed.
 *
 * Why "not ready" exists at all rather than answering immediately: onReceive() and
 * onRequest() below run in the Wire library's own callback context, where doing the
 * command's actual work (SPIFFS access, WiFi, moving a servo, ESP.restart()...)
 * isn't safe - the same restriction Serial has, where readSerialCommands() is only
 * ever called from loop(), never from an interrupt. So onReceive() here only ever
 * captures the command's bytes (cheap, safe) and readI2CCommands() - called from
 * loop(), same as its Serial equivalent - does the real work and prepares the
 * response that onRequest() can then safely hand back.
 *
 * Response size: the ESP32 Arduino core's Wire library has a fixed 128-byte TX
 * buffer (2 of which are address overhead), and this doesn't attempt to raise that
 * via Wire::setBufferSize() - not every core version has it, and working within the
 * guaranteed default avoids a dependency on which version is installed. A command
 * whose normal output is longer than I2C2_RESPONSE_MAX (CAT on a larger file, or
 * INFO's full multi-line output) comes back truncated, not corrupted - there's just
 * no status line at the end to prove it reached a clean finish. PUT is refused
 * outright rather than truncated - see the guard in cmd_put() in commands.ino.
 */

#include <Wire.h>

// I2C2_SDA / I2C2_SCL / I2C2_ADDRESS_DEFAULT / I2C2_RESPONSE_MAX are defined in
// changer_v4_2.ino, alongside the rest of the port/pin allocations. The address
// actually used, i2c2_address, is a runtime variable (also declared there) so it
// can be overridden from config.ini - see assign_config()/save_config() in
// config.ino.

TwoWire I2CSecondary = TwoWire(1);

// A small Print sink - cmdOut (see commands.ino) gets pointed here for the span of
// one I2C command, so whatever the interpreter would normally have sent to Serial
// lands here instead, ready for onRequest() to hand back.
class I2CResponseBuffer : public Print {
  public:
    size_t write(uint8_t c) override {
      if(len >= I2C2_RESPONSE_MAX)
        return 0; // silently drops anything past the cap - truncated, not corrupted
      buf[len++] = c;
      return 1;
    }
    size_t write(const uint8_t *data, size_t size) override {
      size_t n = 0;
      while(n < size && write(data[n]))
        n++;
      return n;
    }
    void reset() { len = 0; }
    const uint8_t* data() const { return buf; }
    size_t length() const { return len; }
  private:
    uint8_t buf[I2C2_RESPONSE_MAX];
    size_t len = 0;
};

I2CResponseBuffer i2cResponse;

volatile bool i2cCommandPending = false;
String i2cPendingCommand = "";
volatile bool i2cResponseReady = false;

// Runs in the Wire library's receive context - kept deliberately minimal: just
// copies the bytes it was given into a String and raises a flag. No SPIFFS/WiFi
// access, no calling processCommand() - see the file header for why.
void onI2CReceive(int numBytes) {
  String cmd = "";
  while(I2CSecondary.available()) {
    char c = I2CSecondary.read();
    if(c != '\r' && c != '\n') // tolerate a trailing newline if the controller sends one
      cmd += c;
  }
  cmd.trim();
  if(cmd.length() == 0)
    return;

  i2cPendingCommand = cmd;
  i2cResponseReady = false; // the previous response is now stale
  i2cCommandPending = true;
}

// Also runs in the Wire library's callback context - just hands back whatever's
// already in i2cResponse, prefixed with the ready/not-ready status byte. No command
// work happens here; readI2CCommands() (from loop()) is what fills that buffer.
void onI2CRequest() {
  if(i2cResponseReady) {
    I2CSecondary.write((uint8_t)0x01);
    I2CSecondary.write(i2cResponse.data(), i2cResponse.length());
  } else {
    I2CSecondary.write((uint8_t)0x00);
  }
}

void setup_i2c_secondary() {
  I2CSecondary.onReceive(onI2CReceive);
  I2CSecondary.onRequest(onI2CRequest);
  I2CSecondary.begin(i2c2_address, I2C2_SDA, I2C2_SCL, 100000);
}

// Called from cmd_i2cEnable() (commands.ino, I2C_ENABLE OFF) - lives here, not there,
// so the I2CSecondary object itself stays local to this file; commands.ino only ever
// calls this wrapper, the same way it only ever sets the i2c_enabled flag rather than
// touching Wire directly.
void teardown_i2c_secondary() {
  I2CSecondary.end();
  i2cCommandPending = false;
  i2cResponseReady = false;
}

// Call every loop() - the I2C equivalent of readSerialCommands(). Does the actual
// work (processCommand(), which may touch SPIFFS/WiFi/servos/etc) from a safe
// context, unlike the two callbacks above.
void readI2CCommands() {
  if(!i2cCommandPending)
    return;
  i2cCommandPending = false;

  i2cResponse.reset();
  Print *previousOut = cmdOut;
  cmdOut = &i2cResponse;
  processingViaI2C = true;

  processCommand(i2cPendingCommand);

  processingViaI2C = false;
  cmdOut = previousOut;
  i2cResponseReady = true;
}