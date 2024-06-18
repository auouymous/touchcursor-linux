#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "buffers.h"
#include "emit.h"
#include "keys.h"
#include "leds.h"
#include "mapper.h"

#define PRESSED_TO_LAYER(device, code) layers[device->pressed[code] - 1]
#define LAYER_TO_PRESSED(layer) (layer->index + 1)

#define MAX_POOL_ACTIVATIONS 8
static struct activation* activation_pool[MAX_POOL_ACTIVATIONS];
static int nr_pool_activations = 0;

/**
 * Toggle layer leds.
 * */
static void toggle_layer_leds(struct input_device* device, struct layer* layer, int state)
{
    for (int i = 0; i < MAX_LAYER_LEDS; i++)
    {
        uint8_t led = layer->leds[i];
        if (led == 0) break;

        int led_state = led >> 4;
        if (state)
            set_led(device, (led & 0xF) - 1, led_state);
        else if (led_state)
            set_led(device, (led & 0xF) - 1, 0);
    }
}

/**
 * Activate layer.
 * */
static struct activation* activate_layer(struct input_device* device, struct layer* layer, enum activation_kind kind, int code)
{
    struct activation* activation = (nr_pool_activations > 0 ? activation_pool[--nr_pool_activations] : malloc(sizeof(struct activation)));
    memset(activation, 0, sizeof(struct activation));
    activation->layer = layer;
    activation->prev = device->top_activation;
    // activation->next = NULL;
    // activation->action = NULL;
    activation->code = code;
    activation->kind = kind;
    // activation->data.* = 0;

    toggle_layer_leds(device, layer, 1);

    if (device->top_activation != NULL) device->top_activation->next = activation;
    device->top_activation = activation;
    return activation;
}

/**
 * Deactivate layer.
 * */
static void deactivate_layer(struct input_device* device, struct activation* activation)
{
    // Turn off this layer's leds
    toggle_layer_leds(device, activation->layer, 0);

    if (activation->prev) activation->prev->next = activation->next;
    if (activation->next) activation->next->prev = activation->prev;
    if (device->top_activation == activation) device->top_activation = activation->prev;

    // Turn any leds back on that the deactivated layer might have turned off
    if (device->top_activation) toggle_layer_leds(device, device->top_activation->layer, 1);

    if (nr_pool_activations < MAX_POOL_ACTIVATIONS)
    {
        activation_pool[nr_pool_activations++] = activation;
    }
    else
    {
        free(activation);
    }
}

/**
 * Deactivate lock-overlay layers above this lock-layer activation.
 * */
static void deactivate_overlays(struct input_device* device, struct activation* activation)
{
    for (struct activation* a = activation->next; a != NULL;)
    {
        struct activation* next = a->next;
        // Any lock-layer activation above this must be a lock-overlay
        if (a->kind == ACTIVATION_LOCK_LAYER) deactivate_layer(device, a);
        a = next;
    }
}

/**
 * Lookup a key layer for an input device.
 */
static struct layer* find_key_layer(struct input_device* device, int code, int value)
{
    if (IS_PRESS(value))
    {
        // Find code in active layers
        struct activation* activation = device->top_activation;
        while (activation != NULL)
        {
            if (activation->layer->keymap[code].kind != ACTION_TRANSPARENT)
            {
                return activation->layer;
            }
            activation = activation->prev;
        }
    }
    else
    {
        // Find code in pressed array
        if (device->pressed[code])
        {
            return PRESSED_TO_LAYER(device, code);
        }
        else
        {
            error("error: the service did not properly check if key was in pressed array before calling find_key_layer()\n");
        }
    }

    return device->layer;
}

/**
 * Emit a sequence of keys.
 * */
static void emit_key_sequence(unsigned int max, uint16_t* sequence, int value)
{
    if (IS_RELEASE(value))
    {
        // Send release sequence in reverse order
        int last = 0;
        for (; last < max; last++)
        {
            if (sequence[last] == 0) break;
        }
        for (int i = last - 1; i >= 0; i--)
        {
            emit(EV_KEY, sequence[i], 0);
        }
        return;
    }

    for (int i = 0; i < max; i++)
    {
        if (sequence[i] == 0)
        {
            break;
        }
        emit(EV_KEY, sequence[i], value);
    }
}

/**
 * Release all output modifier keys and return bitmask to restore.
 * */
static uint8_t release_all_output_modifiers()
{
    uint8_t mods = output_modifier_states;
    for (int i = 0; output_modifier_states; i++)
    {
        const uint8_t m = modifier_bit_list[i];
        if (output_modifier_states & m)
        {
            emit(EV_KEY, modifier_key_list[i], 0);
            output_modifier_states &= ~m;
        }
    }
    return mods;
}

/**
 * Restore all output modifier keys.
 * */
static void restore_all_output_modifiers(uint8_t modifiers)
{
    for (int i = 0; modifiers; i++)
    {
        const uint8_t m = modifier_bit_list[i];
        if (modifiers & m)
        {
            emit(EV_KEY, modifier_key_list[i], 1);
            modifiers &= ~m;
        }
    }
}

#define SHIFT 0x8000
static uint16_t codepoint_to_keycode[256] = {
    // control codes
    0, 0, 0, 0, 0, 0, 0, 0, KEY_BACKSPACE, KEY_TAB, KEY_ENTER, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_ESC, 0, 0, 0, 0,
    //  !"#$%&'()*+,-./0123456789:;<=>?
    KEY_SPACE,
    SHIFT|KEY_1,
    SHIFT|KEY_APOSTROPHE,
    SHIFT|KEY_3,
    SHIFT|KEY_4,
    SHIFT|KEY_5,
    SHIFT|KEY_7,
    KEY_APOSTROPHE,
    SHIFT|KEY_9,
    SHIFT|KEY_0,
    SHIFT|KEY_8,
    SHIFT|KEY_EQUAL,
    KEY_COMMA,
    KEY_MINUS,
    KEY_DOT,
    KEY_SLASH,
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    SHIFT|KEY_SEMICOLON,
    KEY_SEMICOLON,
    SHIFT|KEY_COMMA,
    KEY_EQUAL,
    SHIFT|KEY_DOT,
    SHIFT|KEY_SLASH,
    // @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    SHIFT|KEY_2,
    SHIFT|KEY_A,
    SHIFT|KEY_B,
    SHIFT|KEY_C,
    SHIFT|KEY_D,
    SHIFT|KEY_E,
    SHIFT|KEY_F,
    SHIFT|KEY_G,
    SHIFT|KEY_H,
    SHIFT|KEY_I,
    SHIFT|KEY_J,
    SHIFT|KEY_K,
    SHIFT|KEY_L,
    SHIFT|KEY_M,
    SHIFT|KEY_N,
    SHIFT|KEY_O,
    SHIFT|KEY_P,
    SHIFT|KEY_Q,
    SHIFT|KEY_R,
    SHIFT|KEY_S,
    SHIFT|KEY_T,
    SHIFT|KEY_U,
    SHIFT|KEY_V,
    SHIFT|KEY_W,
    SHIFT|KEY_X,
    SHIFT|KEY_Y,
    SHIFT|KEY_Z,
    KEY_LEFTBRACE,
    KEY_BACKSLASH,
    KEY_RIGHTBRACE,
    SHIFT|KEY_6,
    SHIFT|KEY_MINUS,
    // `abcdefghijklmnopqrstuvwxyz{|}~
    KEY_GRAVE,
    KEY_A,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,
    SHIFT|KEY_LEFTBRACE,
    SHIFT|KEY_BACKSLASH,
    SHIFT|KEY_RIGHTBRACE,
    SHIFT|KEY_GRAVE,
    0, // delete
};

static uint8_t base32_keys[36] = {
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, // KEY_W, KEY_X, KEY_Y, KEY_Z,
};

/**
 * Convert codepoint to a base32 key sequence.
 * */
static void codepoint_to_base32(int codepoint, uint8_t keys[5])
{
    for (int i = 0; i < 5; i++)
    {
        keys[i] = base32_keys[codepoint & 0x1F];
        codepoint >>= 5;
    }
}

static char base16_keys[16] = {
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F,
};

/**
 * Convert codepoint to a base16 key sequence.
 * */
static int codepoint_to_base16(int codepoint, uint8_t keys[6])
{
    for (int i = 0; i < 6; i++)
    {
        keys[i] = base16_keys[codepoint & 0xF];
        codepoint >>= 4;
        if (codepoint == 0) return i;
    }
    return 6 - 1;
}

/**
 * Emit keycode for ASCII codepoint, if available.
 * */
static void emit_codepoint_to_keycode(int codepoint)
{
    int key = codepoint_to_keycode[codepoint];
    if (key == 0) return; // Ignore control codes

    int shift = (key & SHIFT);
    key &= ~SHIFT;
    if (shift) emit(EV_KEY, KEY_LEFTSHIFT, 1);
    emit(EV_KEY, key, 1);
    emit(EV_KEY, key, 0);
    if (shift) emit(EV_KEY, KEY_LEFTSHIFT, 0);
}

/**
 * Emit a codepoint sequence.
 * */
static int emit_codepoint(uint8_t* bytes)
{
    int codepoint = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
    if (codepoint == 0) return 0;

    if (codepoint < 0x20)
    {
        emit_codepoint_to_keycode(codepoint);
        return 1;
    }

    switch (ukey_input_method)
    {
        case input_method_none:
        {
            // Ignore non-ASCII codepoints
            if (codepoint <= 0x7F)
            {
                emit_codepoint_to_keycode(codepoint);
            }
            break;
        }
        case input_method_compose:
        {
            emit(EV_KEY, ukey_compose_key, 1);
            emit(EV_KEY, ukey_compose_key, 0);
            // Send 5 base32 key taps
            uint8_t keys[5];
            codepoint_to_base32(codepoint, keys);
            for (int i = 4; i >= 0; i--)
            {
                emit(EV_KEY, keys[i], 1);
                emit(EV_KEY, keys[i], 0);
            }
            break;
        }
        case input_method_iso14755:
        {
            emit(EV_KEY, KEY_LEFTCTRL, 1);
            emit(EV_KEY, KEY_LEFTSHIFT, 1);
            // Send 1-6 base16 key taps
            uint8_t keys[6];
            int nr_keys = codepoint_to_base16(codepoint, keys);
            for (int i = nr_keys; i >= 0; i--)
            {
                emit(EV_KEY, keys[i], 1);
                emit(EV_KEY, keys[i], 0);
            }
            emit(EV_KEY, KEY_LEFTSHIFT, 0);
            emit(EV_KEY, KEY_LEFTCTRL, 0);
            break;
        }
        case input_method_gtk:
        {
            emit(EV_KEY, KEY_LEFTCTRL, 1);
            emit(EV_KEY, KEY_LEFTSHIFT, 1);
            emit(EV_KEY, KEY_U, 1);
            emit(EV_KEY, KEY_U, 0);
            emit(EV_KEY, KEY_LEFTSHIFT, 0);
            emit(EV_KEY, KEY_LEFTCTRL, 0);
            // Send 1-6 base16 key taps
            uint8_t keys[6];
            int nr_keys = codepoint_to_base16(codepoint, keys);
            for (int i = nr_keys; i >= 0; i--)
            {
                emit(EV_KEY, keys[i], 1);
                emit(EV_KEY, keys[i], 0);
            }
            emit(EV_KEY, KEY_SPACE, 1);
            emit(EV_KEY, KEY_SPACE, 0);
            break;
        }
    }
    return 1;
}

/**
 * Check if timeout has expired.
 * */
static int timeout_has_expired(struct timeval timestamp, struct timeval activation_timeout_timestamp)
{
    return (timerisset(&activation_timeout_timestamp) && timercmp(&timestamp, &activation_timeout_timestamp, >));
}

static void process_action(struct input_device* device, struct layer* layer, int code, int value, struct timeval timestamp);

/**
 * Activate an overload-mod activation.
 * */
static void activate_overload_mod(struct input_device* device, struct activation* activation, int delayed_code, struct timeval timestamp)
{
    activation->data.overload_mod.active = 1;
    // Send overload-mod press sequence
    emit_key_sequence(MAX_SEQUENCE_OVERLOAD_MOD, activation->action->data.overload_mod.codes, 1);
    if (delayed_code)
    {
        // Send press event for delayed key code
        process_action(device, find_key_layer(device, delayed_code, 1), delayed_code, 1, timestamp);
    }
}

/**
 * Activate an overload activation.
 * */
static void activate_overload(struct input_device* device, struct activation* activation, int delayed_code, struct timeval timestamp)
{
    activation->data.overload_layer.active = 1;
    if (delayed_code)
    {
        // Send press event for delayed key code
        process_action(device, find_key_layer(device, delayed_code, 1), delayed_code, 1, timestamp);
    }
}

/**
 * Find activation by key code.
 * */
struct activation* find_activation_by_code(struct input_device* device, int code)
{
    struct activation* activation = device->top_activation;
    while (activation != NULL && activation->code != code) activation = activation->prev;
    return activation;
}

#define FIND_ACTIVATION(device, expression) \
    device->top_activation; \
    while (activation != NULL && !(expression)) activation = activation->prev

/**
 * Process a key action.
 * */
static void process_action(struct input_device* device, struct layer* layer, int code, int value, struct timeval timestamp)
{
    struct action* action = &layer->keymap[code];

    switch (action->kind)
    {
        case ACTION_TRANSPARENT:
        {
            error("error: the service did not properly pass-through a transparent key before calling process_action()\n");
            break;
        }
        case ACTION_DISABLED: break;
        case ACTION_KEY:
        {
            emit(EV_KEY, action->data.key.code, value);
            break;
        }
        case ACTION_KEYS:
        {
            emit_key_sequence(MAX_SEQUENCE, action->data.keys.codes, value);
            break;
        }
        case ACTION_UKEY:
        {
            if (value != 0)
            {
                uint8_t modifiers = release_all_output_modifiers();
                emit_codepoint(action->data.ukey.codepoint);
                restore_all_output_modifiers(modifiers);
            }
            break;
        }
        case ACTION_UKEYS:
        {
            if (value != 0)
            {
                uint8_t modifiers = release_all_output_modifiers();
                for (int i = 0; i < MAX_SEQUENCE_UKEY; i += 3)
                {
                    if (!emit_codepoint(&action->data.ukeys.codepoints[i])) break;
                    usleep(ukeys_delay);
                }
                restore_all_output_modifiers(modifiers);
            }
            break;
        }
        case ACTION_UKEYS_STR:
        {
            if (value != 0)
            {
                int length = 3 * action->data.ukeys_str.length;
                uint8_t* codepoints = codepoint_strings[action->data.ukeys_str.codepoint_string_index];
                uint8_t modifiers = release_all_output_modifiers();
                for (int i = 0; i < length; i += 3)
                {
                    if (!emit_codepoint(&codepoints[i])) break;
                    usleep(ukeys_delay);
                }
                restore_all_output_modifiers(modifiers);
            }
            break;
        }
        case ACTION_OVERLOAD_MOD:
        {
            if (IS_PRESS(value))
            {
                struct activation* activation = activate_layer(device, transparent_layer, ACTIVATION_OVERLOAD_MOD, code);
                activation->action = action;
                int timeout_ms = action->data.overload_mod.timeout_ms;
                if (timeout_ms > 0)
                {
                    struct timeval a = timestamp;
                    struct timeval b = {0, timeout_ms * 1000};
                    timeradd(&a, &b, &device->top_activation->data.overload_mod.timeout_timestamp);
                }
                else
                {
                    timerclear(&device->top_activation->data.overload_mod.timeout_timestamp);
                }
            }
            else if (IS_RELEASE(value))
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL)
                {
                    if (!activation->data.overload_mod.active && timeout_has_expired(timestamp, activation->data.overload_mod.timeout_timestamp))
                    {
                        // Action timed-out, activate on release
                        activate_overload_mod(device, activation, activation->data.overload_mod.delayed_code, timestamp);
                    }

                    if (!activation->data.overload_mod.active)
                    {
                        // Send overload-mod key and delayed key code, if one
                        emit(EV_KEY, action->data.overload_mod.code, 1);
                        if (activation->data.overload_mod.delayed_code)
                        {
                            emit(EV_KEY, activation->data.overload_mod.delayed_code, 1);
                        }
                        emit(EV_KEY, action->data.overload_mod.code, 0);
                    }
                    else
                    {
                        // Send overload-mod release sequence
                        emit_key_sequence(MAX_SEQUENCE_OVERLOAD_MOD, action->data.overload_mod.codes, 0);
                    }
                    deactivate_layer(device, activation);
                }
            }
            else if (action->data.overload_mod.timeout_ms > 0) // IS_REPEAT(value)
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL && !activation->data.overload_mod.active)
                {
                    if (timeout_has_expired(timestamp, activation->data.overload_mod.timeout_timestamp))
                    {
                        // Action timed-out, activate on repeat
                        activate_overload_mod(device, activation, activation->data.overload_mod.delayed_code, timestamp);
                    }
                }
            }
            break;
        }
        case ACTION_OVERLOAD_LAYER:
        {
            if (IS_PRESS(value))
            {
                struct activation* activation = activate_layer(device, layers[action->data.overload_layer.layer_index], ACTIVATION_OVERLOAD_LAYER, code);
                int timeout_ms = action->data.overload_layer.timeout_ms;
                if (timeout_ms > 0)
                {
                    struct timeval a = timestamp;
                    struct timeval b = {0, timeout_ms * 1000};
                    timeradd(&a, &b, &activation->data.overload_layer.timeout_timestamp);
                }
                else
                {
                    timerclear(&activation->data.overload_layer.timeout_timestamp);
                }
            }
            else if (IS_RELEASE(value))
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL)
                {
                    if (!activation->data.overload_layer.active && timeout_has_expired(timestamp, activation->data.overload_layer.timeout_timestamp))
                    {
                        // Action timed-out, activate on release
                        activate_overload(device, activation, activation->data.overload_layer.delayed_code, timestamp);
                    }

                    if (!activation->data.overload_layer.active)
                    {
                        // Send overload-layer key and delayed key code, if one
                        emit(EV_KEY, action->data.overload_layer.code, 1);
                        if (activation->data.overload_layer.delayed_code)
                        {
                            emit(EV_KEY, activation->data.overload_layer.delayed_code, 1);
                        }
                        emit(EV_KEY, action->data.overload_layer.code, 0);
                    }
                    deactivate_layer(device, activation);
                }
            }
            else if (action->data.overload_layer.timeout_ms > 0) // IS_REPEAT(value)
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL && !activation->data.overload_layer.active)
                {
                    if (timeout_has_expired(timestamp, activation->data.overload_layer.timeout_timestamp))
                    {
                        // Action timed-out, activate on repeat
                        activate_overload(device, activation, activation->data.overload_layer.delayed_code, timestamp);
                    }
                }
            }
            break;
        }
        case ACTION_SHIFT_LAYER:
        {
            // Ignore repeat events
            if (IS_PRESS(value))
            {
                activate_layer(device, layers[action->data.shift_layer.layer_index], ACTIVATION_SHIFT_LAYER, code);
            }
            else if (IS_RELEASE(value))
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL)
                {
                    deactivate_layer(device, activation);
                }
            }
            break;
        }
        case ACTION_LATCH_LAYER:
        {
            // Ignore repeat events
            if (IS_PRESS(value))
            {
                activate_layer(device, layers[action->data.latch_layer.layer_index], ACTIVATION_LATCH_LAYER, code);
            }
            else if (IS_RELEASE(value))
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL)
                {
                    if (activation->kind == ACTIVATION_SHIFT_LAYER)
                    {
                        // Latch-layer key was released as a shift activation
                        deactivate_layer(device, activation);
                    }
                    else
                    {
                        // The layer will be deactivated on the first key press
                        activation->action = action;
                        activation->code = 0;
                    }
                }
                // else latch-layer key was released a second time, and was unlatched on press
            }
            break;
        }
        case ACTION_LOCK_LAYER:
        {
            // Ignore repeat events
            if (IS_PRESS(value))
            {
                // Find a previous activation for this lock-layer action
                struct activation* activation = FIND_ACTIVATION(device, activation->action == action);
                if (activation != NULL)
                {
                    // Key that locked activation was pressed a second time, unlock it
                    deactivate_overlays(device, activation);
                    deactivate_layer(device, activation);
                }
                else
                {
                    int can_lock = 1;
                    if (action->data.lock_layer.is_overlay)
                    {
                        // Find a locked non-overlay lock-layer activation with a released key
                        struct activation* activation = FIND_ACTIVATION(device,
                            activation->kind == ACTIVATION_LOCK_LAYER && !activation->data.lock_layer.is_overlay && activation->action != NULL);
                        if (activation == NULL) can_lock = 0;
                    }
                    if (can_lock)
                    {
                        uint8_t layer_index = action->data.lock_layer.layer_index;
                        struct activation* activation = activate_layer(device, layers[layer_index], ACTIVATION_LOCK_LAYER, code);
                        activation->data.lock_layer.layer_index = layer_index;
                        activation->data.lock_layer.is_overlay = action->data.lock_layer.is_overlay;
                    }
                    else
                    {
                        // No locked non-overlay lock-layer activations, convert to a latch activation
                        activate_layer(device, layers[action->data.lock_layer.layer_index], ACTIVATION_LATCH_LAYER, code);
                    }
                }
            }
            else if (IS_RELEASE(value))
            {
                struct activation* activation = find_activation_by_code(device, code);
                if (activation != NULL)
                {
                    if (activation->kind == ACTIVATION_SHIFT_LAYER)
                    {
                        // Lock-layer key was released as a shift activation
                        deactivate_layer(device, activation);
                    }
                    else
                    {
                        if (!action->data.lock_layer.is_overlay)
                        {
                            // Unlock all locked layers that are not layouts, unless locking a layout, then unlock all locked layers
                            int unlock_all = layers[action->data.lock_layer.layer_index]->is_layout;
                            for (struct activation* a = device->top_activation; a != NULL;)
                            {
                                struct activation* prev = a->prev;
                                // Skip activation for this lock-layer key
                                if (a != activation && a->kind == ACTIVATION_LOCK_LAYER && (unlock_all || !a->layer->is_layout))
                                {
                                    deactivate_layer(device, a);
                                }
                                a = prev;
                            }
                        }

                        // The layer remains activated until unlocked
                        activation->action = action;
                        activation->code = 0;
                    }
                }
                // else lock-layer key was released a second time, and was unlocked on press
            }
            break;
        }
        case ACTION_UNLOCK:
        {
            // Ignore press and repeat events
            if (IS_RELEASE(value))
            {
                if (action->data.unlock.all)
                {
                    // Unlock all locked layers, including layouts
                    for (struct activation* a = device->top_activation; a != NULL;)
                    {
                        struct activation* prev = a->prev;
                        // This is safe even if the key used to activate layer is still pressed
                        // Overload key and its delayed key will not be sent
                        deactivate_layer(device, a);
                        a = prev;
                    }
                }
                else
                {
                    // Find lock-layer activation with same layer as this unlock action
                    struct activation* activation = FIND_ACTIVATION(device,
                        activation->kind == ACTIVATION_LOCK_LAYER && activation->data.lock_layer.layer_index == layer->index);
                    if (activation != NULL)
                    {
                        if(!activation->data.lock_layer.is_overlay) deactivate_overlays(device, activation);
                        deactivate_layer(device, activation);
                    }
                }
            }
            break;
        }
        case ACTION_INPUT_METHOD:
        {
            ukey_input_method = action->data.input_method.mode;
            break;
        }
    }

    device->pressed[code] = (IS_RELEASE(value) ? 0 : LAYER_TO_PRESSED(layer));
}

/**
 * Processes a key input event. Converts and emits events as necessary.
 * */
void processKey(struct input_device* device, int type, int code, int value, struct timeval timestamp)
{
    code = device->remap[code];

    if (device->top_activation == NULL)
    {
        if (device->pressed[code])
        {
            // Key was pressed in device layer or a layer that has been deactivated
            process_action(device, PRESSED_TO_LAYER(device, code), code, value, timestamp);
        }
        else
        {
            process_action(device, device->layer, code, value, timestamp);
        }
    }
    else if (code == device->top_activation->code)
    {
        // This path must not be entered on second press of the key that created activation
        // Therefore, activation->code must always be cleared on release
        if (device->pressed[code])
        {
            // Repeat or release the key used to activate current layer
            process_action(device, PRESSED_TO_LAYER(device, code), code, value, timestamp);
        }
        else
        {
            // Key was held down while starting the service, send its unprocessed code
            emit(EV_KEY, code, value);
        }
    }
    else if (isModifier(code) && (device->top_activation->layer->keymap[code].kind == ACTION_TRANSPARENT || (device->pressed[code] && PRESSED_TO_LAYER(device, code) != device->top_activation->layer)))
    {
        // Handle modifier here if not mapped, to avoid activating the layer
        process_action(device, device->pressed[code] ? PRESSED_TO_LAYER(device, code) : find_key_layer(device, code, value), code, value, timestamp);
    }
    else
    {
        struct activation* activation = device->top_activation;
        switch (activation->kind)
        {
            case ACTIVATION_OVERLOAD_MOD:
            {
                // The overload-mod key is down and event key is not the overload-mod key
                if (IS_PRESS(value))
                {
                    // Key press
                    if (!activation->data.overload_mod.active)
                    {
                        int delayed_code = activation->data.overload_mod.delayed_code;
                        if (delayed_code == 0)
                        {
                            // Delay first key press after pressing the overload-mod key
                            activation->data.overload_mod.delayed_code = code;

                            if (timeout_has_expired(timestamp, activation->data.overload_mod.timeout_timestamp))
                            {
                                // Action timed-out, activate on first key press
                                activate_overload_mod(device, activation, code, timestamp);
                            }

                            return;
                        }

                        // A second key press activates the overload-mod layer
                        activate_overload_mod(device, activation, delayed_code, timestamp);
                    }
                }
                else
                {
                    // Key repeat or release
                    int delayed_code = activation->data.overload_mod.delayed_code;
                    if (code != delayed_code)
                    {
                        // A key that was pressed before the overload-mod key was pressed
                        if (device->pressed[code])
                        {
                            process_action(device, PRESSED_TO_LAYER(device, code), code, value, timestamp);
                        }
                        else
                        {
                            // Key was held down while starting the service, send its unprocessed code
                            emit(EV_KEY, code, value);
                        }
                        return;
                    }
                    if (!activation->data.overload_mod.active)
                    {
                        // Delayed key was repeated or released, activate the overload-mod layer
                        activate_overload_mod(device, activation, delayed_code, timestamp);
                    }
                }

                process_action(device, find_key_layer(device, code, value), code, value, timestamp);
                break;
            }
            case ACTIVATION_OVERLOAD_LAYER:
            {
                // The overload-layer key is down and event key is not the overload-layer key
                if (IS_PRESS(value))
                {
                    // Key press
                    if (!activation->data.overload_layer.active)
                    {
                        int delayed_code = activation->data.overload_layer.delayed_code;
                        if (delayed_code == 0)
                        {
                            // Delay first key press after pressing the overload-layer key
                            activation->data.overload_layer.delayed_code = code;

                            if (timeout_has_expired(timestamp, activation->data.overload_layer.timeout_timestamp))
                            {
                                // Action timed-out, activate on first key press
                                activate_overload(device, activation, code, timestamp);
                            }

                            return;
                        }

                        // A second key press activates the overload layer
                        activate_overload(device, activation, delayed_code, timestamp);
                    }
                }
                else
                {
                    // Key repeat or release
                    int delayed_code = activation->data.overload_layer.delayed_code;
                    if (code != delayed_code)
                    {
                        // A key that was pressed before the overload-layer key was pressed
                        if (device->pressed[code])
                        {
                            process_action(device, PRESSED_TO_LAYER(device, code), code, value, timestamp);
                        }
                        else
                        {
                            // Key was held down while starting the service, send its unprocessed code
                            emit(EV_KEY, code, value);
                        }
                        return;
                    }
                    if (!activation->data.overload_layer.active)
                    {
                        // Delayed key was repeated or released, activate the overload layer
                        activate_overload(device, activation, delayed_code, timestamp);
                    }
                }

                process_action(device, find_key_layer(device, code, value), code, value, timestamp);
                break;
            }
            case ACTIVATION_SHIFT_LAYER:
            {
                process_action(device, find_key_layer(device, code, value), code, value, timestamp);
                break;
            }
            case ACTIVATION_LATCH_LAYER:
            {
                struct layer* layer = find_key_layer(device, code, value);

                if (IS_PRESS(value))
                {
                    struct action* action = activation->action;
                    if (action != NULL)
                    {
                        // Latch-layer key was released and a key was pressed, deactivate layer
                        deactivate_layer(device, activation);

                        if (&layer->keymap[code] == action)
                        {
                            // Latch-layer key was pressed a second time, unlatch
                            device->pressed[code] = LAYER_TO_PRESSED(layer);
                            return;
                        }
                    }
                    else
                    {
                        // A key was pressed before releasing latch-layer key, convert to a shift activation
                        activation->kind = ACTIVATION_SHIFT_LAYER;
                    }
                }

                process_action(device, layer, code, value, timestamp);
                break;
            }
            case ACTIVATION_LOCK_LAYER:
            {
                if (IS_PRESS(value) && activation->action == NULL)
                {
                    // A key was pressed before releasing lock-layer key, convert to a shift activation
                    activation->kind = ACTIVATION_SHIFT_LAYER;
                }

                process_action(device, find_key_layer(device, code, value), code, value, timestamp);
                break;
            }
        }
    }
}
