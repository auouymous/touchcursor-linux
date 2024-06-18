#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "binding.h"
#include "buffers.h"
#include "emit.h"
#include "mapper.h"
#include "strings.h"

// The output device
char output_device_name[32] = "Virtual TouchCursor Keyboard";
char output_sys_path[256] = { '\0' };
int output_device_keystate[MAX_KEYBIT];
int output_file_descriptor = -1;

/**
 * Searches /proc/bus/input/devices for the device event.
 *
 * @param device The device entry.
 */
void find_device_event_path(struct input_device* device)
{
    log("info: searching for device %s:%i\n", device->name, device->number);
    device->event_path[0] = '\0';
    FILE* devices_file = fopen("/proc/bus/input/devices", "r");
    if (!devices_file)
    {
        error("error: could not open /proc/bus/input/devices\n");
        return;
    }
    char* line = NULL;
    int matched_name = 0;
    int matched_count = 0;
    int found_event = 0;
    size_t length = 0;
    ssize_t result;
    while (!found_event && (result = getline(&line, &length, devices_file)) != -1)
    {
        if (length < 3) continue;
        if (isspace(line[0])) continue;
        if (!matched_name)
        {
            if (!starts_with(line, "N: ")) continue;
            char* trimmed_line = trim_string(line + 3);
            if (strcmp(trimmed_line, device->name) == 0)
            {
                if (device->number == ++matched_count)
                {
                    matched_name = 1;
                }
                continue;
            }
        }
        if (matched_name)
        {
            if (!starts_with(line, "H: Handlers")) continue;
            char* tokens = line;
            char* token = strsep(&tokens, "=");
            while (tokens != NULL)
            {
                token = strsep(&tokens, " ");
                if (starts_with(token, "event"))
                {
                    sprintf(device->event_path, "/dev/input/%s", token);
                    log("info: found the device event path: %s\n", device->event_path);
                    found_event = 1;
                    break;
                }
            }
        }
    }
    fclose(devices_file);
    if (line) free(line);
    if (!found_event)
    {
        error("error: could not find the event path for device: %s:%i\n", device->name, device->number);
    }
}

/**
 * Binds to the input device using ioctl.
 * */
static int bind_input(struct input_device* device)
{
    // Open the keyboard device
    log("info: attempting to capture: %s\n", device->event_path);
    device->file_descriptor = open(device->event_path, O_RDONLY);
    if (device->file_descriptor < 0)
    {
        error("error: failed to open the input device: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Retrieve the device name
    if (ioctl(device->file_descriptor, EVIOCGNAME(sizeof(device->name)), device->name) < 0)
    {
        error("error: failed to get the device name (EVIOCGNAME: %s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Check that the device is not our virtual device
    if (strcasestr(device->name, "Virtual TouchCursor Keyboard") != NULL)
    {
        error("error: you cannot capture the virtual device: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Allow last key press to go through
    // Grabbing the keys too quickly prevents the last key up event from being sent
    // https://bugs.freedesktop.org/show_bug.cgi?id=101796
    usleep(200 * 1000);
    // Grab keys from the input device
    if (ioctl(device->file_descriptor, EVIOCGRAB, 1) < 0)
    {
        error("error: failed to capture the device (EVIOCGRAB: %s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    log("info: successfully captured input device: \"%s\":%i (%s)\n", device->name, device->number, device->event_path);
    return EXIT_SUCCESS;
}

/**
 * Binds to the input devices using ioctl.
 * */
int bind_inputs()
{
    int bound = 0;
    for (int i = 0; i < nr_input_devices; i++)
    {
        struct input_device* device = &input_devices[i];
        if (device->event_path[0] != '\0' && bind_input(device) == EXIT_SUCCESS)
        {
            bound++;
        }
    }

    if (!bound)
    {
        error("error: no input device was configured (or the event path was not found).\n");
        return 0;
    }

    return bound;
}

/**
 * Releases the input device.
 * */
static void release_input(struct input_device* device)
{
    log("info: releasing: \"%s\":%i (%s)\n", device->name, device->number, device->event_path);
    ioctl(device->file_descriptor, EVIOCGRAB, 0);
    close(device->file_descriptor);
    device->file_descriptor = -1;
}

/**
 * Releases the input devices.
 * */
void release_inputs()
{
    for (int i = 0; i < nr_input_devices; i++)
    {
        struct input_device* device = &input_devices[i];
        if (device->file_descriptor > 0)
        {
            release_input(device);
        }
    }
}

/**
 * Read the input devices.
 * */
void read_inputs()
{
    fd_set set;
    FD_ZERO(&set);
    int max_fd = 0;
    for (int i = 0; i < nr_input_devices; i++)
    {
        int fd = input_devices[i].file_descriptor;
        if (fd > 0)
        {
            FD_SET(fd, &set);
            if (fd > max_fd) max_fd = fd;
        }
    }
    int n = select(max_fd+1, &set, NULL, NULL, NULL);
    if(n > 0){
        struct input_event event;
        ssize_t result;
        for (int i = 0; i < nr_input_devices; i++)
        {
            struct input_device* device = &input_devices[i];
            int fd = device->file_descriptor;
            if (FD_ISSET(fd, &set))
            {
                result = read(fd, &event, sizeof(event));
                if (result == (ssize_t)-1)
                {
                    if (errno == EINTR)
                    {
                        return;
                    }
                    else
                    {
                        error("error: unable to read input event for %s:%i (%s)\n", device->name, device->number, strerror(errno));
                        release_input(device);
                    }
                }
                if (result == (ssize_t)0)
                {
                    error("error: received EOF while reading input event for %s:%i\n", device->name, device->number);
                    release_input(device);
                }
                if (result != sizeof(event))
                {
                    warn("warning: partial input event received\n");
                    return;
                }
                // We only want to manipulate key presses
                if (event.type == EV_KEY
                    && (event.value == 0 || event.value == 1 || event.value == 2))
                {
                    processKey(device, event.type, event.code, event.value);
                }
                else
                {
                    emit(event.type, event.code, event.value);
                }
            }
        }
    }
}

/**
 * Creates and binds a virtual output device using ioctl and uinput.
 * */
int bind_output()
{
    // Define the virtual keyboard
    struct uinput_setup virtual_keyboard;
    memset(&virtual_keyboard, 0, sizeof(virtual_keyboard));
    strcpy(virtual_keyboard.name, output_device_name);
    virtual_keyboard.id.bustype = BUS_USB;
    virtual_keyboard.id.vendor = 0x01;
    virtual_keyboard.id.product = 0x01;
    virtual_keyboard.id.version = 1;
    // Open uinput
    output_file_descriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (output_file_descriptor < 0)
    {
        error("error: failed to open /dev/uinput: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Enable key press/release events
    if (ioctl(output_file_descriptor, UI_SET_EVBIT, EV_KEY) < 0)
    {
        error("error: failed to set EV_KEY on output (UI_SET_KEYBIT, EV_KEY: %s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Enable the set of KEY events
    for (int i = 0; i <= MAX_KEYBIT; i++)
    {
        int result = ioctl(output_file_descriptor, UI_SET_KEYBIT, i);
        if (result < 0)
        {
            error("error: failed to set key bit (UI_SET_KEYBIT, %i: %s)\n", i, strerror(errno));
            return EXIT_FAILURE;
        }
    }
    // Set up the device
    if (ioctl(output_file_descriptor, UI_DEV_SETUP, &virtual_keyboard) < 0)
    {
        error("error: failed to set up the virtual device (UI_DEV_SETUP: %s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Create the device
    if (ioctl(output_file_descriptor, UI_DEV_CREATE) < 0)
    {
        error("error: failed to create the virtual device (UI_DEV_CREATE: %s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    // Get the device path
    char sysname[16];
    if (ioctl(output_file_descriptor, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0)
    {
        error("error: failed to get the sysfs name (UI_GET_SYSNAME: %s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    strcat(output_sys_path, "/sys/devices/virtual/input/");
    strcat(output_sys_path, sysname);
    log("info: successfully created output device: %s (%s)\n", output_device_name, output_sys_path);
    return EXIT_SUCCESS;
}

/**
 * Releases any held keys on the output device.
 * */
void release_output_keys()
{
    for (int i = 0; i < MAX_KEYBIT; i++)
    {
        if (output_device_keystate[i] > 0)
        {
            /* log("info: releasing key: %i\n", i); */
            emit(EV_KEY, i, 0);
            output_device_keystate[i] = 0;
        }
    }
}

/**
 * Releases the virtual output device.
 * */
int release_output()
{
    if (output_file_descriptor > 0)
    {
        log("info: releasing: %s (%s)\n", output_device_name, output_sys_path);
        ioctl(output_file_descriptor, UI_DEV_DESTROY);
        close(output_file_descriptor);
    }
    return EXIT_SUCCESS;
}
