#ifndef config_h
#define config_h

#define MAX_DEVICES 4
#define MAX_KEYMAP_CODE 255
#define MAX_KEYMAP (MAX_KEYMAP_CODE + 1)
#define MAX_SEQUENCE 4

/**
 * The configuration file path.
 * */
extern char configuration_file_path[256];

/**
 * Sequence of key codes.
 * */
struct key_output
{
    int sequence[MAX_SEQUENCE];
};
extern struct key_output hyper_keymap[MAX_KEYMAP];

/**
 * Automatically reload the configuration file when modified.
 * */
extern int automatic_reload;

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
    struct key_output keymap[MAX_KEYMAP];
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

#endif
