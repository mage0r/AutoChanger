// All the arm-sensor (carriage IR sensor) related debounce/pass-counter globals live here.
// The ARM pin itself stays with the other pin allocations in the main .ino file.
// See arm.ino (the debounce/pass logic) and interrupts.ino (the ISR that sets armTrigger).

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
byte armCounter = 0;  // We ignore every second pass.
boolean armTrigger = false;

// Tracks which arms are currently at their HIGH (active) position, independent of
// the automatic pattern sequence - used by the AYAB-facing ARMS query and S<arm>
// command (see commands.ino). Set unconditionally at the top of moveServo() so it
// stays accurate regardless of which of the many callers (the sequence, a test
// cycle, EJECT_ALL, these new commands, the web UI) triggered the move, and
// regardless of moveServo()'s own "skip if already at that PWM value" optimisation -
// the commanded position is what this tracks, not just whether a write happened.
bool armUp[4] = {false, false, false, false};

volatile bool servoTestPending = false;
int servoTestArm = -1;
boolean servosDefaulted = false; // set true by default_servos() when /servos.txt wasn't found
int armTestCount = 0;
bool armTestAll = false;