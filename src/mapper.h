#ifndef mapper_h
#define mapper_h

/**
 * Processes a key input event. Converts and emits events as necessary.
 * */
void processKey(struct input_device* device, int code, int type, int value, struct timeval timestamp);

#endif
