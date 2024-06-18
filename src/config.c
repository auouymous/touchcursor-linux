#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <regex.h>
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
struct layer* transparent_layer;

struct input_device input_devices[MAX_DEVICES];
int nr_input_devices;

static regex_t re_layer_name;

struct layer_path_reference
{
    int lineno;
    struct layer* parent_layer;
    char path[MAX_LAYER_NAME];
    uint8_t* field;
    struct layer_path_reference *next;
};
static struct layer_path_reference* layer_path_references = NULL;

/**
 * Add a layer path reference.
 * */
static void add_layer_path_reference(int lineno, struct layer* parent_layer, char* path, uint8_t* field)
{
    struct layer_path_reference* p = malloc(sizeof(struct layer_path_reference));
    p->lineno = lineno;
    p->parent_layer = parent_layer;
    strcpy(p->path, path);
    p->field = field;
    p->next = layer_path_references;
    layer_path_references = p;
}

/**
 * Find layer by path name.
 * */
static struct layer* find_layer(int lineno, struct layer* parent_layer, char* path)
{
    char fullpath[2 * MAX_LAYER_NAME];
    if (path[0] == '.')
    {
        if (strlen(parent_layer->name) + strlen(path) >= MAX_LAYER_NAME)
        {
            error("error[%d]: layer path is longer than %d: %s\n", lineno, MAX_LAYER_NAME - 1, path);
            return NULL;
        }

        // Start in parent layer
        snprintf(fullpath, MAX_LAYER_NAME, "%s%s", parent_layer->name, path);
        path = fullpath;
    }

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
    configuration_settings,
    configuration_layer,
    configuration_invalid
};

/**
 * Parse a user layer name.
 * */
static int parse_user_layer(char* line, int lineno, int line_length, struct layer** user_layer, enum sections *section)
{
    if (line[line_length - 1] != ']') return 0;

    line[line_length - 1] = '\0'; // Remove ']'

    // Skip '['
    char* s = &line[1];

    if (regexec(&re_layer_name, s, 0, NULL, 0) != 0)
    {
        error("error[%d]: invalid layer name: %s\n", lineno, s);
        *user_layer = NULL;
        *section = configuration_layer;
    }
    else
    {
        *user_layer = registerLayer(lineno, *user_layer, s);
        *section = configuration_layer;
    }
    return 1;
}

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
 * Parse a sequence of key codes.
 * */
static int parse_key_code_sequence(int lineno, char* tokens, unsigned int max, uint16_t* sequence)
{
    char* token;
    unsigned int index = 0;
    while ((token = strsep(&tokens, ",")) != NULL)
    {
        if (index >= max)
        {
            error("error[%d]: exceeded limit of %d keys in sequence: %s\n", lineno, max, token);
            return 0;
        }
        int toCode = convertKeyStringToCode(token);
        if (toCode == 0)
        {
            error("error[%d]: invalid key: expected a single key or comma separated list of keys: %s\n", lineno, token);
            return 0;
        }
        sequence[index++] = toCode;
    }
    if (index == 0)
    {
        error("error[%d]: expected a single key or comma separated list of keys\n", lineno);
        return 0;
    }
    return index;
}

/**
 * Parse space separated tokens in a line.
 * */
static char* next_argument(char** tokens)
{
    return (*tokens ? strsep(tokens, " ") : "");
}

/**
 * Check if argument has a key, and set value string.
 * */
static int get_key_value_argument(char* token, char* key, char** value)
{
    int key_length = strlen(key);
    if (token[key_length] == '=' && strncmp(token, key, key_length) == 0)
    {
        *value = &token[key_length + 1];
        return 1;
    }
    return 0;
}

/**
 * Check if argument is an integer.
 * */
static int is_integer(char* token)
{
    if (*token == '-') token++;
    while (*token >= '0' && *token <= '9') token++;
    return (*token == '\0');
}

/**
 * Parse integer.
 * */
static int parse_integer(int* value, char* string, int min, int max, char* name, int lineno)
{
    *value = atoi(string);
    if (!is_integer(string) || *value < min || *value > max)
    {
        error("error[%d]: invalid %s: expected %d to %d: %s\n", lineno, name, min, max, string);
        return 0;
    }
    return 1;
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
    if (layer->keymap[fromCode].kind != ACTION_TRANSPARENT)
    {
        warn("warning[%d]: duplicate bindings for key: %s\n", lineno, line);
    }

    if (tokens[0] == '(')
    {
        // Action
        tokens++;
        size_t length = strlen(tokens);
        char* action = tokens;
        if (tokens[length - 1] == ')')
        {
            tokens[length - 1] = '\0'; // Remove ')'
            action = strsep(&tokens, " ");
            if (strcmp(action, "disabled") == 0)
            {
                // (disabled)
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                setLayerActionDisabled(layer, fromCode);
                return;
            }
            if (strcmp(action, "overload") == 0)
            {
                // (overload to_layer [tap=to_code] [timeout=timeout_ms])
                // (overload to_codes [tap=to_code] [timeout=timeout_ms])
                char* to_layer_path = next_argument(&tokens);
                char* to_code_name = "";
                char* timeout_str = "";
                while (tokens)
                {
                    char* arg = next_argument(&tokens);
                    if (get_key_value_argument(arg, "tap", &to_code_name)) continue;
                    if (get_key_value_argument(arg, "timeout", &timeout_str)) continue;
                    error("error[%d]: invalid argument: %s\n", lineno, arg);
                }
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                int to_code = fromCode;
                if (to_code_name[0] != '\0')
                {
                    // Parse optional key code
                    to_code = convertKeyStringToCode(to_code_name);
                    if (to_code == 0)
                    {
                        error("error[%d]: invalid key: expected a single key: %s\n", lineno, to_code_name);
                        return;
                    }
                }

                int timeout_ms = 0;
                if (timeout_str[0] != '\0')
                {
                    // Parse optional timeout
                    if (!parse_integer(&timeout_ms, timeout_str, 0, 65535, "timeout", lineno)) return;
                }

                if (strstr(to_layer_path, ",") != NULL || convertKeyStringToCode(to_layer_path) != 0)
                {
                    uint16_t sequence[MAX_SEQUENCE_OVERLOAD_MOD];
                    unsigned int index = parse_key_code_sequence(lineno, to_layer_path, MAX_SEQUENCE_OVERLOAD_MOD, sequence);
                    if (index == 0) return;
                    setLayerActionOverloadMod(layer, fromCode, lineno, index, sequence, to_code, timeout_ms);
                }
                else
                {
                    setLayerActionOverload(layer, fromCode, NULL, lineno, to_layer_path, to_code, timeout_ms);
                }
                return;
            }
            if (strcmp(action, "shift") == 0)
            {
                // (shift to_layer)
                char* to_layer_path = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                setLayerActionShift(layer, fromCode, NULL, lineno, to_layer_path);
                return;
            }
            if (strcmp(action, "latch") == 0)
            {
                // (latch to_layer)
                char* to_layer_path = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                setLayerActionLatch(layer, fromCode, NULL, lineno, to_layer_path);
                return;
            }
            if (strcmp(action, "lock") == 0)
            {
                // (lock to_layer)
                char* to_layer_path = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                setLayerActionLock(layer, fromCode, NULL, lineno, to_layer_path, 0);
                return;
            }
            if (strcmp(action, "lock-overlay") == 0)
            {
                // (lock-overlay to_layer)
                char* to_layer_path = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                setLayerActionLock(layer, fromCode, NULL, lineno, to_layer_path, 1);
                return;
            }
            if (strcmp(action, "unlock") == 0)
            {
                // (unlock [*])
                char* all_str = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                uint8_t all = 0;
                if (all_str[0] != '\0')
                {
                    // Parse optional '*' to unlock all activations
                    if (strcmp(all_str, "*"))
                    {
                        error("error[%d]: expected no arguments or '*': %s\n", lineno, tokens);
                        return;
                    }
                    all = 1;
                }

                setLayerActionUnlock(layer, fromCode, all);
                return;
            }
        }

        error("error[%d]: invalid action: %s\n", lineno, action);
        return;
    }

    uint16_t sequence[MAX_SEQUENCE];
    unsigned int index = parse_key_code_sequence(lineno, tokens, MAX_SEQUENCE, sequence);
    if (index == 0) return;
    setLayerKey(layer, fromCode, index, sequence);
}

/**
 * Parse a command in a user layer.
 * */
static void parse_command(char* line, int lineno, struct layer* user_layer)
{
    // Skip '('
    line++;

    char* tokens = line;
    size_t length = strlen(tokens);
    char* command = tokens;
    if (tokens[length - 1] == ')')
    {
        tokens[length - 1] = '\0';
        command = strsep(&tokens, " ");
        if (strcmp(command, "device") == 0)
        {
            // (device "name"[:number])
            char* _name = tokens;
            int number = get_device_number(_name);

            if (user_layer->parent_layer)
            {
                error("error[%d]: device command is only valid in a top-level layer: %s:%i\n", lineno, _name, number);
                return;
            }
            if (user_layer->device_index != 0xFF)
            {
                error("error[%d]: multiple device commands in same layer: %s:%i\n", lineno, _name, number);
                return;
            }
            char name[256];
            snprintf(name, 256, "Name=%s", _name);
            struct input_device* device = registerInputDevice(lineno, name, number, user_layer);
            if (device == NULL) return;
            find_device_event_path(device);
            return;
        }
        if (strcmp(command, "inherit-remap") == 0)
        {
            // (inherit-remap)
            if (tokens)
            {
                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                return;
            }

            if (user_layer->device_index == 0xFF)
            {
                error("error[%d]: inherit-remap command is only valid in a device layer\n", lineno);
                return;
            }
            input_devices[user_layer->device_index].inherit_remap = 1;
            return;
        }
        if (strcmp(command, "is-layout") == 0)
        {
            // (is-layout)
            if (tokens)
            {
                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                return;
            }

            if (user_layer->device_index != 0xFF)
            {
                error("error[%d]: is-layout command is not valid in a device layer\n", lineno);
                return;
            }
            user_layer->is_layout = 1;
            return;
        }
    }

    error("error[%d]: invalid command: %s\n", lineno, command);
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
    transparent_layer = NULL;

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
    regfree(&re_layer_name);
    regcomp(&re_layer_name, "^[a-z0-9_-]+$", REG_EXTENDED|REG_NOSUB); // Uppercase names are reserved for sections
    struct layer* user_layer = NULL;
    int* user_remap = NULL;
    int invalid_layer_indent = 0;
    int base_user_layer_indent = 0;
    while ((result = getline(&buffer, &length, configuration_file)) != -1)
    {
        lineno++;
        char* line = trim_comment(buffer);
        if (user_layer)
        {
            line = rtrim_string(line);
        }
        else
        {
            line = trim_string(line);
        }
        // Comment or empty line
        if (is_comment_or_empty(line))
        {
            continue;
        }
        if (user_layer && !isspace(line[0]))
        {
            user_layer = NULL;
            user_remap = NULL;
            invalid_layer_indent = 0;
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
                    hyper_layer = registerLayer(0, NULL, "Bindings");
                    if (hyper_layer == NULL)
                    {
                        section = configuration_invalid;
                        continue;
                    }
                }

                section = configuration_bindings;
                continue;
            }
            if (strncmp(line, "[Settings]", line_length) == 0)
            {
                section = configuration_settings;
                continue;
            }

            if (parse_user_layer(line, lineno, line_length, &user_layer, &section)) continue;

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
                struct layer* layer = registerLayer(0, NULL, layer_name);
                if (layer == NULL) continue;
                struct input_device* device = registerInputDevice(lineno, name, number, layer);
                if (device == NULL) continue;
                find_device_event_path(device);
                device->inherit_remap = 1;
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
            case configuration_settings:
            {
                if (line[0] == '(')
                {
                    // Skip '('
                    line++;

                    char* tokens = line;
                    size_t length = strlen(line);
                    char* setting = tokens;
                    if (tokens[length - 1] == ')')
                    {
                        tokens[length - 1] = '\0';
                        setting = strsep(&tokens, " ");
                        if (strcmp(setting, "disable-automatic-reload") == 0)
                        {
                            // (disable-automatic-reload)
                            if (tokens)
                            {
                                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                                continue;
                            }

                            automatic_reload = 0;
                            continue;
                        }
                    }
                }

                error("error[%d]: invalid setting: %s\n", lineno, line);
                break;
            }
            case configuration_layer:
            {
                if (user_layer == NULL) continue; // Ignore all lines in an invalid layer

                // Count line indent
                char* s = line;
                while (*s == ' ' || *s == '\t') s++;
                int indent = s - line;
                line = s;
                // Count layer indent
                int layer_indent = 1;
                for (struct layer* layer = user_layer; layer->parent_layer != NULL; layer = layer->parent_layer) layer_indent++;
                if (user_remap) layer_indent++;
                // Compare indents
                if (base_user_layer_indent > 0)
                {
                    int want_indent = base_user_layer_indent * layer_indent;
                    while (indent < want_indent && (user_remap || user_layer->parent_layer))
                    {
                        // Return to parent layer from a nested layer
                        if (user_remap)
                        {
                            user_remap = NULL;
                        }
                        else
                        {
                            user_layer = user_layer->parent_layer;
                        }
                        layer_indent -= base_user_layer_indent;
                        want_indent = base_user_layer_indent * layer_indent;
                    }
                    if (indent != want_indent && invalid_layer_indent == 0)
                    {
                        warn("warning[%d]: expected %d indent characters, got %d\n", lineno, want_indent, indent);
                    }
                }
                else
                {
                    // First line in first user layer
                    base_user_layer_indent = indent;
                }

                if (invalid_layer_indent)
                {
                    if (indent > invalid_layer_indent) continue;
                    invalid_layer_indent = 0;
                }

                if (user_remap)
                {
                    // [Remap] binding
                    parse_remap(line, lineno, user_remap);
                    continue;
                }

                if (line[0] == '[')
                {
                    // Nested layer
                    size_t line_length = strlen(line);
                    if (strncmp(line, "[Remap]", line_length) == 0)
                    {
                        if (user_layer->device_index == 0xFF)
                        {
                            error("error[%d]: only device layers can have a [Remap] section\n", lineno);
                            invalid_layer_indent = indent;
                            continue;
                        }
                        user_remap = input_devices[user_layer->device_index].remap;
                        continue;
                    }

                    if (line_length && parse_user_layer(line, lineno, line_length, &user_layer, &section)) continue;

                    error("error[%d]: invalid user layer: %s\n", lineno, line);
                    invalid_layer_indent = indent;
                    continue;
                }
                if (line[0] == '(')
                {
                    // Command
                    parse_command(line, lineno, user_layer);
                    continue;
                }
                // Key = Value
               parse_binding(line, lineno, user_layer);
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

    for (struct layer_path_reference* p = layer_path_references; p != NULL;)
    {
        struct layer_path_reference* next = p->next;
        struct layer* layer = find_layer(p->lineno, p->parent_layer, p->path);
        if (layer)
        {
            *p->field = layer->index;
        }
        else
        {
            error("error[%d]: layer not found: %s\n", p->lineno, p->path);
        }
        free(p);
        p = next;
    }
    layer_path_references = NULL;

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
            if (input_devices[d].layer->name[0] == 'D')
            {
                setLayerActionOverload(input_devices[d].layer, hyperKey, hyper_layer, 0, NULL, hyperKey, 0);
            }
        }
    }

    log("info: found %d layers\n", nr_layers);

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
    device->inherit_remap = 0;

    layer->device_index = nr_input_devices;

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

    int inherit = device->inherit_remap;

    // Each device inherits the global remap array
    for (int r = 0; r < MAX_KEYMAP; r++)
    {
        // Per-device remapping
        if (device->remap[r] != 0) continue;

        if (remap[r] != 0 && inherit)
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
 * Disable key in layer.
 */
void setLayerActionDisabled(struct layer* layer, int key)
{
    layer->keymap[key].kind = ACTION_DISABLED;
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
 * Set overload-mod key in layer.
 */
void setLayerActionOverloadMod(struct layer* layer, int key, int lineno, unsigned int length, uint16_t* sequence, uint16_t to_code, uint16_t timeout_ms)
{
    if (transparent_layer == NULL)
    {
        transparent_layer = registerLayer(lineno, NULL, "Transparent");
    }

    layer->keymap[key].kind = ACTION_OVERLOAD_MOD;
    for (int i = 0; i < length; i++)
    {
        layer->keymap[key].data.overload_mod.codes[i] = sequence[i];
    }
    for (int i = length; i < MAX_SEQUENCE_OVERLOAD_MOD; i++)
    {
        layer->keymap[key].data.overload_mod.codes[i] = 0;
    }
    layer->keymap[key].data.overload_mod.code = to_code;
    layer->keymap[key].data.overload_mod.timeout_ms = timeout_ms;
}

/**
 * Set overload-layer key in layer.
 */
void setLayerActionOverload(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path, uint16_t to_code, uint16_t timeout_ms)
{
    layer->keymap[key].kind = ACTION_OVERLOAD_LAYER;
    if (to_layer)
    {
        layer->keymap[key].data.overload_layer.layer_index = to_layer->index;
    }
    else
    {
        add_layer_path_reference(lineno, layer, to_layer_path, &layer->keymap[key].data.overload_layer.layer_index);
    }
    layer->keymap[key].data.overload_layer.code = to_code;
    layer->keymap[key].data.overload_layer.timeout_ms = timeout_ms;
}

/**
 * Set shift-layer key in layer.
 */
void setLayerActionShift(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path)
{
    layer->keymap[key].kind = ACTION_SHIFT_LAYER;
    if (to_layer)
    {
        layer->keymap[key].data.shift_layer.layer_index = to_layer->index;
    }
    else
    {
        add_layer_path_reference(lineno, layer, to_layer_path, &layer->keymap[key].data.shift_layer.layer_index);
    }
}

/**
 * Set latch-layer key in layer.
 */
void setLayerActionLatch(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path)
{
    layer->keymap[key].kind = ACTION_LATCH_LAYER;
    if (to_layer)
    {
        layer->keymap[key].data.latch_layer.layer_index = to_layer->index;
    }
    else
    {
        add_layer_path_reference(lineno, layer, to_layer_path, &layer->keymap[key].data.latch_layer.layer_index);
    }
}

/**
 * Set lock-layer key in layer.
 */
void setLayerActionLock(struct layer* layer, int key, struct layer* to_layer, int lineno, char* to_layer_path, uint8_t is_overlay)
{
    layer->keymap[key].kind = ACTION_LOCK_LAYER;
    layer->keymap[key].data.lock_layer.is_overlay = is_overlay;
    if (to_layer)
    {
        layer->keymap[key].data.lock_layer.layer_index = to_layer->index;
    }
    else
    {
        add_layer_path_reference(lineno, layer, to_layer_path, &layer->keymap[key].data.lock_layer.layer_index);
    }
}

/**
 * Set unlock key in layer.
 */
void setLayerActionUnlock(struct layer* layer, int key, uint8_t all)
{
    layer->keymap[key].kind = ACTION_UNLOCK;
    layer->keymap[key].data.unlock.all = all;
}

/**
 * Register a layer.
 */
struct layer* registerLayer(int lineno, struct layer* parent_layer, char* name)
{
    if (nr_layers >= MAX_LAYERS)
    {
        error("error[%d]: exceeded limit of %d layers: %s\n", lineno, MAX_LAYERS, name);
        return NULL;
    }

    if (parent_layer && strlen(parent_layer->name) + strlen(name) >= MAX_LAYER_NAME)
    {
        error("error[%d]: layer path is longer than %d: %s.%s\n", lineno, MAX_LAYER_NAME - 1, parent_layer->name, name);
        return NULL;
    }
    else if (strlen(name) >= MAX_LAYER_NAME)
    {
        error("error[%d]: layer name is longer than %d: %s\n", lineno, MAX_LAYER_NAME - 1, name);
        return NULL;
    }

    struct layer* layer = malloc(sizeof(struct layer));
    layer->index = nr_layers;
    layer->device_index = 0xFF; // No device
    layer->is_layout = 0;
    if (parent_layer)
    {
        char fullpath[2 * MAX_LAYER_NAME];
        sprintf(fullpath, "%s.%s", parent_layer->name, name);
        strcpy(layer->name, fullpath);
    }
    else
    {
        strcpy(layer->name, name);
    }
    layer->parent_layer = parent_layer;
    memset(layer->keymap, 0, sizeof(layer->keymap));

    if (find_layer(lineno, NULL, layer->name))
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
