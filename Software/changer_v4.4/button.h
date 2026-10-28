// All the button-debounce state globals live here. The button/encoder pin allocations
// stay with the other pin defines in the main .ino file.

// button states
byte buttonState[4] = {false,false,false,false};
byte buttonExec[4] = {false,false,false,false};
boolean pgrmState = false;

// Debouncing
Bounce * debouncer = new Bounce[4];
byte previousButton;
Bounce pgrmDebouncer = Bounce();
unsigned long buttonPressTimeStamp;
boolean triggerPgrm = false;
boolean pgrmExec = false;
unsigned long pgrmTimeStamp = 0;
// A short press is acted on when the button is RELEASED, so a long press (2s, switch
// program) never also counts as a short one. buttonShort[] is set on release unless
// the long press already fired, and cleared at the end of every checkButtons().
bool buttonShort[4] = {false,false,false,false};
bool buttonLongFired[4] = {false,false,false,false};