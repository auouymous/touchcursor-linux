#ifndef beep_h
#define beep_h

/**
 * Open PC Speaker.
 * */
void openSpeaker();

/**
 * Close PC Speaker.
 * */
void closeSpeaker();

/**
 * Play a sound on PC Speaker.
 * */
void beep(int frequency, int duration_ms);

#endif
