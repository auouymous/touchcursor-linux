#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "beep.h"
#include "buffers.h"

static char* speaker_filename = "/dev/input/by-path/platform-pcspkr-event-spkr";
static int speaker_file_descriptor = -1;

/**
 * Open PC Speaker.
 * */
void openSpeaker()
{
    speaker_file_descriptor = open(speaker_filename, O_RDWR);
    if (speaker_file_descriptor == -1)
    {
        log("info: can not open %s, beeps are not available\n", speaker_filename);
        return;
    }
    int capabilities;
    if (ioctl(speaker_file_descriptor, EVIOCGBIT(0, EV_MAX), &capabilities) < 0)
    {
        close(speaker_file_descriptor);
        speaker_file_descriptor = -1;
        log("info: can not ioctl %s, beeps are not available\n", speaker_filename);
        return;
    }
    if (capabilities & EV_SND)
    {
        close(speaker_file_descriptor);
        speaker_file_descriptor = -1;
        log("info: %s does not support sound, beeps are not available\n", speaker_filename);
        return;
    }

    log("info: beeps supported\n");
}

/**
 * Close PC Speaker.
 * */
void closeSpeaker()
{
    if (speaker_file_descriptor == -1) return;

    close(speaker_file_descriptor);
    speaker_file_descriptor = -1;
}

/**
 * Send event to PC Speaker.
 * */
static int play(int frequency){
    struct input_event ev;
    ev.type = EV_SND;
    ev.code = SND_TONE;
    ev.value = frequency;
    if (write(speaker_file_descriptor, &ev, sizeof(struct input_event)) < 0)
    {
        error("error: can not play beep\n");
        return 0;
    }
    return 1;
}

/**
 * Play a sound on PC Speaker.
 * */
void beep(int frequency, int duration_ms)
{
    if (speaker_file_descriptor == -1) return; // no speaker

    if (!play(frequency)) return; // fail
    usleep(duration_ms * 1000);
    play(0);
}
