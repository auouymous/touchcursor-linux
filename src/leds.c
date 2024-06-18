#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "leds.h"

/**
 * Map string name or symbol to led code.
 * */
struct led
{
    const char* name;
    int code;
};

#define LED(code, name) {name, LED_##code}
static const struct led leds[] = {
    LED(NUML, "numlock"),
    LED(CAPSL, "capslock"),
    LED(SCROLLL, "scrolllock"),
    LED(COMPOSE, "compose"),
    LED(KANA, "kana"),
    LED(SLEEP, "sleep"),
    LED(SUSPEND, "suspend"),
    LED(MUTE, "mute"),
    LED(MISC, "misc"),
    LED(MAIL, "mail"),
    LED(CHARGING, "charging"),

    {NULL, 0}
};

/**
 * Output the led list to console.
 * */
void outputLedList()
{
        for (int i = 0; leds[i].name != NULL; i++)
        {
            printf("% 4d:  %s\n", leds[i].code, leds[i].name);
        }
}

/**
 * Converts an led string (e.g. "numlock") to its corresponding code.
 * */
int convertLedStringToCode(char* ledString)
{
    if (ledString == NULL) return -1;

    for (int i = 0; leds[i].name != NULL; i++)
    {
        if (strcmp(ledString, leds[i].name) == 0) return leds[i].code;
    }

    return -1;
}

/**
 * Set an led on input device.
 * */
void set_led(struct input_device* device, int led, int state)
{
    if (state != device->leds[led])
    {
        struct input_event e;
        e.time.tv_sec = 0;
        e.time.tv_usec = 0;
        // Set the virtual key code / value
        e.type = EV_LED;
        e.code = led;
        e.value = state;
        write(device->file_descriptor, &e, sizeof(e));

        device->leds[led] = state;

        // Emit a syn event
        e.type = EV_SYN;
        e.code = SYN_REPORT;
        e.value = 0;
        write(device->file_descriptor, &e, sizeof(e));
    }
}
