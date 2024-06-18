#ifndef config_h
#define config_h

#include <linux/uinput.h>
#include <stdint.h>
#include <sys/time.h>

#define MAX_LEDBIT LED_CHARGING

#define MAX_DEVICES 4
// Layer indices are uint8_t and some are offset by one to use zero as undefined
#define MAX_LAYERS 255
#define MAX_LAYER_NAME 61
#define MAX_KEYMAP_CODE 255
#define MAX_KEYMAP (MAX_KEYMAP_CODE + 1)
#define MAX_LAYER_LEDS 8
#define MAX_SEQUENCE 5
#define MAX_SEQUENCE_UKEY 3 * 3
#define MAX_SEQUENCE_UKEY_STR 3 * 256
#define MAX_SEQUENCE_OVERLOAD_MOD (MAX_SEQUENCE - 3)

/**
 * The configuration file path.
 * */
extern char configuration_file_path[256];

/**
 * Automatically reload the configuration file when modified.
 * */
extern int automatic_reload;

/**
 * Input methods for unicode codepoints.
 * */
enum input_method
{
    input_method_none,
    input_method_compose,
    input_method_iso14755,
    input_method_gtk,
};
extern enum input_method ukey_input_method;

/**
 * Prefix key for codepoints using the compose input method.
 * */
extern uint16_t ukey_compose_key;

/**
 * Microsecond delay between sending each codepoint.
 * */
extern int ukeys_delay;

/**
 * Frequency and duration of beeps when disabled keys are pressed.
 * */
extern int beep_on_disabled_press_frequency;
extern int beep_on_disabled_press_duration_ms;

/**
 * Frequency and duration of beeps when non-ASCII codepoints are pressed in the 'none' input method.
 * */
extern int beep_on_invalid_codepoint_frequency;
extern int beep_on_invalid_codepoint_duration_ms;

/**
 * Array of codepoint strings for ACTION_UKEYS_STR actions.
 * */
extern uint8_t** codepoint_strings;

/**
 * Key action.
 * */
enum action_kind
{
    ACTION_TRANSPARENT,     // pass-through to lower layer
    ACTION_DISABLED,        // do nothing
    ACTION_KEY,             // emit a single key code
    ACTION_KEYS,            // emit multiple key codes
    ACTION_UKEY,            // emit a single unicode key sequence
    ACTION_UKEYS,           // emit multiple unicode key sequences
    ACTION_UKEYS_STR,       // emit long string of unicode key sequences
    ACTION_OVERLOAD_MOD,    // press keys on hold, or emit a single key code on tap
    ACTION_OVERLOAD_LAYER,  // activate layer on hold, or emit a single key code on tap
    ACTION_SHIFT_LAYER,     // activate layer on hold
    ACTION_LATCH_LAYER,     // activate layer on hold, or activate layer for a single key press after released
    ACTION_LATCH_MENU,      // activate nearest Menu layer on hold, or activate layer for a single key press after released
    ACTION_LOCK_LAYER,      // activate layer on hold, or activate layer after released until unlocked
    ACTION_UNLOCK,          // unlock locked layer or all activations
    ACTION_INPUT_METHOD,    // change input method
};
struct action
{
    enum action_kind kind;
    union {
        struct {
            uint16_t code;
        } key;
        struct {
            uint16_t codes[MAX_SEQUENCE];
        } keys;
        struct {
            uint8_t codepoint[3];
        } ukey;
        struct {
            uint8_t codepoints[MAX_SEQUENCE_UKEY];
        } ukeys;
        struct {
            uint16_t codepoint_string_index;
            uint16_t length;
        } ukeys_str;
        struct {
            uint16_t codes[MAX_SEQUENCE_OVERLOAD_MOD];
            uint16_t code;
            uint16_t timeout_ms; // milliseconds
        } overload_mod;
        struct {
            uint8_t layer_index;
            uint16_t code;
            uint16_t timeout_ms; // milliseconds
        } overload_layer;
        struct {
            uint8_t layer_index;
        } shift_layer;
        struct {
            uint8_t layer_index;
        } latch_layer;
        struct {
            uint8_t layer_index;
            uint8_t is_overlay;
        } lock_layer;
        struct {
            uint8_t all;
        } unlock;
        struct {
            enum input_method mode;
        } input_method;
    } data;
};

/**
 * Key layer.
 * */
struct layer
{
    uint8_t index;
    uint8_t device_index;
    uint8_t is_layout;
    char name[MAX_LAYER_NAME];
    struct layer* parent_layer;
    struct layer* menu_layer;
    struct action keymap[MAX_KEYMAP];
    uint8_t leds[MAX_LAYER_LEDS];
    uint8_t mod_layers[15]; // layer indices offset by 1, zero represents no modifier layer
};
extern struct layer* layers[MAX_LAYERS];
extern struct layer* transparent_layer;

/**
 * Layer activation.
 * */
enum activation_kind
{
    ACTIVATION_OVERLOAD_MOD,
    ACTIVATION_OVERLOAD_LAYER,
    ACTIVATION_SHIFT_LAYER,
    ACTIVATION_LATCH_LAYER,
    ACTIVATION_LOCK_LAYER,
};
struct activation
{
    struct layer* layer;
    struct activation* prev;
    struct activation* next;
    struct action* action; // the key's action after released
    enum activation_kind kind;
    uint8_t code; // key code that activated the layer
    union {
        struct {
            uint8_t active; // after second event
            uint16_t delayed_code; // delay first key press
            struct timeval timeout_timestamp;
        } overload_mod;
        struct {
            uint8_t active; // after second event
            uint16_t delayed_code; // delay first key press
            struct timeval timeout_timestamp;
        } overload_layer;
        struct {
            uint8_t layer_index;
            uint8_t is_overlay;
        } lock_layer;
    } data;
};

/**
 * The input device.
 * */
struct input_device
{
    char name[256];
    int number;
    char event_path[256];
    int file_descriptor;
    int remap[MAX_KEYMAP];
    struct layer* layer;
    uint8_t pressed[MAX_KEYMAP]; // layer indices (+1) of each pressed key code
    struct activation* top_activation;
    uint8_t inherit_remap;
    int leds[MAX_LEDBIT]; // state of each led
};
extern struct input_device input_devices[MAX_DEVICES];
extern int nr_input_devices;

/**
 * Finds the configuration file location.
 * */
int find_configuration_file();

/**
 * Check if argument is an integer.
 * */
int is_integer(char* token);

/**
 * Reads the configuration file.
 * */
int read_configuration();

/**
 * Register an input device.
 */
struct input_device* registerInputDevice(int lineno, const char* name, int number, struct layer* layer);

/**
 * Finalize the keymap and remap arrays in an input device.
 */
void finalizeInputDevice(struct input_device* device, int* remap);

/**
 * Remap bindings to maintain compatibility with existing [Bindings].
 */
void remapBindings(int* remap, struct layer* layer);

/**
 * Disable key in layer.
 */
void setLayerActionDisabled(struct layer* layer, int key);

/**
 * Set key or key sequence in layer.
 */
void setLayerKey(struct layer* layer, int key, unsigned int length, uint16_t* sequence);

/**
 * Set codepoint or codepoint sequence in layer.
 */
void setLayerUKey(struct layer* layer, int key, unsigned int length, uint8_t* sequence);

/**
 * Set overload-mod key in layer.
 */
void setLayerActionOverloadMod(struct layer* layer, int key, int lineno, unsigned int length, uint16_t* sequence, uint16_t to_code, uint16_t timeout_ms);

/**
 * Set overload-layer key in layer.
 */
void setLayerActionOverload(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path, uint16_t to_code, uint16_t timeout_ms);

/**
 * Set shift-layer key in layer.
 */
void setLayerActionShift(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path);

/**
 * Set latch-layer key in layer.
 */
void setLayerActionLatch(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path);

/**
 * Set latch-menu key in layer, for current [Menu] layer.
 */
void setLayerActionLatchMenu(struct layer* layer, int key, int lineno);

/**
 * Set lock-layer key in layer.
 */
void setLayerActionLock(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path, uint8_t is_overlay);

/**
 * Set unlock key in layer.
 */
void setLayerActionUnlock(struct layer* layer, int key, uint8_t all);

/**
 * Set input-method key in layer.
 */
void setLayerActionInputMethod(struct layer* layer, int key, enum input_method mode);

/**
 * Register a layer.
 */
struct layer* registerLayer(int lineno, struct layer* parent_layer, char* name);

#endif
