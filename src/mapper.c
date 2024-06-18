#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>

#include "config.h"
#include "emit.h"
#include "keys.h"
#include "mapper.h"
#include "queue.h"

static int hyperDown = 0;
static int hyperActive = 0;

/**
 * Lookup a key action for an input device.
 */
static struct action find_key_action(struct input_device* device, int code)
{
    return hyper_keymap[code].kind != ACTION_TRANSPARENT ? hyper_keymap[code] : device->keymap[code];
}

/**
 * Sends a hyper layer key sequence.
 * */
static void send_layer_key(struct input_device* device, int code, int value)
{
    struct action action = find_key_action(device, code);
    switch (action.kind)
    {
        case ACTION_TRANSPARENT: break;
        case ACTION_KEY:
        {
            emit(EV_KEY, action.data.key.code, value);
            break;
        }
        case ACTION_KEYS:
        {
            for (int i = 0; i < MAX_SEQUENCE; i++)
            {
                if (action.data.keys.codes[i] == 0)
                {
                    break;
                }
                emit(EV_KEY, action.data.keys.codes[i], value);
            }
            break;
        }
    }

    if (IS_RELEASE(value))
    {
        removeKeyFromQueue(code);
    }
}

/**
 * Sends all keys in the hyper queue.
 * */
static void send_layer_queue(struct input_device* device, int value)
{
    int length = lengthOfQueue();
    for (int i = 0; i < length; i++)
    {
        send_layer_key(device, dequeue(), value);
    }
}

/**
 * Sends a default key.
 * */
static void send_default_key(int code, int value)
{
    emit(EV_KEY, code, value);
    if (IS_RELEASE(value))
    {
        removeKeyFromQueue(code);
    }
}

/**
 * Sends all keys in the default queue.
 * */
static void send_default_queue(int value)
{
    int length = lengthOfQueue();
    for (int i = 0; i < length; i++)
    {
        send_default_key(dequeue(), value);
    }
}

/**
 * Process a hyper key event.
 * */
static void process_hyper(struct input_device* device, int code, int value)
{
    // Ignore repeat events
    if (IS_PRESS(value))
    {
        hyperDown = 1;
        hyperActive = 0;
        clearQueue();
    }
    else if (IS_RELEASE(value))
    {
        hyperDown = 0;
        if (hyperActive)
        {
            // Flush hyper layer queue
            send_layer_queue(device, 0);
            hyperActive = 0;
        }
        else
        {
            // Send hyper key and delayed key code, if one
            send_default_key(hyperKey, 1);
            send_default_queue(1);
            send_default_key(hyperKey, 0);
        }
    }
}

/**
 * Process a key action.
 * */
static void process_action(struct input_device* device, int code, int value)
{
    if (code == hyperKey)
    {
        process_hyper(device, code, value);
    }
    else
    {
        send_default_key(code, value);
    }
}

/**
 * Processes a key input event. Converts and emits events as necessary.
 * */
void processKey(struct input_device* device, int type, int code, int value)
{
    code = device->remap[code];

    if (!hyperDown)
    {
        process_action(device, code, value);
    }
    else if (code == hyperKey)
    {
        // Repeat or release hyper key
        process_hyper(device, code, value);
    }
    else if (isModifier(code) && hyper_keymap[code].kind == ACTION_TRANSPARENT)
    {
        // Handle modifier here if not mapped, to avoid activating the hyper layer
        send_default_key(code, value);
    }
    else
    {
        // The hyper key is down and event key is not the hyper key
        if (IS_PRESS(value))
        {
            // Key press
            if (!hyperActive)
            {
                if (lengthOfQueue() == 0)
                {
                    // Queue and delay first key press after pressing the hyper key
                    enqueue(code);
                    return;
                }

                // A second key press activates the hyper layer
                hyperActive = 1;
                // Send press event for delayed key code
                send_layer_key(device, peek(), 1);
            }

            // Queue key press
            enqueue(code);
        }
        else
        {
            // Key repeat or release
            if (!inQueue(code))
            {
                // A key that was pressed before the hyper key was pressed
                send_default_key(code, value);
                return;
            }
            if (!hyperActive)
            {
                // Delayed key was repeated or released, activate the hyper layer
                hyperActive = 1;
                // Send press event for delayed key code
                send_layer_key(device, peek(), 1);
            }
        }

        send_layer_key(device, code, value);
    }
}
