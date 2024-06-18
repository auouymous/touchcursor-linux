#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>

#include "config.h"
#include "emit.h"
#include "keys.h"
#include "mapper.h"
#include "queue.h"

// The state machine state
enum states state = idle;

// Flag if the hyper key has been emitted
static int hyperEmitted;

/**
 * Checks if the key is the hyper key.
 * */
static int isHyper(int code)
{
    return code == hyperKey;
}

/**
 * Lookup a key sequence for an input device.
 */
static struct key_output find_key_output(struct input_device* device, int code)
{
    return hyper_keymap[code];
}

/**
 * Checks if the key has been mapped.
 * */
static int isMapped(struct input_device* device, int code)
{
    return find_key_output(device, code).sequence[0] != 0;
}

/**
 * Sends a hyper layer key sequence.
 * */
static void send_layer_key(struct input_device* device, int code, int value)
{
    struct key_output output = find_key_output(device, code);
    for (int i = 0; i < MAX_SEQUENCE; i++)
    {
        if (output.sequence[i] == 0)
        {
            break;
        }
        emit(EV_KEY, output.sequence[i], value);
    }
    if (value == 0)
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
    if (value == 0)
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
 * Processes a key input event. Converts and emits events as necessary.
 * */
void processKey(struct input_device* device, int type, int code, int value)
{
    code = device->remap[code];

    /* printf("processKey(in): code=%i value=%i state=%i\n", code, value, state); */
    switch (state)
    {
        case idle: // 0
        {
            if (isHyper(code) && isDown(value))
            {
                state = hyper;
                hyperEmitted = 0;
                clearQueue();
            }
            else
            {
                send_default_key(code, value);
            }
            break;
        }
        case hyper: // 1
        {
            if (isHyper(code))
            {
                if (!isDown(value))
                {
                    state = idle;
                    if (!hyperEmitted)
                    {
                        send_default_key(code, 1);
                    }
                    send_default_key(code, 0);
                }
            }
            else if (isMapped(device, code))
            {
                if (isDown(value))
                {
                    state = delay;
                    enqueue(code);
                }
                else
                {
                    send_default_key(code, value);
                }
            }
            else
            {
                if (!isModifier(code) && isDown(value))
                {
                    if (!hyperEmitted)
                    {
                        send_default_key(hyperKey, 1);
                        hyperEmitted = 1;
                    }
                }
                send_default_key(code, value);
            }
            break;
        }
        case delay: // 2
        {
            if (isHyper(code))
            {
                if (!isDown(value))
                {
                    state = idle;
                    if (!hyperEmitted)
                    {
                        send_default_key(hyperKey, 1);
                    }
                    send_default_queue(1);
                    send_default_key(hyperKey, 0);
                }
            }
            else if (isMapped(device, code))
            {
                state = map;
                if (isDown(value))
                {
                    if (lengthOfQueue() != 0)
                    {
                        send_layer_key(device, peek(), 1);
                    }
                    enqueue(code);
                    send_layer_key(device, code, value);
                }
                else
                {
                    send_layer_queue(device, 1);
                    send_layer_key(device, code, value);
                }
            }
            else
            {
                state = map;
                send_default_key(code, value);
            }
            break;
        }
        case map: // 3
        {
            if (isHyper(code))
            {
                if (!isDown(value))
                {
                    state = idle;
                    send_layer_queue(device, 0);
                }
            }
            else if (isMapped(device, code))
            {
                if (isDown(value))
                {
                    enqueue(code);
                }
                send_layer_key(device, code, value);
            }
            else
            {
                send_default_key(code, value);
            }
            break;
        }
    }
    /* printf("processKey(out): state=%i\n", state); */
}
