#ifndef leds_h
#define leds_h

#include "config.h"

/**
 * Output the led list to console.
 * */
void outputLedList();

/**
 * Converts an led string (e.g. "numlock") to its corresponding code.
 * */
int convertLedStringToCode(char* ledString);

/**
 * Set an led on input device.
 * */
void set_led(struct input_device* device, int led, int state);

#endif
