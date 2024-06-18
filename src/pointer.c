#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "binding.h"
#include "pointer.h"

/**
 * Emits a relative pointer event.
 * */
void emit_pointer_relative(int type, int code_x, int value_x, int code_y, int value_y)
{
    if (value_x == 0 && value_y == 0) return;

    // printf("emit pointer relative: code_x=%i value_x=%i code_y=%i value_y=%i\n", code_x, value_x, code_y, value_y);
    struct input_event e[3];
    int syn_i = (value_x != 0 && value_y != 0) ? 2 : 1;
    int size = sizeof(struct input_event) * (syn_i + 1);
    memset(e, 0, size);

    // Emit the virtual pointer code / value
    if (value_x != 0)
    {
        // time.tv_sec = 0
        // time.tv_usec = 0
        e[0].type = type;
        e[0].code = code_x;
        e[0].value = value_x;
    }
    if (value_y != 0)
    {
        int y_i = syn_i - 1;
        // time.tv_sec = 0
        // time.tv_usec = 0
        e[y_i].type = type;
        e[y_i].code = code_y;
        e[y_i].value = value_y;
    }

    // Emit a syn event
    // time.tv_sec = 0
    // time.tv_usec = 0
    e[syn_i].type = EV_SYN;
    e[syn_i].code = SYN_REPORT;
    // value = 0;

    write(output_file_descriptor, &e, size);
}
