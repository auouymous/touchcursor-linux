#ifndef binding_h
#define binding_h

#include "config.h"

/**
 * @brief The upper limit for enabling key events.
 *
 * There used to be KEY_MAX here, but that seems to be causing issues:
 * At one point there was an issue where the virtual device could not be created
 * if keys up to KEY_MAX (767) were included. 572 came from iterating down
 * from KEY_MAX until things started working again. Not sure what the underlying
 * cause is. For further reference, see
 * https://github.com/donniebreve/touchcursor-linux/pull/39#issuecomment-1000901050.
 */
#define MAX_KEYBIT 572

/**
 * Searches /proc/bus/input/devices for the device event.
 *
 * @param device The device entry.
 */
void find_device_event_path(struct input_device* device);

/**
 * Binds to the input devices using ioctl.
 * */
int bind_inputs();

/**
 * Releases the input devices.
 * */
void release_inputs();

/**
 * Read the input devices.
 * */
void read_inputs();

/**
 * The name of the output device.
 * */
extern char output_device_name[32];
/**
 * The sys path for the output device.
 * */
extern char output_sys_path[256];
/**
 * The output device key state.
 * */
extern int output_device_keystate[MAX_KEYBIT];
/**
 * The file descriptor for the output device.
 * */
extern int output_file_descriptor;

/**
 * Creates and binds a virtual output device using ioctl and uinput.
 * */
int bind_output();

/**
 * Releases any held keys on the output device.
 * */
void release_output_keys();

/**
 * Releases the virtual output device.
 * */
int release_output();

#endif
