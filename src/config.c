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
#include "leds.h"
#include "strings.h"

char configuration_file_path[256];
int automatic_reload;

enum input_method ukey_input_method;
uint16_t ukey_compose_key;
int ukeys_delay;

int beep_on_disabled_press_frequency;
int beep_on_disabled_press_duration_ms;
int beep_on_invalid_codepoint_frequency;
int beep_on_invalid_codepoint_duration_ms;

uint8_t** codepoint_strings;
int nr_codepoint_strings = 0;
int max_codepoint_strings;

struct layer* layers[MAX_LAYERS];
static int nr_layers = 0;
struct layer* transparent_layer;
static uint8_t disable_unset_keys[MAX_LAYERS];
static uint8_t mod_layers[MAX_LAYERS];

static uint8_t default_layer_leds[MAX_LAYER_LEDS];

struct input_device input_devices[MAX_DEVICES];
int nr_input_devices;

static regex_t re_layer_name;
static regex_t re_mod_layer_name;

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
 * Duplicate a layer path reference, if action has one.
 * */
static void duplicate_layer_path_reference(struct action *to_action, struct action *from_action)
{
    uint8_t* start = (uint8_t*)from_action;
    uint8_t* end = (uint8_t*)from_action + sizeof(struct action);
    for (struct layer_path_reference* p = layer_path_references; p != NULL; p = p->next)
    {
        if (p->field >= start && p->field < end)
        {
            // Field found inside from_action, duplicate it
            add_layer_path_reference(p->lineno, p->parent_layer, p->path, (uint8_t*)to_action + (p->field - start));
            return;
        }
    }
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
int is_integer(char* token)
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
                // (latch)
                // (latch modifier)
                // (latch to_layer)
                char* what = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                if (what[0] == '\0')
                {
                    if (!isModifier(fromCode))
                    {
                        error("error[%d]: can only bind to modifier keys, or layer name is missing\n", lineno);
                        return;
                    }

                    setLayerActionLatchMod(layer, fromCode, lineno, fromCode);
                    return;
                }

                int whatCode = convertKeyStringToCode(what);
                if (isModifier(whatCode))
                {
                    setLayerActionLatchMod(layer, fromCode, lineno, whatCode);
                }
                else
                {
                    setLayerActionLatch(layer, fromCode, NULL, lineno, what);
                }
                return;
            }
            if (strcmp(action, "latch-menu") == 0)
            {
                // (latch-menu)
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                setLayerActionLatchMenu(layer, fromCode, lineno);
                return;
            }
            if (strcmp(action, "lock") == 0)
            {
                // (lock)
                // (lock modifier)
                // (lock to_layer)
                char* what = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                if (what[0] == '\0')
                {
                    if (!isModifier(fromCode))
                    {
                        error("error[%d]: can only bind to modifier keys, or layer name is missing\n", lineno);
                        return;
                    }

                    setLayerActionLockMod(layer, fromCode, lineno, fromCode);
                    return;
                }

                int whatCode = convertKeyStringToCode(what);
                if (isModifier(whatCode))
                {
                    setLayerActionLockMod(layer, fromCode, lineno, whatCode);
                }
                else
                {
                    setLayerActionLock(layer, fromCode, NULL, lineno, what, 0);
                }
                return;
            }
            if (strcmp(action, "lock-if") == 0)
            {
                // (lock-if key)
                char* key = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                int code = key[0] != '\0' ? convertKeyStringToCode(key) : 0;
                if (code == 0)
                {
                    error("error[%d]: invalid key: expected a single key: %s\n", lineno, key);
                    return;
                }
                setLayerActionLockModIf(layer, fromCode, lineno, code);
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
            if (strcmp(action, "input-method") == 0)
            {
                // (input-method mode)
                char* mode = next_argument(&tokens);
                if (tokens)
                {
                    error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                    return;
                }

                if (strcmp(mode, "none") == 0)
                {
                    setLayerActionInputMethod(layer, fromCode, input_method_none);
                    return;
                }
                if (strcmp(mode, "compose") == 0)
                {
                    setLayerActionInputMethod(layer, fromCode, input_method_compose);
                    return;
                }
                if (strcmp(mode, "iso14755") == 0)
                {
                    setLayerActionInputMethod(layer, fromCode, input_method_iso14755);
                    return;
                }
                if (strcmp(mode, "gtk") == 0)
                {
                    setLayerActionInputMethod(layer, fromCode, input_method_gtk);
                    return;
                }
                error("error[%d]: invalid mode: %s\n", lineno, mode);
                return;
            }
        }

        error("error[%d]: invalid action: %s\n", lineno, action);
        return;
    }

    if (tokens[0] == '"' || tokens[0] == '\'')
    {
        // Unicode string
        char quote = tokens[0];
        tokens++;
        size_t length = strlen(tokens);
        if (length && tokens[length - 1] == quote)
        {
            tokens[length - 1] = '\0'; // Remove end quote
            uint8_t sequence[MAX_SEQUENCE_UKEY_STR];
            unsigned int index = 0;
            for (char* p = tokens; *p != '\0';)
            {
                if (index >= MAX_SEQUENCE_UKEY_STR)
                {
                    error("error[%d]: exceeded limit of %d UTF-8 characters in string: \"%s\"\n", lineno, MAX_SEQUENCE_UKEY_STR / 3, tokens);
                    return;
                }
                int codepoint = 0;
                uint8_t c = *p;
                uint i = 0;
                if (c <= 0x7F)
                {
                    if (c == '\\')
                    {
                        // Escape sequence
                        p++;
                        switch (*p)
                        {
                            case '\\':
                            {
                                codepoint = (uint8_t)'\\';
                                break;
                            }
                            case '\'':
                            {
                                codepoint = (uint8_t)'\'';
                                break;
                            }
                            case '"':
                            {
                                codepoint = (uint8_t)'"';
                                break;
                            }
                            case 'b':
                            {
                                codepoint = 0x08;
                                break;
                            }
                            case 'e':
                            {
                                codepoint = 0x1B;
                                break;
                            }
                            case 'n':
                            {
                                codepoint = 0x0A;
                                break;
                            }
                            case 't':
                            {
                                codepoint = 0x09;
                                break;
                            }
                            default:
                            {
                                error("error[%d]: invalid escape sequence in string: \\%c\n" \
                                      "\tValid sequences are \\\\, \\', \\\", \\b, \\e, \\n, \\t\n", lineno, *p);
                                return;
                            }
                        }
                    }
                    else
                    {
                        codepoint = c;
                    }
                }
                else if (c <= 0xDF)
                {
                    codepoint = (uint)(c & 0x1F) << 6;
                    i = 1;
                }
                else if (c <= 0xEF)
                {
                    codepoint = (uint)(c & 0x0F) << 12;
                    i = 2;
                }
                else if (c <= 0xF7)
                {
                    codepoint = (uint)(c & 0x07) << 18;
                    i = 3;
                }
                else if (c <= 0xFB)
                {
                    codepoint = (uint)(c & 0x03) << 24;
                    i = 4;
                }
                else if (c <= 0xFD)
                {
                    codepoint = (uint)(c & 0x01) << 30;
                    i = 5;
                }
                p++;
                for (; i > 0 && *p != '\0'; i--, p++)
                {
                    c = *p;
                    if ((c & 0xC0) != 0x80) break;
                    codepoint |= (uint)(c & 0x3F) << ((i - 1) * 6);
                }
                if (i > 0 || codepoint & 0xFF000000)
                {
                    error("error[%d]: invalid UTF-8 characters in string: \"%s\"\n", lineno, tokens);
                    return;
                }
                sequence[index++] = codepoint & 0xFF;
                sequence[index++] = (codepoint >> 8) & 0xFF;
                sequence[index++] = (codepoint >> 16) & 0xFF;
            }
            if (index == 0)
            {
                error("error[%d]: expected a string of 1-%d UTF-8 characters\n", lineno, MAX_SEQUENCE_UKEY_STR / 3);
                return;
            }

            setLayerUKey(layer, fromCode, index / 3, sequence);
            return;
        }

        error("error[%d]: invalid unicode string: %s\n", lineno, tokens);
        return;
    }

    if (tokens[0] == 'U')
    {
        size_t length = strlen(tokens);
        if (length >= 3 && tokens[1] == '+')
        {
            // Unicode codepoint
            tokens += 2;
            int codepoint;
            if (sscanf(tokens, "%X", &codepoint) != 1 || codepoint & 0xFF000000)
            {
                error("error[%d]: invalid Unicode codepoint: U+%s\n", lineno, tokens);
                return;
            }

            uint8_t sequence[3];
            sequence[0] = codepoint & 0xFF;
            sequence[1] = (codepoint >> 8) & 0xFF;
            sequence[2] = (codepoint >> 16) & 0xFF;
            setLayerUKey(layer, fromCode, 1, sequence);
            return;
        }
    }

    uint16_t sequence[MAX_SEQUENCE];
    unsigned int index = parse_key_code_sequence(lineno, tokens, MAX_SEQUENCE, sequence);
    if (index == 0) return;
    setLayerKey(layer, fromCode, index, sequence);
}

/**
 * Parse a list of leds.
 * */
static int parse_leds(uint8_t* leds, char* tokens, int lineno)
{
    char* token;
    unsigned int index = 0;
    while ((token = strsep(&tokens, " ")) != NULL)
    {
        if (index >= MAX_LAYER_LEDS)
        {
            error("error[%d]: exceeded limit of %d leds: %s\n", lineno, MAX_LAYER_LEDS, token);
            return 0;
        }
        int state = 1;
        if (token[0] == '!')
        {
            state = 0;
            token++;
        }
        int led = convertLedStringToCode(token);
        if (led == -1)
        {
            error("error[%d]: invalid led: expected one or more led names with or without a '!' prefix to turn it off: %s\n", lineno, token);
            return 0;
        }
        leds[index++] = (state << 4) | (led + 1);
    }
    if (index == 0)
    {
        error("error[%d]: invalid led: expected one or more led names with or without a '!' prefix to turn it off\n", lineno);
        return 0;
    }
    return 1;
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

            if (user_layer->device_index != 0xFF || user_layer->name[0] == ' ' || mod_layers[user_layer->index])
            {
                error("error[%d]: is-layout command is not valid in a device layer, [Menu] or modifier layer\n", lineno);
                return;
            }
            user_layer->is_layout = 1;
            return;
        }
        if (strcmp(command, "disable-unset-keys") == 0)
        {
            // (disable-unset-keys)
            if (tokens)
            {
                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                return;
            }

            if (user_layer->device_index != 0xFF || user_layer->name[0] == ' ')
            {
                error("error[%d]: disable-unset-keys command is not valid in a device layer or [Menu] layer\n", lineno);
                return;
            }
            disable_unset_keys[user_layer->index] = 1;
            return;
        }
        if (strcmp(command, "leds") == 0)
        {
            // (leds led...)

            // This command overrides the (default-layer-leds) setting
            memset(user_layer->leds, 0, sizeof(user_layer->leds));
            if (!parse_leds(user_layer->leds, tokens, lineno)) return;

            if (user_layer->device_index != 0xFF || mod_layers[user_layer->index])
            {
                error("error[%d]: leds command is not valid in a device or modifier layer\n", lineno);
                return;
            }
            return;
        }
        if (strcmp(command, "copy-from-layer") == 0)
        {
            // (copy-from-layer layer)
            char* layer_path = next_argument(&tokens);
            if (tokens)
            {
                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                return;
            }

            struct layer* layer = find_layer(lineno, user_layer, layer_path);
            if (layer == NULL)
            {
                // Forward layer references are not allowed, the layer must exist to copy it now
                error("error[%d]: layer not found: %s\n", lineno, layer_path);
                return;
            }

            for (int k = 0; k < MAX_KEYMAP; k++)
            {
                if (layer->keymap[k].kind != ACTION_TRANSPARENT)
                {
                    if (user_layer->keymap[k].kind != ACTION_TRANSPARENT)
                    {
                        warn("warning[%d]: %s binding overwritten while copying from layer %s\n", lineno, convertKeyCodeToString(k), layer->name);
                    }

                    memcpy(&user_layer->keymap[k], &layer->keymap[k], sizeof(layer->keymap[k]));
                    // Layer path references must be duplicated for the copied binds
                    duplicate_layer_path_reference(&user_layer->keymap[k], &layer->keymap[k]);
                }
            }

            if (layer->menu_layer)
            {
                if (mod_layers[user_layer->index])
                {
                    warn("warning[%d]: layer being copied has a [Menu]: %s\n" \
                         "\tModifier layers can not have a [Menu] section.\n" \
                         "\tNot copying its [Menu] to \"%s\".", lineno, layer_path, user_layer->name);
                }
                if (user_layer->device_index == 0xFF && !user_layer->is_layout)
                {
                    warn("warning[%d]: layer being copied has a [Menu]: %s\n" \
                         "\tOnly device layers and layout layers can have a [Menu] section.\n" \
                         "\tAdd (is-layout) to \"%s\" to copy its [Menu].", lineno, layer_path, user_layer->name);
                    return;
                }
                user_layer->menu_layer = layer->menu_layer;
            }
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

    ukey_input_method = input_method_none;
    ukey_compose_key = KEY_CANCEL;
    ukeys_delay = 5;

    beep_on_disabled_press_frequency = 0;
    beep_on_disabled_press_duration_ms = 0;
    beep_on_invalid_codepoint_frequency = 0;
    beep_on_invalid_codepoint_duration_ms = 0;

    // Free existing codepoint strings
    if (nr_codepoint_strings)
    {
        for (int i = 0; i < nr_codepoint_strings; i++) free(codepoint_strings[i]);
        free(codepoint_strings);
    }
    codepoint_strings = NULL;
    nr_codepoint_strings = 0;
    max_codepoint_strings = 0;

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
    memset(disable_unset_keys, 0, sizeof(disable_unset_keys));
    memset(mod_layers, 0, sizeof(mod_layers));

    memset(default_layer_leds, 0, sizeof(default_layer_leds));

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
    regfree(&re_mod_layer_name);
    regcomp(&re_mod_layer_name, "^(SHIFT|CTRL|ALT|META)([+](SHIFT|CTRL|ALT|META)){0,3}[]]$", REG_EXTENDED|REG_NOSUB);
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
                        if (strcmp(setting, "input-method") == 0)
                        {
                            // (input-method mode)
                            char* mode = next_argument(&tokens);
                            if (tokens)
                            {
                                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                                continue;
                            }

                            if (strcmp(mode, "none") == 0)
                            {
                                ukey_input_method = input_method_none;
                                continue;
                            }
                            if (strcmp(mode, "compose") == 0)
                            {
                                ukey_input_method = input_method_compose;
                                continue;
                            }
                            if (strcmp(mode, "iso14755") == 0)
                            {
                                ukey_input_method = input_method_iso14755;
                                continue;
                            }
                            if (strcmp(mode, "gtk") == 0)
                            {
                                ukey_input_method = input_method_gtk;
                                continue;
                            }
                            error("error[%d]: invalid mode: %s\n", lineno, mode);
                            continue;
                        }
                        if (strcmp(setting, "unicode-compose-key") == 0)
                        {
                            // (unicode-compose-key key)
                            char* code_name = next_argument(&tokens);
                            if (tokens)
                            {
                                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                                continue;
                            }

                            int code = convertKeyStringToCode(code_name);
                            if (code == 0)
                            {
                                error("error[%d]: invalid key: expected a single key: %s\n", lineno, code_name);
                                continue;
                            }

                            ukey_compose_key = code;
                            continue;
                        }
                        if (strcmp(setting, "ukeys-delay") == 0)
                        {
                            // (ukeys-delay microseconds)
                            char* delay_str = next_argument(&tokens);
                            if (tokens)
                            {
                                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                                continue;
                            }

                            int delay_us;
                            if (!parse_integer(&delay_us, delay_str, 0, 100000, "delay", lineno)) continue;

                            ukeys_delay = delay_us;
                            continue;
                        }
                        if (strcmp(setting, "beep-on-disabled-press") == 0)
                        {
                            // (beep-on-disabled-press frequency duration_ms)
                            char* frequency_str = next_argument(&tokens);
                            char* duration_str = next_argument(&tokens);
                            if (tokens)
                            {
                                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                                continue;
                            }

                            int frequency;
                            if (!parse_integer(&frequency, frequency_str, 200, 8000, "frequency", lineno)) continue;

                            int duration_ms;
                            if (!parse_integer(&duration_ms, duration_str, 10, 1000, "duration", lineno)) continue;

                            beep_on_disabled_press_frequency = frequency;
                            beep_on_disabled_press_duration_ms = duration_ms;
                            continue;
                        }
                        if (strcmp(setting, "beep-on-invalid-codepoint") == 0)
                        {
                            // (beep-on-invalid-codepoint frequency duration_ms)
                            char* frequency_str = next_argument(&tokens);
                            char* duration_str = next_argument(&tokens);
                            if (tokens)
                            {
                                error("error[%d]: extra arguments found: %s\n", lineno, tokens);
                                continue;
                            }

                            int frequency;
                            if (!parse_integer(&frequency, frequency_str, 200, 8000, "frequency", lineno)) continue;

                            int duration_ms;
                            if (!parse_integer(&duration_ms, duration_str, 10, 1000, "duration", lineno)) continue;

                            beep_on_invalid_codepoint_frequency = frequency;
                            beep_on_invalid_codepoint_duration_ms = duration_ms;
                            continue;
                        }
                        if (strcmp(setting, "default-layer-leds") == 0)
                        {
                            // (default-layer-leds led...)
                            parse_leds(default_layer_leds, tokens, lineno);
                            continue;
                        }
                        if (strcmp(setting, "modifier-layer-leds") == 0)
                        {
                            // (modifier-layer-leds led...)
                            if (transparent_layer == NULL)
                            {
                                transparent_layer = registerLayer(lineno, NULL, "Transparent");
                            }

                            // This setting overrides the (default-layer-leds) setting
                            memset(transparent_layer->leds, 0, sizeof(transparent_layer->leds));
                            parse_leds(transparent_layer->leds, tokens, lineno);
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
                    if (strncmp(line, "[Menu]", line_length) == 0)
                    {
                        if (user_layer->device_index == 0xFF && !user_layer->is_layout)
                        {
                            error("error[%d]: only device layers and layout layers can have a [Menu] section\n", lineno);
                            invalid_layer_indent = indent;
                            continue;
                        }
                        user_layer = registerLayer(lineno, user_layer, "Menu");
                        user_layer->parent_layer->menu_layer = user_layer;
                        disable_unset_keys[user_layer->index] = 1;
                        section = configuration_layer;
                        continue;
                    }

                    // Check for modifier layers
                    if (line[line_length - 1] == ']')
                    {
                        // Skip '['
                        char* s = &line[1];

                        if (regexec(&re_mod_layer_name, s, 0, NULL, 0) == 0)
                        {
                            struct layer* parent_layer = user_layer;
                            if (mod_layers[parent_layer->index])
                            {
                                error("error[%d]: modifier layers can not be nested inside other modifier layers\n", lineno);
                                invalid_layer_indent = indent;
                                continue;
                            }

                            line[line_length - 1] = '\0'; // Remove ']'

                            user_layer = registerLayer(lineno, user_layer, s);
                            mod_layers[user_layer->index] = 1;
                            int mods = 0;
                            char *mod_token;
                            while ((mod_token = strsep(&s, "+")) != NULL)
                            {
                                if (strcmp(mod_token, "SHIFT") == 0)
                                {
                                    if (mods & 1)
                                    {
                                        error("error[%d]: duplicate SHIFT in modifier layer name\n", lineno);
                                    }
                                    mods |= 1;
                                }
                                if (strcmp(mod_token, "CTRL") == 0)
                                {
                                    if (mods & 2)
                                    {
                                        error("error[%d]: duplicate CTRL in modifier layer name\n", lineno);
                                    }
                                    mods |= 2;
                                }
                                if (strcmp(mod_token, "ALT") == 0)
                                {
                                    if (mods & 4)
                                    {
                                        error("error[%d]: duplicate ALT in modifier layer name\n", lineno);
                                    }
                                    mods |= 4;
                                }
                                if (strcmp(mod_token, "META") == 0)
                                {
                                    if (mods & 8)
                                    {
                                        error("error[%d]: duplicate META in modifier layer name\n", lineno);
                                    }
                                    mods |= 8;
                                }
                            }
                            if (parent_layer->mod_layers[mods])
                            {
                                error("error[%d]: duplicate %s modifier layers in %s\n", lineno, user_layer->name, parent_layer->name);
                            }
                            else
                            {
                                parent_layer->mod_layers[mods - 1] = user_layer->index + 1; // Offset by 1
                            }
                            section = configuration_layer;
                            continue;
                        }
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

    for (int i = 0; i < nr_layers; i++)
    {
        if (disable_unset_keys[i])
        {
            struct layer* layer = layers[i];
            // Disable all unset bindings
            for (int k = 0; k < MAX_KEYMAP; k++)
            {
                if (layer->keymap[k].kind == ACTION_TRANSPARENT && !isModifier(k))
                {
                    layer->keymap[k].kind = ACTION_DISABLED;
                }
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
    memset(device->leds, 0, sizeof(device->leds));

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
 * Set codepoint or codepoint sequence in layer.
 */
void setLayerUKey(struct layer* layer, int key, unsigned int length, uint8_t* sequence)
{
    switch (length)
    {
        case 0: break;
        case 1:
        {
            layer->keymap[key].kind = ACTION_UKEY;
            layer->keymap[key].data.ukey.codepoint[0] = sequence[0];
            layer->keymap[key].data.ukey.codepoint[1] = sequence[1];
            layer->keymap[key].data.ukey.codepoint[2] = sequence[2];
            break;
        }
        default:
        {
            uint8_t* codepoints;
            if (3 * length > MAX_SEQUENCE_UKEY)
            {
                if (nr_codepoint_strings == max_codepoint_strings)
                {
                    // Create or resize the array
                    max_codepoint_strings += 64;
                    codepoint_strings = realloc(codepoint_strings, max_codepoint_strings * sizeof(void*));
                }

                layer->keymap[key].kind = ACTION_UKEYS_STR;
                layer->keymap[key].data.ukeys_str.codepoint_string_index = nr_codepoint_strings;
                layer->keymap[key].data.ukeys_str.length = length;
                codepoints = malloc(3 * length);
                codepoint_strings[nr_codepoint_strings++] = codepoints;
            }
            else
            {
                layer->keymap[key].kind = ACTION_UKEYS;
                codepoints = layer->keymap[key].data.ukeys.codepoints;
            }

            for (int i = 0; i < 3 * length; i++)
            {
                codepoints[i] = sequence[i];
            }
            for (int i = 3 * length; i < MAX_SEQUENCE_UKEY; i++)
            {
                codepoints[i] = 0;
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
 * Set latch-menu key in layer, for current [Menu] layer.
 */
void setLayerActionLatchMenu(struct layer* layer, int key, int lineno)
{
    layer->keymap[key].kind = ACTION_LATCH_MENU;
}

/**
 * Set latch-mod key in layer.
 */
void setLayerActionLatchMod(struct layer* layer, int key, int lineno, uint8_t code)
{
    if (!isModifier(code))
    {
        error("error[%d]: latch action can only be bound to modifier keys: %s\n", lineno, convertKeyCodeToString(code));
        return;
    }

    if (transparent_layer == NULL)
    {
        transparent_layer = registerLayer(lineno, NULL, "Transparent");
    }

    layer->keymap[key].kind = ACTION_LATCH_MOD;
    layer->keymap[key].data.latch_mod.modifier_bit = modifierKeyCodeToBit(code);
    layer->keymap[key].data.latch_mod.modifier_code = code;
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
 * Set lock-mod key in layer.
 */
void setLayerActionLockMod(struct layer* layer, int key, int lineno, uint8_t code)
{
    if (!isModifier(code))
    {
        error("error[%d]: lock action can only be bound to modifier keys: %s\n", lineno, convertKeyCodeToString(code));
        return;
    }

    if (transparent_layer == NULL)
    {
        transparent_layer = registerLayer(lineno, NULL, "Transparent");
    }

    layer->keymap[key].kind = ACTION_LOCK_MOD;
    layer->keymap[key].data.lock_mod.modifier_bit = modifierKeyCodeToBit(code);
    layer->keymap[key].data.lock_mod.modifier_code = code;
}

/**
 * Set lock-if key in layer.
 */
void setLayerActionLockModIf(struct layer* layer, int key, int lineno, uint8_t if_code)
{
    if (!isModifier(key))
    {
        error("error[%d]: lock-if action can only be bound to modifier keys: %s\n", lineno, convertKeyCodeToString(key));
        return;
    }

    layer->keymap[key].kind = ACTION_LOCK_MOD_IF;
    layer->keymap[key].data.lock_mod_if.modifier_bit = modifierKeyCodeToBit(key);
    layer->keymap[key].data.lock_mod_if.modifier_code = key;
    // Non-modifier if_code keys can not be used to unlock the modifier in key parameter
    layer->keymap[key].data.lock_mod_if.if_bit = modifierKeyCodeToBit(if_code);
    layer->keymap[key].data.lock_mod_if.if_code = isModifier(if_code) ? if_code : 0;
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
 * Set input-method key in layer.
 */
void setLayerActionInputMethod(struct layer* layer, int key, enum input_method mode)
{
    layer->keymap[key].kind = ACTION_INPUT_METHOD;
    layer->keymap[key].data.input_method.mode = mode;
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
    layer->menu_layer = NULL;
    memset(layer->keymap, 0, sizeof(layer->keymap));
    memcpy(layer->leds, default_layer_leds, sizeof(layer->leds));
    memset(layer->mod_layers, 0, sizeof(layer->mod_layers));

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
