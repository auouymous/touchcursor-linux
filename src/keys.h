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

/**
 * Converts a key code to its corresponding string.
 * */
const char* convertKeyCodeToString(int keyCode);

#define IS_PRESS(value) (value == 1)
#define IS_REPEAT(value) (value == 2)
#define IS_RELEASE(value) (value == 0)

/**
 * Checks if the key is a modifier key.
 * */
int isModifier(int code);

#define MOD_LEFTSHIFT  ((uint8_t)0x01)
#define MOD_RIGHTSHIFT ((uint8_t)0x02)
#define MOD_LEFTCTRL   ((uint8_t)0x04)
#define MOD_RIGHTCTRL  ((uint8_t)0x08)
#define MOD_LEFTALT    ((uint8_t)0x10)
#define MOD_RIGHTALT   ((uint8_t)0x20)
#define MOD_LEFTMETA   ((uint8_t)0x40)
#define MOD_RIGHTMETA  ((uint8_t)0x80)

#define MOD_SHIFT ((uint8_t)0x03)
#define MOD_CTRL  ((uint8_t)0x0C)
#define MOD_ALT   ((uint8_t)0x30)
#define MOD_META  ((uint8_t)0xC0)

/**
 * Modifier key list.
 * */
extern const uint16_t modifier_key_list[];

/**
 * Modifier bit list.
 * */
extern const uint8_t modifier_bit_list[];

/**
 * Return modifier bit for key code.
 * */
uint8_t modifierKeyCodeToBit(uint8_t keyCode);

/**
 * Output modifier states.
 * */
extern uint8_t output_modifier_states;

#define IS_MODIFIER_LOCKED(modifiers) ((locked_modifiers & (modifiers)) == (modifiers))

/**
 * Locked modifiers.
 * */
extern uint8_t locked_modifiers;

/**
 * Toggle output modifier state, and return true if event should not be emitted.
 * */
int toggleOutputModifierState(int code, int value);

#endif
