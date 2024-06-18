#ifndef config_h
#define config_h

#include <stdint.h>

#define MAX_DEVICES 4
#define MAX_KEYMAP_CODE 255
#define MAX_KEYMAP (MAX_KEYMAP_CODE + 1)
#define MAX_SEQUENCE 5

/**
 * The configuration file path.
 * */
extern char configuration_file_path[256];

/**
 * Automatically reload the configuration file when modified.
 * */
extern int automatic_reload;

/**
 * Key action.
 * */
enum action_kind
{
    ACTION_TRANSPARENT,     // pass-through to lower layer
    ACTION_KEY,             // emit a single key code
    ACTION_KEYS,            // emit multiple key codes
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
    } data;
};
extern struct action hyper_keymap[MAX_KEYMAP];

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
    struct action keymap[MAX_KEYMAP];
};
extern struct input_device input_devices[MAX_DEVICES];
extern int nr_input_devices;

/**
 * The hyper key.
 * */
extern int hyperKey;

/**
 * Finds the configuration file location.
 * */
int find_configuration_file();

/**
 * Reads the configuration file.
 * */
int read_configuration();

/**
 * Register an input device.
 */
struct input_device* registerInputDevice(int lineno, const char* name, int number);

/**
 * Finalize the keymap and remap arrays in an input device.
 */
void finalizeInputDevice(struct input_device* device, int* remap);

/**
 * Remap bindings to maintain compatibility with existing [Bindings].
 */
void remapBindings(int* remap);

/**
 * Set key or key sequence in layer.
 */
void setLayerKey(int key, unsigned int length, uint16_t* sequence);

#endif
