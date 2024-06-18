#ifndef keys_h
#define keys_h

// These are included for kernel v5.4 support (Ubuntu LTS)
// and can be safely removed after Ubuntu LTS updates (2025! :D)
#ifndef KEY_NOTIFICATION_CENTER
#define KEY_NOTIFICATION_CENTER 0x1bc
#endif
#ifndef KEY_PICKUP_PHONE
#define KEY_PICKUP_PHONE        0x1bd
#endif
#ifndef KEY_HANGUP_PHONE
#define KEY_HANGUP_PHONE        0x1be
#endif
#ifndef KEY_FN_RIGHT_SHIFT
#define KEY_FN_RIGHT_SHIFT      0x1e5
#endif

/**
 * Output the key list to console.
 * */
void outputKeyList();

/**
 * Converts a key string "KEY_I" to its corresponding code.
 * */
int convertKeyStringToCode(char* keyString);

#define IS_PRESS(value) (value == 1)
#define IS_REPEAT(value) (value == 2)
#define IS_RELEASE(value) (value == 0)

/**
 * Checks if the key is a modifier key.
 * */
int isModifier(int code);

#endif
