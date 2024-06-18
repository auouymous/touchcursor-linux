#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "binding.h"
#include "buffers.h"
#include "config.h"
#include "keys.h"
#include "strings.h"

char configuration_file_path[256];
int automatic_reload;

struct layer* layers[MAX_LAYERS];
static int nr_layers = 0;

struct input_device input_devices[MAX_DEVICES];
int nr_input_devices;

/**
 * Find layer by path name.
 * */
static struct layer* find_layer(int lineno, char* path)
{
    for (int i = 0; i < nr_layers; i++)
    {
        if (strcmp(layers[i]->name, path) == 0)
        {
            return layers[i];
        }
    }

    return NULL;
}

/**
 * Checks for the device number if it is configured.
 * Also removes the trailing number configuration from the input.
 * */
static int get_device_number(char* device_config_value)
{
    int device_number = 1;
    int length = strlen(device_config_value);
    for (int i = length - 1; i >= 0; i--)
    {
        if (device_config_value[i] == '\0') break;
        if (device_config_value[i] == '"') break;
        if (device_config_value[i] == ':')
        {
            device_number = atoi(device_config_value + i + 1);
            device_config_value[i] = '\0';
        }
    }
    return device_number;
}

/**
 * Checks if a file exists.
 * */
static int file_exists(const char* const path)
{
    int result = access(path, F_OK);
    if (result < 0)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/**
 * Finds the configuration file location.
 * */
int find_configuration_file()
{
    configuration_file_path[0] = '\0';
    char* home_path = getenv("HOME");
    if (!home_path)
    {
        error("error: home path environment variable not specified\n");
    }
    else
    {
        strcat(configuration_file_path, home_path);
        strcat(configuration_file_path, "/.config/touchcursor/touchcursor.conf");
    }
    if (!file_exists(configuration_file_path))
    {
        strcpy(configuration_file_path, "/etc/touchcursor/touchcursor.conf");
    }
    if (file_exists(configuration_file_path))
    {
        log("info: found the configuration file: %s\n", configuration_file_path);
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

enum sections
{
    configuration_none,
    configuration_device,
    configuration_remap,
    configuration_hyper,
    configuration_bindings,
    configuration_invalid
};

/**
 * Parse a line in a remap section.
 * */
static void parse_remap(char* line, int lineno, int* remap)
{
    char* tokens = line;
    char* token = strsep(&tokens, "=");
    int fromCode = convertKeyStringToCode(token);
    if (fromCode == 0)
    {
        error("error[%d]: invalid key: expected a single key: %s\n", lineno, token);
        return;
    }
    if (fromCode > MAX_KEYMAP_CODE)
    {
        error("error[%d]: left key code must be less than %d: %s\n", lineno, MAX_KEYMAP, token);
        return;
    }
    token = strsep(&tokens, "=");
    int toCode = convertKeyStringToCode(token);
    if (toCode == 0)
    {
        error("error[%d]: invalid key: expected a single key: %s\n", lineno, token);
        return;
    }
    if (toCode > MAX_KEYMAP_CODE)
    {
        error("error[%d]: right key code must be less than %d: %s\n", lineno, MAX_KEYMAP, token);
        return;
    }
    remap[fromCode] = toCode;
}

/**
 * Parse a line in a binding section.
 * */
static void parse_binding(char* line, int lineno, struct layer* layer)
{
    char* tokens = line;
    char* token = strsep(&tokens, "=");
    int fromCode = convertKeyStringToCode(token);
    if (fromCode == 0)
    {
        error("error[%d]: invalid key: expected a single key: %s\n", lineno, token);
        return;
    }
    if (fromCode > MAX_KEYMAP_CODE)
    {
        error("error[%d]: left key code must be less than %d: %s\n", lineno, MAX_KEYMAP, token);
        return;
    }
    uint16_t sequence[MAX_SEQUENCE];
    unsigned int index = 0;
    while ((token = strsep(&tokens, ",")) != NULL)
    {
        if (index >= MAX_SEQUENCE)
        {
            error("error[%d]: exceeded limit of %d keys in sequence: %s\n", lineno, MAX_SEQUENCE, token);
            return;
        }
        int toCode = convertKeyStringToCode(token);
        if (toCode == 0)
        {
            error("error[%d]: invalid key: expected a single key or comma separated list of keys: %s\n", lineno, token);
            return;
        }
        sequence[index++] = toCode;
    }
    if (index == 0)
    {
        error("error[%d]: expected a single key or comma separated list of keys\n", lineno);
        return;
    }
    setLayerKey(layer, fromCode, index, sequence);
}

/**
 * Reads the configuration file.
 * */
int read_configuration()
{
    // Enable automatic reload
    automatic_reload = 1;

    // Reset the input devices
    nr_input_devices = 0;

    // Clear existing hyper key
    int hyperKey = 0;

    // Zero the temporary array
    int remap[MAX_KEYMAP];
    memset(remap, 0, sizeof(remap));

    // Free existing layers and zero the existing array
    for (int i = 0; i < nr_layers; i++) free(layers[i]);
    memset(layers, 0, sizeof(layers));
    nr_layers = 0;

    // This layer is only for compatbility with existing configurations
    struct layer* hyper_layer = NULL;

    // Open the configuration file
    FILE* configuration_file = fopen(configuration_file_path, "r");
    if (!configuration_file)
    {
        error("error: could not open the configuration file\n");
        return EXIT_FAILURE;
    }
    // Parse the configuration file
    char* buffer = NULL;
    size_t length = 0;
    ssize_t result = -1;
    int lineno = 0;
    enum sections section = configuration_none;
    while ((result = getline(&buffer, &length, configuration_file)) != -1)
    {
        lineno++;
        char* line = trim_comment(buffer);
        line = trim_string(line);
        // Comment or empty line
        if (is_comment_or_empty(line))
        {
            continue;
        }
        // Check for section
        if (line[0] == '[')
        {
            size_t line_length = strlen(line);
            if (strncmp(line, "[Device]", line_length) == 0)
            {
                section = configuration_device;
                continue;
            }
            if (strncmp(line, "[Remap]", line_length) == 0)
            {
                section = configuration_remap;
                continue;
            }
            if (strncmp(line, "[Hyper]", line_length) == 0)
            {
                section = configuration_hyper;
                continue;
            }
            if (strncmp(line, "[Bindings]", line_length) == 0)
            {
                if (hyper_layer == NULL)
                {
                    hyper_layer = registerLayer(0, "Bindings");
                    if (hyper_layer == NULL)
                    {
                        section = configuration_invalid;
                        continue;
                    }
                }

                section = configuration_bindings;
                continue;
            }
            if (strncmp(line, "[DisableAutomaticReload]", line_length) == 0)
            {
                section = configuration_none;
                automatic_reload = 0;
                continue;
            }
            error("error[%d]: invalid section: %s\n", lineno, line);
            section = configuration_invalid;
            continue;
        }
        // Read configurations
        switch (section)
        {
            case configuration_device:
            {
                char* name = line;
                int number = get_device_number(name);
                char layer_name[11];
                snprintf(layer_name, 11, "Device %d", nr_input_devices);
                struct layer* layer = registerLayer(0, layer_name);
                if (layer == NULL) continue;
                struct input_device* device = registerInputDevice(lineno, name, number, layer);
                if (device == NULL) continue;
                find_device_event_path(device);
                break;
            }
            case configuration_remap:
            {
                parse_remap(line, lineno, remap);
                break;
            }
            case configuration_hyper:
            {
                char* tokens = line;
                char* token = strsep(&tokens, "=");
                token = strsep(&tokens, "=");
                int code = convertKeyStringToCode(token);
                if (code == 0)
                {
                    error("error[%d]: invalid key: expected a single key: %s\n", lineno, token);
                    continue;
                }
                if (hyperKey != 0)
                {
                    error("error[%d]: hyper key set multiple times\n", lineno);
                    continue;
                }
                hyperKey = code;
                break;
            }
            case configuration_bindings:
            {
                parse_binding(line, lineno, hyper_layer);
                break;
            }
            case configuration_invalid:
            {
                error("error[%d]: ignoring line in invalid section: %s\n", lineno, line);
                continue;
            }
            case configuration_none:
            {
                error("error[%d]: ignoring line not in a section: %s\n", lineno, line);
                continue;
            }
        }
    }
    fclose(configuration_file);
    if (buffer)
    {
        free(buffer);
    }

    for (int d = 0; d < nr_input_devices; d++)
    {
        finalizeInputDevice(&input_devices[d], remap);
    }

    // Remap bindings and hyper key
    if(hyper_layer) remapBindings(remap, hyper_layer);
    hyperKey = remap[hyperKey] != 0 ? remap[hyperKey] : hyperKey;

    // Add overload action to each device for hyperKey
    if (hyper_layer && hyperKey > 0)
    {
        for (int d = 0; d < nr_input_devices; d++)
        {
            setLayerActionOverload(input_devices[d].layer, hyperKey, hyper_layer, hyperKey);
        }
    }

    return EXIT_SUCCESS;
}

/**
 * Register an input device.
 */
struct input_device* registerInputDevice(int lineno, const char* name, int number, struct layer* layer)
{
    for (int d = 0; d < nr_input_devices; d++)
    {
        if (number == input_devices[d].number && strcmp(name, input_devices[d].name) == 0)
        {
            error("error[%d]: duplicate input devices: %s:%i\n", lineno, name, number);
            return NULL;
        }
    }

    struct input_device* device = &input_devices[nr_input_devices];
    strcpy(device->name, name);
    device->number = number;
    device->event_path[0] = '\0';
    device->file_descriptor = -1;
    memset(device->remap, 0, sizeof(device->remap));
    device->layer = layer;
    memset(device->pressed, 0, sizeof(device->pressed));
    device->top_activation = NULL;

    nr_input_devices++;

    return device;
}

/**
 * Finalize the keymap and remap arrays in an input device.
 */
void finalizeInputDevice(struct input_device* device, int* remap)
{
    // Initialize all unset bindings
    for (int k = 0; k < MAX_KEYMAP; k++)
    {
        if (device->layer->keymap[k].kind == ACTION_TRANSPARENT)
        {
            device->layer->keymap[k].kind = ACTION_KEY;
            device->layer->keymap[k].data.key.code = k;
        }
    }

    // Each device inherits the global remap array
    for (int r = 0; r < MAX_KEYMAP; r++)
    {
        // Per-device remapping
        if (device->remap[r] != 0) continue;

        if (remap[r] != 0)
        {
            // Inherit
            device->remap[r] = remap[r];
        }
        else
        {
            // Pass-through
            device->remap[r] = r;
        }
    }
}

/**
 * Remap bindings to maintain compatibility with existing [Bindings].
 */
void remapBindings(int* remap, struct layer* layer)
{
    // This allows bindings in layers to be specified using the remapped keys.
    // While existing [Bindings] continue to work with the unremapped keys.

    struct action keymap[MAX_KEYMAP];
    memset(keymap, 0, sizeof(keymap));

    // Remap bindings
    for (int b = 0; b < MAX_KEYMAP; b++)
    {
        if (layer->keymap[b].kind != ACTION_TRANSPARENT)
        {
            int rb = remap[b] != 0 ? remap[b] : b;
            keymap[rb] = layer->keymap[b];
        }
    }

    for (int b = 0; b < MAX_KEYMAP; b++)
    {
        layer->keymap[b] = keymap[b];
    }
}

/**
 * Set key or key sequence in layer.
 */
void setLayerKey(struct layer* layer, int key, unsigned int length, uint16_t* sequence)
{
    switch (length)
    {
        case 0: break;
        case 1:
        {
            layer->keymap[key].kind = ACTION_KEY;
            layer->keymap[key].data.key.code = sequence[0];
            break;
        }
        default:
        {
            layer->keymap[key].kind = ACTION_KEYS;
            for (int i = 0; i < length; i++)
            {
                layer->keymap[key].data.keys.codes[i] = sequence[i];
            }
            for (int i = length; i < MAX_SEQUENCE; i++)
            {
                layer->keymap[key].data.keys.codes[i] = 0;
            }
            break;
        }
    }
}

/**
 * Set overload-layer key in layer.
 */
void setLayerActionOverload(struct layer* layer, int key, struct layer* to_layer, uint16_t to_code)
{
    layer->keymap[key].kind = ACTION_OVERLOAD_LAYER;
    layer->keymap[key].data.overload_layer.layer_index = to_layer->index;
    layer->keymap[key].data.overload_layer.code = to_code;
}

/**
 * Register a layer.
 */
struct layer* registerLayer(int lineno, char* name)
{
    if (nr_layers >= MAX_LAYERS)
    {
        error("error[%d]: exceeded limit of %d layers: %s\n", lineno, MAX_LAYERS, name);
        return NULL;
    }

    if (strlen(name) >= MAX_LAYER_NAME)
    {
        error("error[%d]: layer name is longer than %d: %s\n", lineno, MAX_LAYER_NAME - 1, name);
        return NULL;
    }

    struct layer* layer = malloc(sizeof(struct layer));
    layer->index = nr_layers;
    layer->device_index = 0xFF; // No device
    strcpy(layer->name, name);
    memset(layer->keymap, 0, sizeof(layer->keymap));

    if (find_layer(lineno, layer->name))
    {
        error("error[%d]: duplicate layer names: %s\n", lineno, layer->name);
    }

    layers[nr_layers] = layer;
    nr_layers++;

    return layer;
}

/**
 * Helper method to print existing keyboard devices.
 * Does not work for bluetooth keyboards.
 */
// Need to revisit this
// void printKeyboardDevices()
// {
//     DIR* directoryStream = opendir("/dev/input/");
//     if (!directoryStream)
//     {
//         printf("error: could not open /dev/input/\n");
//         return; //EXIT_FAILURE;
//     }
//     log("suggestion: use any of the following in the configuration file for this application:\n");
//     struct dirent* directory = NULL;
//     while ((directory = readdir(directoryStream)))
//     {
//         if (strstr(directory->d_name, "kbd"))
//         {
//             printf ("keyboard=/dev/input/by-id/%s\n", directory->d_name);
//         }
//     }
// }
