#ifndef mapper_h
#define mapper_h

// The state machine states
enum states
{
    idle,
    hyper,
    delay,
    map
};

extern enum states state;

/**
 * Processes a key input event. Converts and emits events as necessary.
 * */
void processKey(struct input_device* device, int code, int type, int value);

#endif
