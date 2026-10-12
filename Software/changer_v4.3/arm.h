// All the arm-sensor (carriage IR sensor) related debounce/pass-counter globals live here.
// The ARM pin itself stays with the other pin allocations in the main .ino file.
// See interrupts.ino (the ISR that counts edges and debounces them) and arm.ino
// (process_sensor(), which acts on the counted passes from loop()).

// How long the sensor must be quiet before the next edge counts as a new pass (see
// interrupts.ino). 400ms: one pass's chatter can have gaps of ~300ms, and real passes
// are well over a second apart. Increase if one pass logs as two.
unsigned long debounceDelay = 400;
byte armCounter = 0;  // We ignore every second pass.

// Written by the sensor interrupt, read by loop() - volatile so loop() always reads
// the current value. Counters only ever go up; loop() works out what's new by
// comparing with what it saw last time, so no pass is lost or merged however late
// loop() gets to it.
volatile uint32_t sensorEdges = 0;          // every falling edge the sensor produced
volatile uint32_t sensorPasses = 0;         // edges accepted as a carriage pass (debounced)
volatile unsigned long lastEdgeAt = 0;      // millis() of the last edge of any kind

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