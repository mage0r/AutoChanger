// All the arm-sensor (carriage IR sensor) related debounce/pass-counter globals live here.
// The ARM pin itself stays with the other pin allocations in the main .ino file.
// See arm.ino (the debounce/pass logic) and interrups.ino (the ISR that sets armTrigger).

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
byte armCounter = 0;  // We ignore every second pass.
boolean armTrigger = false;