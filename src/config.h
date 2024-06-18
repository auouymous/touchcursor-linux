#ifndef config_h
#define config_h

#include <stdint.h>

#define MAX_DEVICES 4
// Layer indices are uint8_t and some are offset by one to use zero as undefined
#define MAX_LAYERS 255
#define MAX_LAYER_NAME 62
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
    ACTION_DISABLED,        // do nothing
    ACTION_KEY,             // emit a single key code
    ACTION_KEYS,            // emit multiple key codes
    ACTION_OVERLOAD_LAYER,  // activate layer on hold, or emit a single key code on tap
    ACTION_SHIFT_LAYER,     // activate layer on hold
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
            uint8_t layer_index;
            uint16_t code;
        } overload_layer;
        struct {
            uint8_t layer_index;
        } shift_layer;
    } data;
};

/**
 * Key layer.
 * */
struct layer
{
    uint8_t index;
    uint8_t device_index;
    char name[MAX_LAYER_NAME];
    struct layer* parent_layer;
    struct action keymap[MAX_KEYMAP];
};
extern struct layer* layers[MAX_LAYERS];

/**
 * Layer activation.
 * */
enum activation_kind
{
    ACTIVATION_OVERLOAD_LAYER,
    ACTIVATION_SHIFT_LAYER,
};
struct activation
{
    struct layer* layer;
    struct activation* prev;
    struct activation* next;
    enum activation_kind kind;
    uint8_t code; // key code that activated the layer
    union {
        struct {
            uint8_t active; // after second event
            uint16_t delayed_code; // delay first key press
        } overload_layer;
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
};
extern struct input_device input_devices[MAX_DEVICES];
extern int nr_input_devices;

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
 * Set overload-layer key in layer.
 */
void setLayerActionOverload(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path, uint16_t to_code);

/**
 * Set shift-layer key in layer.
 */
void setLayerActionShift(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path);

/**
 * Register a layer.
 */
struct layer* registerLayer(int lineno, struct layer* parent_layer, char* name);

#endif
