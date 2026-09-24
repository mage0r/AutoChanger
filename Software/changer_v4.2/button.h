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
unsigned long btnTimeStamps[4] = {0,0,0,0};