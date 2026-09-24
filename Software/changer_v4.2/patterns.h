// All the pattern-related types and globals live here.

#define MAX_PATTERN_LENGTH 99 // longest sequence a single program can hold

#define DISPLAY_WINDOW 7 // how many steps the main screen shows at once (see build_temp_pattern())

// One pattern is a null-terminated string of '0'-'3' digit characters, one per step.
// length is cached (rather than recomputed with strlen()) since it's read on every
// display refresh and encoder tick.
struct PatternData {
  char steps[MAX_PATTERN_LENGTH+1];
  byte length;
};

// Array of patterns to store our pattern.
int currentPattern = 0;
PatternData patterns[4];
byte displayPattern[DISPLAY_WINDOW];

uint8_t servonum_temp; // declare this once and hopefully don't overrun my buffers - used by build_temp_pattern()