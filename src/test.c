// build
// gcc -Wall src/keys.c src/leds.c src/strings.c src/binding.c src/config.c src/mapper.c src/test.c -o out/test
// run
// ./out/test

#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "keys.h"
#include "strings.h"

static int tests_run = 0;
static int tests_failed = 0;

// String for the full key event output
static char output[256];

// String for the emit function output
static char emitString[8];

// An input device for testing
struct input_device* test_device;

/*
 * Override of the emit function(s).
 */
void emit(int type, int code, int value)
{
    toggleOutputModifierState(code, value);

    sprintf(emitString, "%i:%i ", code, value);
    strcat(output, emitString);
}

// Now include the mapper
#include "mapper.h"

struct test_keys
{
    int code;
    char* name;
};
static struct test_keys test_keys[] = {
    {KEY_LEFTSHIFT, "leftshift"}, // not mapped in layer
    {KEY_LEFTCTRL, "leftctrl"}, // not mapped in layer
    {KEY_CANCEL, "cancel"}, // not mapped in layer

    {KEY_O, "other"}, // not mapped in layer

    {KEY_J, "m1"}, {KEY_LEFT, "layer_m1"}, // mapped
    {KEY_K, "m2"}, {KEY_DOWN, "layer_m2"}, // mapped
    {KEY_L, "m3"}, {KEY_RIGHT, "layer_m3"}, // mapped

    {KEY_S, "seq"}, {KEY_A, "lseq"}, {KEY_1, "seq1"}, {KEY_2, "seq2"}, {KEY_3, "seq3"}, {KEY_4, "seq4"}, // short and long sequences

    {KEY_R, "or1"}, {KEY_O, "or1_to"}, // other remap
    {KEY_E, "mr2"}, {KEY_K, "mr2_to"}, {KEY_5, "layer_mr2"}, // mapped remap

    {KEY_D, "disabled"},
    {KEY_M, "overload-mod"}, {KEY_LEFTSHIFT, "overload-mod-seq"},
    {KEY_N, "overload-mod-500ms"},
    {KEY_SPACE, "overload"},
    {KEY_B, "overload-500ms"},
    {KEY_COMMA, "shift"},
    {KEY_DOT, "latch"},
    {KEY_APOSTROPHE, "latch-menu"},
    {KEY_SLASH, "lock"},
    {KEY_BACKSLASH, "lock-overlay"},
    {KEY_F1, "akey"}, {KEY_F2, "ukey"}, {KEY_F3, "lukey"},
    {KEY_F4, "IM-none"},
    {KEY_F5, "IM-compose"},
    {KEY_F6, "IM-iso14755"},
    {KEY_F7, "IM-gtk"},

    {0, NULL}
};

/*
 * Get code for named key.
 */
static int lookup_key_code(char* name)
{
    for (int i = 0; test_keys[i].name != NULL; i++)
    {
        if (strcmp(name, test_keys[i].name) == 0) return test_keys[i].code;
    }

    int code = atoi(name);
    if (is_integer(name) && code >= 1 && code <= 65535) return code;

    return 0;
}

/*
 * Simulates typing keys.
 */
static int type(char* keys, char* expect)
{
    tests_run++;
    for (int i = 0; i < 256; i++) output[i] = 0;

    struct timeval timestamp = {0, 0};

    char* _keys = malloc(strlen(keys) + 1);
    strcpy(_keys, keys);
    char* tokens = _keys;
    char* token;
    while ((token = strsep(&tokens, ",")) != NULL)
    {
        token = trim_string(token);
        char* key = token;
        strsep(&token, " ");
        char* action = token;
        if (key == NULL || action == NULL)
        {
            printf("  FAIL [%s]\n    missing key or action '%s %s'\n", keys, key, action);
            free(_keys);
            return 1;
        }

        if (strcmp(key, "wait") == 0)
        {
            int milliseconds = atoi(action);
            struct timeval a = timestamp;
            struct timeval b = {0, milliseconds * 1000};
            timeradd(&a, &b, &timestamp);
            continue;
        }

        int code = lookup_key_code(key);
        if (code == 0)
        {
            printf("  FAIL [%s]\n    invalid key '%s'\n", keys, key);
            free(_keys);
            return 1;
        }

        if (strcmp(action, "down") == 0)
        {
            processKey(test_device, EV_KEY, code, 1, timestamp);
        }
        else if (strcmp(action, "repeat") == 0)
        {
            processKey(test_device, EV_KEY, code, 2, timestamp);
        }
        else if (strcmp(action, "up") == 0)
        {
            processKey(test_device, EV_KEY, code, 0, timestamp);
        }
        else if (strcmp(action, "tap") == 0)
        {
            processKey(test_device, EV_KEY, code, 1, timestamp);
            processKey(test_device, EV_KEY, code, 0, timestamp);
        }
        else
        {
            printf("  FAIL [%s]\n    invalid key action '%s'\n", keys, action);
            free(_keys);
            return 1;
        }
    }
    free(_keys);

    char expected_output[256];
    char* eo = expected_output;
    char* _expect = malloc(strlen(expect) + 1);
    strcpy(_expect, expect);
    tokens = _expect;
    if (*tokens == '\0')
    {
        expected_output[0] = '\0';
    }
    else while ((token = strsep(&tokens, ",")) != NULL)
    {
        token = trim_string(token);
        char* key = token;
        strsep(&token, " ");
        char* action = token;
        if (key == NULL || action == NULL)
        {
            printf("  FAIL [%s]\n    missing key or action '%s %s'\n", keys, key, action);
            free(_expect);
            return 1;
        }

        int code = lookup_key_code(key);
        if (code == 0)
        {
            printf("  FAIL [%s]\n    invalid expect key '%s'\n", keys, key);
            free(_expect);
            return 1;
        }

        if (strcmp(action, "down") == 0)
        {
            sprintf(eo, "%d:1 ", code);
        }
        else if (strcmp(action, "repeat") == 0)
        {
            sprintf(eo, "%d:2 ", code);
        }
        else if (strcmp(action, "up") == 0)
        {
            sprintf(eo, "%d:0 ", code);
        }
        else if (strcmp(action, "tap") == 0)
        {
            sprintf(eo, "%d:1 %d:0 ", code, code);
        }
        else
        {
            printf("  FAIL [%s]\n    invalid expect action '%s'\n", keys, action);
            free(_expect);
            return 1;
        }

        eo += strlen(eo);
    }
    free(_expect);

    if (strcmp(output, expected_output) != 0)
    {
        printf("  FAIL [%s]\n    expected: [%s]\n    expected: '%s'\n      output: '%s'\n", keys, expect, expected_output, output);
        return 1;
    }
    printf("  pass [%s]\n      output: '%s'\n", keys, output);
    return 0;
}
#define TYPE(keys, ignore, expect) tests_failed += type(keys, expect)

/*
 * Get code for named key or output error message.
 */
static int lookup_key_code_with_error(char* name)
{
    int code = lookup_key_code(name);
    if (code == 0)
    {
        printf("ERROR invalid key '%s'\n", name);
    }
    return code;
}
#define KEY(name) lookup_key_code_with_error(name)

/*
 * Tests for normal (slow) typing.
 * These tests should rarely have overlapping key events.
 */
static void testNormalTyping()
{
    printf("Normal typing tests...\n");

    TYPE("overload tap", EXPECT, "overload tap");

    TYPE("overload tap, overload tap", EXPECT, "overload tap, overload tap");

    TYPE("other down, overload tap, other up", EXPECT, "other down, overload tap, other up");

    TYPE("overload down, other tap, overload up", EXPECT, "other tap");

    TYPE("m1 down, overload tap, m1 up", EXPECT, "m1 down, overload tap, m1 up");

    TYPE("overload down, m1 tap, overload up", EXPECT, "layer_m1 tap");

    TYPE("m1 down, overload tap, m2 tap, m1 up", EXPECT, "m1 down, overload tap, m2 tap, m1 up");

    TYPE("overload down, seq tap, overload up", EXPECT, "seq1 down, seq2 down, seq2 up, seq1 up");

    TYPE("overload down, lseq tap, overload up",
        EXPECT, "seq1 down, seq2 down, seq3 down, seq4 down, seq4 up, seq3 up, seq2 up, seq1 up");

    TYPE("or1 tap", EXPECT, "other tap");

    TYPE("overload down, or1 tap, overload up", EXPECT, "other tap");

    TYPE("mr2 tap", EXPECT, "m2 tap");

    TYPE("overload down, mr2 tap, overload up", EXPECT, "layer_m2 tap");

    // The disabled key should be output
    TYPE("disabled tap", EXPECT, "disabled tap");

    // Nothing should be output
    TYPE("overload down, disabled tap, overload up", EXPECT, "");

    TYPE("overload-mod tap, other tap", EXPECT, "overload-mod tap, other tap");

    TYPE("overload-mod down, other tap, overload-mod up", EXPECT, "overload-mod-seq down, other tap, overload-mod-seq up");

    TYPE("shift tap, other tap", EXPECT, "other tap");

    TYPE("shift down, m1 tap, shift up", EXPECT, "layer_m1 tap");

    TYPE("shift down, m1 tap, m1 tap, shift up", EXPECT, "layer_m1 tap, layer_m1 tap");

    // Mapped key from latched layer is output along with its default code
    TYPE("latch tap, m1 tap, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from latched layer is output along with its default code
    TYPE("latch down, m1 tap, latch up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from latched layer is output twice along with its default code
    TYPE("latch down, m1 tap, m1 tap, latch up, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from latched-menu layer is output along with its default code
    TYPE("latch-menu tap, m1 tap, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from latched-menu layer is output along with its default code
    TYPE("latch-menu down, m1 tap, latch-menu up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from latched-menu layer is output twice along with its default code
    TYPE("latch-menu down, m1 tap, m1 tap, latch-menu up, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked layer is output twice
    TYPE("lock tap, m1 tap, m1 tap, lock tap, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked layer is output along with its default code
    TYPE("lock down, m1 tap, lock up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from locked layer is output twice along with its default code
    TYPE("lock down, m1 tap, m1 tap, lock up, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output twice
    TYPE("lock-overlay tap, m1 tap, m1 tap, lock-overlay tap, m1 tap, m1 tap", EXPECT, "layer_m1 tap, m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output along with its default code
    TYPE("lock-overlay down, m1 tap, lock-overlay up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output twice along with its default code
    TYPE("lock-overlay down, m1 tap, m1 tap, lock-overlay up, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output twice
    TYPE("lock tap, lock-overlay tap, m1 tap, m1 tap, lock-overlay tap, lock tap, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output twice, the unlock should also clear the lock-overlay
    TYPE("lock tap, lock-overlay tap, m1 tap, m1 tap, lock tap, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output along with its default code
    TYPE("lock tap, lock-overlay down, m1 tap, lock-overlay up, lock tap, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output twice along with its default code
    TYPE("lock tap, lock-overlay down, m1 tap, m1 tap, lock-overlay up, lock tap, m1 tap", EXPECT, "layer_m1 tap, layer_m1 tap, m1 tap");

    TYPE("overload-mod down, wait 1000, overload-mod up", EXPECT, "overload-mod tap");
    TYPE("overload-mod-500ms down, wait 250, overload-mod-500ms up", EXPECT, "overload-mod-500ms tap");
    TYPE("overload-mod-500ms down, wait 1000, overload-mod-500ms up", EXPECT, "overload-mod-seq tap");
    TYPE("overload-mod-500ms down, wait 250, overload-mod-500ms repeat, overload-mod-500ms up", EXPECT, "overload-mod-500ms tap");
    TYPE("overload-mod-500ms down, wait 1000, overload-mod-500ms repeat, overload-mod-500ms up", EXPECT, "overload-mod-seq tap");
    TYPE("overload-mod-500ms down, wait 250, other down, overload-mod-500ms up", EXPECT, "overload-mod-500ms down, other down, overload-mod-500ms up");
    TYPE("overload-mod-500ms down, wait 1000, other down", EXPECT, "overload-mod-seq down, other down");
        struct timeval timestamp = {2, 0};
        processKey(test_device, EV_KEY, KEY("overload-mod-500ms"), 0, timestamp); // Deactivate to avoid memory leaks

    TYPE("overload down, wait 1000, overload up", EXPECT, "overload tap");
    TYPE("overload-500ms down, wait 250, overload-500ms up", EXPECT, "overload-500ms tap");
    TYPE("overload-500ms down, wait 1000, overload-500ms up", EXPECT, "");
    TYPE("overload-500ms down, wait 250, overload-500ms repeat, overload-500ms up", EXPECT, "overload-500ms tap");
    TYPE("overload-500ms down, wait 1000, overload-500ms repeat, overload-500ms up", EXPECT, "");
    TYPE("overload-500ms down, wait 250, other down, overload-500ms up", EXPECT, "overload-500ms down, other down, overload-500ms up");
    TYPE("overload-500ms down, wait 1000, other down", EXPECT, "other down");
        processKey(test_device, EV_KEY, KEY("overload-500ms"), 0, timestamp); // Deactivate to avoid memory leaks

    // Save and restore pressed modifiers while sending input method sequences
    TYPE("leftctrl down, IM-iso14755 tap, ukey tap, leftctrl up", EXPECT,
        "leftctrl tap, leftctrl down, leftshift down, 4 tap, 48 tap, 2 tap, leftshift up, leftctrl up, leftctrl tap"); // ctrl-shift 3 B 1

    // Shifted layer
    TYPE("IM-iso14755 tap, leftshift down, ukey tap, leftshift up", EXPECT,
        "leftshift tap, leftctrl down, leftshift down, 4 tap, 10 tap, 2 tap, leftshift up, leftctrl up, leftshift tap"); // ctrl-shift 3 9 1

    TYPE("IM-none tap, akey tap", EXPECT, "leftshift down, 30 tap, leftshift up"); // shift A
    TYPE("IM-compose tap, akey tap", EXPECT, "cancel tap, 11 tap, 11 tap, 11 tap, 3 tap, 2 tap"); // cancel 0 0 0 3 2
    TYPE("IM-iso14755 tap, akey tap", EXPECT, "leftctrl down, leftshift down, 5 tap, 2 tap, leftshift up, leftctrl up"); // ctrl-shift 4 1
    TYPE("IM-gtk tap, akey tap", EXPECT, "leftctrl down, leftshift down, 22 tap, leftshift up, leftctrl up, 5 tap, 2 tap, 57 tap"); // ctrl-shift-U 4 1 space

    TYPE("IM-none tap, ukey tap", EXPECT, ""); // no output
    TYPE("IM-compose tap, ukey tap", EXPECT, "cancel tap, 11 tap, 11 tap, 11 tap, 20 tap, 35 tap"); // cancel 0 0 0 T H
    TYPE("IM-iso14755 tap, ukey tap", EXPECT, "leftctrl down, leftshift down, 4 tap, 48 tap, 2 tap, leftshift up, leftctrl up"); // ctrl-shift 3 B 1
    TYPE("IM-gtk tap, ukey tap", EXPECT, "leftctrl down, leftshift down, 22 tap, leftshift up, leftctrl up, 4 tap, 48 tap, 2 tap, 57 tap"); // ctrl-shift-U 3 B 1 space

    TYPE("IM-none tap, lukey tap", EXPECT, ""); // no output
    TYPE("IM-compose tap, lukey tap", EXPECT,
        "cancel tap, 11 tap, 11 tap, 11 tap, 20 tap, 35 tap, cancel tap, 11 tap, 11 tap, 11 tap, 20 tap, 23 tap");
        // cancel 0 0 0 T H cancel 0 0 0 T I
    TYPE("IM-iso14755 tap, lukey tap", EXPECT,
        "leftctrl down, leftshift down, 4 tap, 48 tap, 2 tap, leftshift up, leftctrl up, " \
        "leftctrl down, leftshift down, 4 tap, 48 tap, 3 tap, leftshift up, leftctrl up");
        // ctrl-shift 3 B 1 ctrl-shift 3 B 2
    TYPE("IM-gtk tap, lukey tap", EXPECT,
        "leftctrl down, leftshift down, 22 tap, leftshift up, leftctrl up, 4 tap, 48 tap, 2 tap, 57 tap, " \
        "leftctrl down, leftshift down, 22 tap, leftshift up, leftctrl up, 4 tap, 48 tap, 3 tap, 57 tap");
        // ctrl-shift-U 3 B 1 space ctrl-shift-U 3 B 2 space
}

/*
 * Tests for fast typing.
 * These tests should have many overlapping key events.
 */
static void testFastTyping()
{
    printf("Fast typing tests...\n");

    // The mapped key should not be converted
    TYPE("overload down, m1 down, overload up, m1 up", EXPECT, "overload down, m1 down, overload up, m1 up");

    // The mapped key should not be converted
    // This is not out of order, remember space down does not emit anything
    TYPE("m1 down, overload down, m1 up, overload up", EXPECT, "m1 tap, overload tap");

    // The mapped keys should be sent converted
    TYPE("overload down, m1 down, m2 down, overload up, m1 up, m2 up", EXPECT, "layer_m1 down, layer_m2 down, layer_m1 up, layer_m2 up");

    // The mapped keys should be sent converted
    TYPE("overload down, m1 down, m2 down, m3 down, overload up, m1 up, m2 up, m3 up",
        EXPECT, "layer_m1 down, layer_m2 down, layer_m3 down, layer_m1 up, layer_m2 up, layer_m3 up");

    TYPE("overload-mod down, other down, overload-mod up, other up", EXPECT, "overload-mod down, other down, overload-mod up, other up");

    TYPE("shift down, m1 down, shift up, m1 up", EXPECT, "layer_m1 tap");

    // Mapped key from latched layer is output along with its default code
    TYPE("latch down, m1 down, latch up, m1 up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from latched-menu layer is output along with its default code
    TYPE("latch-menu down, m1 down, latch-menu up, m1 up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from locked layer is output along with its default code
    TYPE("lock down, m1 down, lock up, m1 up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");

    // Mapped key from locked-overlay layer is output along with its default code
    TYPE("lock-overlay down, m1 down, lock-overlay up, m1 up, m1 tap", EXPECT, "layer_m1 tap, m1 tap");
}

/*
 * Tests for fast typing.
 * These tests should have many overlapping key events.
 */
static void testSpecialTyping()
{
    printf("Special typing tests...\n");

    // The key should be output, overload mode not retained
    TYPE("overload down, leftshift tap, overload up", EXPECT, "leftshift tap, overload tap");
}

/*
 * Main method.
 */
int main()
{
    test_device = registerInputDevice(0, "test", 1, registerLayer(0, NULL, "test Device"));
    test_device->inherit_remap = 1;

    int remap[MAX_KEYMAP];
    memset(remap, 0, sizeof(remap));

    struct layer* shift_layer = registerLayer(0, test_device->layer, "test Device shifted");
    test_device->layer->mod_layers[0] = shift_layer->index + 1;

    struct layer* test_layer = registerLayer(0, NULL, "test Bindings");

    test_device->layer->menu_layer = test_layer;

    uint16_t sequence[4];

    // default config
    sequence[0] = KEY("layer_m1"); setLayerKey(test_layer, KEY("m1"), 1, sequence);
    sequence[0] = KEY("layer_m2"); setLayerKey(test_layer, KEY("m2"), 1, sequence);
    sequence[0] = KEY("layer_m3"); setLayerKey(test_layer, KEY("m3"), 1, sequence);

    sequence[0] = KEY("seq1"); sequence[1] = KEY("seq2"); setLayerKey(test_layer, KEY("seq"), 2, sequence);
    sequence[0] = KEY("seq1"); sequence[1] = KEY("seq2"); sequence[2] = KEY("seq3"); sequence[3] = KEY("seq4"); setLayerKey(test_layer, KEY("lseq"), 4, sequence);

    remap[KEY("or1")] = KEY("other");
    remap[KEY("mr2")] = KEY("m2");
    sequence[0] = KEY("layer_mr2"); setLayerKey(test_layer, KEY("mr2"), 1, sequence);

    setLayerActionDisabled(test_layer, KEY("disabled"));
    sequence[0] = KEY("leftshift"); setLayerActionOverloadMod(test_device->layer, KEY("overload-mod"), 0, 1, sequence, KEY("overload-mod"), 0);
    sequence[0] = KEY("leftshift"); setLayerActionOverloadMod(test_device->layer, KEY("overload-mod-500ms"), 0, 1, sequence, KEY("overload-mod-500ms"), 500);
    setLayerActionOverload(test_device->layer, KEY("overload"), test_layer, 0, NULL, KEY("overload"), 0);
    setLayerActionOverload(test_device->layer, KEY("overload-500ms"), test_layer, 0, NULL, KEY("overload-500ms"), 500);
    setLayerActionShift(test_device->layer, KEY("shift"), test_layer, 0, NULL);
    setLayerActionLatch(test_device->layer, KEY("latch"), test_layer, 0, NULL);
    setLayerActionLatchMenu(test_device->layer, KEY("latch-menu"), 0);
    setLayerActionLock(test_device->layer, KEY("lock"), test_layer, 0, NULL, 0);
    setLayerActionLock(test_device->layer, KEY("lock-overlay"), test_layer, 0, NULL, 1);

    ukey_compose_key = KEY("cancel");
    uint8_t usequence[6];
    usequence[0] = 'A'; usequence[1] = 0x00; usequence[2] = 0x00; setLayerUKey(test_device->layer, KEY("akey"), 1, usequence); // A
    usequence[0] = 0xB1; usequence[1] = 0x03; usequence[2] = 0x00; setLayerUKey(test_device->layer, KEY("ukey"), 1, usequence); // α
    usequence[0] = 0x91; usequence[1] = 0x03; usequence[2] = 0x00; setLayerUKey(shift_layer, KEY("ukey"), 1, usequence); // Α
    usequence[0] = 0xB1; usequence[1] = 0x03; usequence[2] = 0x00; usequence[3] = 0xB2; usequence[4] = 0x03; usequence[5] = 0x00;
        setLayerUKey(test_device->layer, KEY("lukey"), 2, usequence); // αβ
    setLayerActionInputMethod(test_device->layer, KEY("IM-none"), input_method_none);
    setLayerActionInputMethod(test_device->layer, KEY("IM-compose"), input_method_compose);
    setLayerActionInputMethod(test_device->layer, KEY("IM-iso14755"), input_method_iso14755);
    setLayerActionInputMethod(test_device->layer, KEY("IM-gtk"), input_method_gtk);

    finalizeInputDevice(test_device, remap);

    remapBindings(remap, test_layer);

    testNormalTyping();
    testFastTyping();
    testSpecialTyping();

    printf("\nTests run: %d\n", tests_run);
    if (tests_failed > 0)
    {
        printf("*** %d tests FAILED ***\n", tests_failed);
    }
    else
    {
        printf("All tests passed!\n");
    }

    return tests_failed;
}

// Sample tests from touchcursor source

// normal (slow) typing
// CHECK((SP, up,  SP,up, 0));
// CHECK((SP, dn,  SP,up, 0));
// CHECK((SP, up,  SP,up, SP,dn, SP,up, 0));
// CHECK((x, dn,   SP,up, SP,dn, SP,up, x,dn, 0));
// CHECK((x, up,   SP,up, SP,dn, SP,up, x,dn, x,up, 0));
// CHECK((j, dn,   SP,up, SP,dn, SP,up, x,dn, x,up, j,dn, 0));
// CHECK((j, up,   SP,up, SP,dn, SP,up, x,dn, x,up, j,dn, j,up, 0));

// overlapped slightly
// resetOutput();
// CHECK((SP, dn,  0));
// CHECK((x, dn,   SP,dn, x,dn, 0));
// CHECK((SP, up,  SP,dn, x,dn, SP,up, 0));
// CHECK((x, up,   SP,dn, x,dn, SP,up, x,up, 0));

//... plus repeating spaces
// CHECK((SP, dn,  0));
// CHECK((SP, dn,  0));
// CHECK((j, dn,   0));
// CHECK((SP, dn,  0));
// CHECK((SP, up,  SP,dn, j,dn, SP,up, 0));
// CHECK((j, up,   SP,dn, j,dn, SP,up, j,up, 0));

// key ups in waitMappedDown
// CHECK((SP, dn,  0));
// CHECK((x, up,   x,up, 0));
// CHECK((j, up,   x,up, j,up, 0));
// CHECK((SP, up,  x,up, j,up, SP,dn, SP,up, 0));

// other keys in waitMappedUp
// CHECK((SP, dn,  0));
// CHECK((j, dn,   0));
// CHECK((x, up,   SP,dn, j,dn, x,up, 0));
// CHECK((x, dn,   SP,dn, j,dn, x,up, x,dn, 0));
// CHECK((j, dn,   SP,dn, j,dn, x,up, x,dn, j,dn, 0));
// CHECK((SP, up,  SP,dn, j,dn, x,up, x,dn, j,dn, SP,up, 0));

// CHECK((SP, dn,  0));
// CHECK((j, dn,   0));
// CHECK((x, dn,   SP,dn, j,dn, x,dn, 0));
// CHECK((SP, up,  SP,dn, j,dn, x,dn, SP,up, 0));

// activate mapping
// CHECK((SP, dn,  0));
// CHECK((j, dn,   0));
// CHECK((j, up,   LE,edn, LE,up, 0));
// CHECK((SP, up,  LE,edn, LE,up, 0));

// autorepeat into mapping, and out
// CHECK((SP, dn,  0));
// CHECK((j, dn,   0));
// CHECK((j, dn,   LE,edn, LE,edn, 0));
// CHECK((j, dn,   LE,edn, LE,edn, LE,edn, 0));
// CHECK((j, up,   LE,edn, LE,edn, LE,edn, LE,up, 0));
// CHECK((SP, dn,  LE,edn, LE,edn, LE,edn, LE,up, 0));
// CHECK((j, dn,   LE,edn, LE,edn, LE,edn, LE,up, LE,edn, 0));
// CHECK((SP, up,  LE,edn, LE,edn, LE,edn, LE,up, LE,edn, LE,up, 0));
// CHECK((j, dn,   LE,edn, LE,edn, LE,edn, LE,up, LE,edn, LE,up, j,dn, 0));
// CHECK((j, up,   LE,edn, LE,edn, LE,edn, LE,up, LE,edn, LE,up, j,dn, j,up, 0));

// other keys during mapping
// CHECK((SP, dn,  0));
// CHECK((j, dn,   0));
// CHECK((j, up,   LE,edn, LE,up, 0));
// CHECK((x, dn,   LE,edn, LE,up, x,dn, 0));
// CHECK((x, up,   LE,edn, LE,up, x,dn, x,up, 0));
// CHECK((j, dn,   LE,edn, LE,up, x,dn, x,up, LE,edn, 0));
// CHECK((SP, up,  LE,edn, LE,up, x,dn, x,up, LE,edn, LE,up, 0));

// check space-emmitted states
// CHECK((SP, dn,  0));
// CHECK((x, dn,   SP,dn, x,dn, 0));
// CHECK((SP, dn,  SP,dn, x,dn, 0));
// CHECK((x, dn,   SP,dn, x,dn, x,dn, 0));
// CHECK((x, up,   SP,dn, x,dn, x,dn, x,up, 0));
// CHECK((j, up,   SP,dn, x,dn, x,dn, x,up, j,up, 0));
// CHECK((j, dn,   SP,dn, x,dn, x,dn, x,up, j,up, 0));
// CHECK((j, up,   SP,dn, x,dn, x,dn, x,up, j,up, LE,edn, LE,up, 0));
// CHECK((SP, up,  SP,dn, x,dn, x,dn, x,up, j,up, LE,edn, LE,up, 0)); //XXX should this emit a space (needs mappingSpaceEmitted state)

// wmuse
// CHECK((SP, dn,  0));
// CHECK((x, dn,   SP,dn, x,dn, 0));
// CHECK((j, dn,   SP,dn, x,dn, 0));
// CHECK((SP, dn,  SP,dn, x,dn, 0));
// CHECK((SP, up,  SP,dn, x,dn, j,dn, SP,up, 0));

// CHECK((SP, dn,  0));
// CHECK((x, dn,   SP,dn, x,dn, 0));
// CHECK((j, dn,   SP,dn, x,dn, 0));
// CHECK((j, dn,   SP,dn, x,dn, LE,edn, LE,edn, 0));
// CHECK((SP, up,  SP,dn, x,dn, LE,edn, LE,edn, LE,up, 0)); //XXX should this emit a space (needs mappingSpaceEmitted state)

// CHECK((SP, dn,  0));
// CHECK((x, dn,   SP,dn, x,dn, 0));
// CHECK((j, dn,   SP,dn, x,dn, 0));
// CHECK((x, up,   SP,dn, x,dn, x,up, 0));
// CHECK((j, up,   SP,dn, x,dn, x,up, LE,edn, LE,up, 0));
// CHECK((SP, up,  SP,dn, x,dn, x,up, LE,edn, LE,up, 0)); //XXX should this emit a space (needs mappingSpaceEmitted state)

// run configure tests
// idle
// CHECK((F5, dn,  F5,dn, 0));
// CHECK((SP, dn,  F5,dn, 0));
// wmd
// CHECK((F5, dn,  F5,dn, '*',dn, 0));
// CHECK((F5, up,  F5,dn, '*',dn, F5,up, 0));
// CHECK((j, dn,   F5,dn, '*',dn, F5,up, 0));
// wmu
// CHECK((F5, dn,  F5,dn, '*',dn, F5,up, '*',dn, 0));
// CHECK((j, up,   F5,dn, '*',dn, F5,up, '*',dn, LE,edn, LE,up, 0));
// mapping
// CHECK((F5, dn,  F5,dn, '*',dn, F5,up, '*',dn, LE,edn, LE,up, '*',dn, 0));
// CHECK((SP, up,  F5,dn, '*',dn, F5,up, '*',dn, LE,edn, LE,up, '*',dn, 0));

// CHECK((SP, dn,  0));
// wmd
// CHECK((x, dn,   SP,dn, x,dn, 0));
// wmd-se
// CHECK((F5, dn,  SP,dn, x,dn, '*',dn, 0));
// CHECK((j, dn,   SP,dn, x,dn, '*',dn, 0));
// wmu-se
// CHECK((F5, dn,  SP,dn, x,dn, '*',dn, '*',dn, 0));
// CHECK((SP, up,  SP,dn, x,dn, '*',dn, '*',dn, j,dn, SP,up, 0));

// Overlapping mapped keys
// CHECK((SP, dn,  0));
// CHECK((m, dn,   0));
// CHECK((j, dn,   DEL,edn, LE,edn, 0));
// CHECK((j, up,   DEL,edn, LE,edn, LE,up, 0));
// CHECK((m, up,   DEL,edn, LE,edn, LE,up, DEL,up, 0));
// CHECK((SP, up,  DEL,edn, LE,edn, LE,up, DEL,up, 0));

// Overlapping mapped keys -- space up first.
// should release held mapped keys.  (Fixes sticky Shift bug.)
// CHECK((SP, dn,  0));
// CHECK((m, dn,   0));
// CHECK((j, dn,   DEL,edn, LE,edn, 0));
// release order is in vk code order
// CHECK((SP, up,  DEL,edn, LE,edn, LE,up, DEL,up, 0));

// mapped modifier keys
// options.keyMapping['C'] = ctrlFlag | 'C'; // ctrl+c
// CHECK((SP, dn,  0));
// CHECK((c, dn,   0));
// CHECK((c, up,   ctrl,dn, c,dn, ctrl,up, c,up, 0));
// CHECK((c, dn,   ctrl,dn, c,dn, ctrl,up, c,up, ctrl,dn, c,dn, ctrl,up, 0));
// CHECK((c, up,   ctrl,dn, c,dn, ctrl,up, c,up, ctrl,dn, c,dn, ctrl,up, c,up, 0));
// CHECK((SP, up,  ctrl,dn, c,dn, ctrl,up, c,up, ctrl,dn, c,dn, ctrl,up, c,up, 0));
// with modifier already down:
// CHECK((SP, dn,  0));
// CHECK((ctrl,dn, ctrl,dn, 0));
// CHECK((c, dn,   ctrl,dn, 0));
// CHECK((c, up,   ctrl,dn, c,dn, c,up, 0));
// CHECK((ctrl,up, ctrl,dn, c,dn, c,up, ctrl,up, 0));
// CHECK((SP,up,   ctrl,dn, c,dn, c,up, ctrl,up, 0));

// training mode
// options.trainingMode = true;
// options.beepForMistakes = false;
// CHECK((x, dn,   x,dn, 0));
// CHECK((x, up,   x,dn, x,up, 0));
// CHECK((LE, edn, x,dn, x,up, 0));
// CHECK((LE, up,  x,dn, x,up, 0));
// with modifier mapping
// CHECK((c, dn,    c,dn, 0));
// CHECK((c, up,    c,dn, c,up, 0));
// CHECK((ctrl, dn, c,dn, c,up, ctrl,dn, 0));
// CHECK((c, dn,    c,dn, c,up, ctrl,dn, 0));
// CHECK((c, up,    c,dn, c,up, ctrl,dn, 0));
// CHECK((ctrl, up, c,dn, c,up, ctrl,dn, ctrl,up, 0));

// SM.printUnusedTransitions();
