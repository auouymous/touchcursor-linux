#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>

#include "keys.h"

/**
 * Map string name or symbol to key or button code.
 * */
struct key
{
    const char* name;
    const char* symbol;
    int code;
};

#define KEYS(name, symbol) {#name, symbol, KEY_##name}
#define KEY_(name) {#name, NULL, KEY_##name}
static const struct key keys[] = {
    KEYS(ESC, "ESCAPE"),
    KEY_(1),
    KEY_(2),
    KEY_(3),
    KEY_(4),
    KEY_(5),
    KEY_(6),
    KEY_(7),
    KEY_(8),
    KEY_(9),
    KEY_(0),
    KEYS(MINUS, "-"),
    KEYS(EQUAL, "="),
    KEY_(BACKSPACE),
    KEY_(TAB),
    KEY_(Q),
    KEY_(W),
    KEY_(E),
    KEY_(R),
    KEY_(T),
    KEY_(Y),
    KEY_(U),
    KEY_(I),
    KEY_(O),
    KEY_(P),
    KEYS(LEFTBRACE, "["),
    KEYS(RIGHTBRACE, "]"),
    KEY_(ENTER),
    KEYS(LEFTCTRL, "CTRL"),
    KEY_(A),
    KEY_(S),
    KEY_(D),
    KEY_(F),
    KEY_(G),
    KEY_(H),
    KEY_(J),
    KEY_(K),
    KEY_(L),
    KEYS(SEMICOLON, ";"),
    KEYS(APOSTROPHE, "'"),
    KEYS(GRAVE, "`"),
    KEYS(LEFTSHIFT, "SHIFT"),
    KEYS(BACKSLASH, "\\"),
    KEY_(Z),
    KEY_(X),
    KEY_(C),
    KEY_(V),
    KEY_(B),
    KEY_(N),
    KEY_(M),
    KEYS(COMMA, ","),
    KEYS(DOT, "."),
    KEYS(SLASH, "/"),
    KEY_(RIGHTSHIFT),
    KEY_(KPASTERISK),
    KEYS(LEFTALT, "ALT"),
    KEY_(SPACE),
    KEY_(CAPSLOCK),
    KEY_(F1),
    KEY_(F2),
    KEY_(F3),
    KEY_(F4),
    KEY_(F5),
    KEY_(F6),
    KEY_(F7),
    KEY_(F8),
    KEY_(F9),
    KEY_(F10),
    KEY_(NUMLOCK),
    KEY_(SCROLLLOCK),
    KEY_(KP7),
    KEY_(KP8),
    KEY_(KP9),
    KEY_(KPMINUS),
    KEY_(KP4),
    KEY_(KP5),
    KEY_(KP6),
    KEY_(KPPLUS),
    KEY_(KP1),
    KEY_(KP2),
    KEY_(KP3),
    KEY_(KP0),
    KEY_(KPDOT),
    KEY_(ZENKAKUHANKAKU),
    KEY_(102ND),
    KEY_(F11),
    KEY_(F12),
    KEY_(RO),
    KEY_(KATAKANA),
    KEY_(HIRAGANA),
    KEY_(HENKAN),
    KEY_(KATAKANAHIRAGANA),
    KEY_(MUHENKAN),
    KEY_(KPJPCOMMA),
    KEY_(KPENTER),
    KEY_(RIGHTCTRL),
    KEY_(KPSLASH),
    KEY_(SYSRQ),
    KEY_(RIGHTALT),
    KEY_(LINEFEED),
    KEY_(HOME),
    KEY_(UP),
    KEY_(PAGEUP),
    KEY_(LEFT),
    KEY_(RIGHT),
    KEY_(END),
    KEY_(DOWN),
    KEY_(PAGEDOWN),
    KEY_(INSERT),
    KEY_(DELETE),
    KEY_(MACRO),
    KEY_(MUTE),
    KEY_(VOLUMEDOWN),
    KEY_(VOLUMEUP),
    KEY_(POWER),
    KEY_(KPEQUAL),
    KEY_(KPPLUSMINUS),
    KEY_(PAUSE),
    KEY_(SCALE),
    KEY_(KPCOMMA),
    KEY_(HANGEUL),
    KEY_(HANGUEL),
    KEY_(HANJA),
    KEY_(YEN),
    KEYS(LEFTMETA, "META"),
    KEY_(RIGHTMETA),
    KEY_(COMPOSE),
    KEY_(STOP),
    KEY_(AGAIN),
    KEY_(PROPS),
    KEY_(UNDO),
    KEY_(FRONT),
    KEY_(COPY),
    KEY_(OPEN),
    KEY_(PASTE),
    KEY_(FIND),
    KEY_(CUT),
    KEY_(HELP),
    KEY_(MENU),
    KEY_(CALC),
    KEY_(SETUP),
    KEY_(SLEEP),
    KEY_(WAKEUP),
    KEY_(FILE),
    KEY_(SENDFILE),
    KEY_(DELETEFILE),
    KEY_(XFER),
    KEY_(PROG1),
    KEY_(PROG2),
    KEY_(WWW),
    KEY_(MSDOS),
    KEY_(COFFEE),
    KEY_(SCREENLOCK),
    KEY_(ROTATE_DISPLAY),
    KEY_(DIRECTION),
    KEY_(CYCLEWINDOWS),
    KEY_(MAIL),
    KEY_(BOOKMARKS),
    KEY_(COMPUTER),
    KEY_(BACK),
    KEY_(FORWARD),
    KEY_(CLOSECD),
    KEY_(EJECTCD),
    KEY_(EJECTCLOSECD),
    KEY_(NEXTSONG),
    KEY_(PLAYPAUSE),
    KEY_(PREVIOUSSONG),
    KEY_(STOPCD),
    KEY_(RECORD),
    KEY_(REWIND),
    KEY_(PHONE),
    KEY_(ISO),
    KEY_(CONFIG),
    KEY_(HOMEPAGE),
    KEY_(REFRESH),
    KEY_(EXIT),
    KEY_(MOVE),
    KEY_(EDIT),
    KEY_(SCROLLUP),
    KEY_(SCROLLDOWN),
    KEY_(KPLEFTPAREN),
    KEY_(KPRIGHTPAREN),
    KEY_(NEW),
    KEY_(REDO),
    KEY_(F13),
    KEY_(F14),
    KEY_(F15),
    KEY_(F16),
    KEY_(F17),
    KEY_(F18),
    KEY_(F19),
    KEY_(F20),
    KEY_(F21),
    KEY_(F22),
    KEY_(F23),
    KEY_(F24),
    KEY_(PLAYCD),
    KEY_(PAUSECD),
    KEY_(PROG3),
    KEY_(PROG4),
    KEY_(DASHBOARD),
    KEY_(SUSPEND),
    KEY_(CLOSE),
    KEY_(PLAY),
    KEY_(FASTFORWARD),
    KEY_(BASSBOOST),
    KEY_(PRINT),
    KEY_(HP),
    KEY_(CAMERA),
    KEY_(SOUND),
    KEY_(QUESTION),
    KEY_(EMAIL),
    KEY_(CHAT),
    KEY_(SEARCH),
    KEY_(CONNECT),
    KEY_(FINANCE),
    KEY_(SPORT),
    KEY_(SHOP),
    KEY_(ALTERASE),
    KEY_(CANCEL),
    KEY_(BRIGHTNESSDOWN),
    KEY_(BRIGHTNESSUP),
    KEY_(MEDIA),
    KEY_(SWITCHVIDEOMODE),
    KEY_(KBDILLUMTOGGLE),
    KEY_(KBDILLUMDOWN),
    KEY_(KBDILLUMUP),
    KEY_(SEND),
    KEY_(REPLY),
    KEY_(FORWARDMAIL),
    KEY_(SAVE),
    KEY_(DOCUMENTS),
    KEY_(BATTERY),
    KEY_(BLUETOOTH),
    KEY_(WLAN),
    KEY_(UWB),
    KEY_(UNKNOWN),
    KEY_(VIDEO_NEXT),
    KEY_(VIDEO_PREV),
    KEY_(BRIGHTNESS_CYCLE),
    KEY_(BRIGHTNESS_AUTO),
    KEY_(BRIGHTNESS_ZERO),
    KEY_(DISPLAY_OFF),
    KEY_(WWAN),
    KEY_(WIMAX),
    KEY_(RFKILL),
    KEY_(MICMUTE),
    KEY_(OK),
    KEY_(SELECT),
    KEY_(GOTO),
    KEY_(CLEAR),
    KEY_(POWER2),
    KEY_(OPTION),
    KEY_(INFO),
    KEY_(TIME),
    KEY_(VENDOR),
    KEY_(ARCHIVE),
    KEY_(PROGRAM),
    KEY_(CHANNEL),
    KEY_(FAVORITES),
    KEY_(EPG),
    KEY_(PVR),
    KEY_(MHP),
    KEY_(LANGUAGE),
    KEY_(TITLE),
    KEY_(SUBTITLE),
    KEY_(ANGLE),
    KEY_(FULL_SCREEN),
    KEY_(ZOOM),
    KEY_(MODE),
    KEY_(KEYBOARD),
    KEY_(ASPECT_RATIO),
    KEY_(SCREEN),
    KEY_(PC),
    KEY_(TV),
    KEY_(TV2),
    KEY_(VCR),
    KEY_(VCR2),
    KEY_(SAT),
    KEY_(SAT2),
    KEY_(CD),
    KEY_(TAPE),
    KEY_(RADIO),
    KEY_(TUNER),
    KEY_(PLAYER),
    KEY_(TEXT),
    KEY_(DVD),
    KEY_(AUX),
    KEY_(MP3),
    KEY_(AUDIO),
    KEY_(VIDEO),
    KEY_(DIRECTORY),
    KEY_(LIST),
    KEY_(MEMO),
    KEY_(CALENDAR),
    KEY_(RED),
    KEY_(GREEN),
    KEY_(YELLOW),
    KEY_(BLUE),
    KEY_(CHANNELUP),
    KEY_(CHANNELDOWN),
    KEY_(FIRST),
    KEY_(LAST),
    KEY_(AB),
    KEY_(NEXT),
    KEY_(RESTART),
    KEY_(SLOW),
    KEY_(SHUFFLE),
    KEY_(BREAK),
    KEY_(PREVIOUS),
    KEY_(DIGITS),
    KEY_(TEEN),
    KEY_(TWEN),
    KEY_(VIDEOPHONE),
    KEY_(GAMES),
    KEY_(ZOOMIN),
    KEY_(ZOOMOUT),
    KEY_(ZOOMRESET),
    KEY_(WORDPROCESSOR),
    KEY_(EDITOR),
    KEY_(SPREADSHEET),
    KEY_(GRAPHICSEDITOR),
    KEY_(PRESENTATION),
    KEY_(DATABASE),
    KEY_(NEWS),
    KEY_(VOICEMAIL),
    KEY_(ADDRESSBOOK),
    KEY_(MESSENGER),
    KEY_(DISPLAYTOGGLE),
    KEY_(BRIGHTNESS_TOGGLE),
    KEY_(SPELLCHECK),
    KEY_(LOGOFF),
    KEY_(DOLLAR),
    KEY_(EURO),
    KEY_(FRAMEBACK),
    KEY_(FRAMEFORWARD),
    KEY_(CONTEXT_MENU),
    KEY_(MEDIA_REPEAT),
    KEY_(10CHANNELSUP),
    KEY_(10CHANNELSDOWN),
    KEY_(IMAGES),
    KEY_(NOTIFICATION_CENTER),
    KEY_(PICKUP_PHONE),
    KEY_(HANGUP_PHONE),
    KEY_(DEL_EOL),
    KEY_(DEL_EOS),
    KEY_(INS_LINE),
    KEY_(DEL_LINE),
    KEY_(FN),
    KEY_(FN_ESC),
    KEY_(FN_F1),
    KEY_(FN_F2),
    KEY_(FN_F3),
    KEY_(FN_F4),
    KEY_(FN_F5),
    KEY_(FN_F6),
    KEY_(FN_F7),
    KEY_(FN_F8),
    KEY_(FN_F9),
    KEY_(FN_F10),
    KEY_(FN_F11),
    KEY_(FN_F12),
    KEY_(FN_1),
    KEY_(FN_2),
    KEY_(FN_D),
    KEY_(FN_E),
    KEY_(FN_F),
    KEY_(FN_S),
    KEY_(FN_B),
    KEY_(FN_RIGHT_SHIFT),
    KEY_(BRL_DOT1),
    KEY_(BRL_DOT2),
    KEY_(BRL_DOT3),
    KEY_(BRL_DOT4),
    KEY_(BRL_DOT5),
    KEY_(BRL_DOT6),
    KEY_(BRL_DOT7),
    KEY_(BRL_DOT8),
    KEY_(BRL_DOT9),
    KEY_(BRL_DOT10),
    KEY_(NUMERIC_0),
    KEY_(NUMERIC_1),
    KEY_(NUMERIC_2),
    KEY_(NUMERIC_3),
    KEY_(NUMERIC_4),
    KEY_(NUMERIC_5),
    KEY_(NUMERIC_6),
    KEY_(NUMERIC_7),
    KEY_(NUMERIC_8),
    KEY_(NUMERIC_9),
    KEY_(NUMERIC_STAR),
    KEY_(NUMERIC_POUND),
    KEY_(NUMERIC_A),
    KEY_(NUMERIC_B),
    KEY_(NUMERIC_C),
    KEY_(NUMERIC_D),
    KEY_(CAMERA_FOCUS),
    KEY_(WPS_BUTTON),
    KEY_(TOUCHPAD_TOGGLE),
    KEY_(TOUCHPAD_ON),
    KEY_(TOUCHPAD_OFF),
    KEY_(CAMERA_ZOOMIN),
    KEY_(CAMERA_ZOOMOUT),
    KEY_(CAMERA_UP),
    KEY_(CAMERA_DOWN),
    KEY_(CAMERA_LEFT),
    KEY_(CAMERA_RIGHT),
    KEY_(ATTENDANT_ON),
    KEY_(ATTENDANT_OFF),
    KEY_(ATTENDANT_TOGGLE),
    KEY_(LIGHTS_TOGGLE),
    KEY_(ALS_TOGGLE),
    KEY_(ROTATE_LOCK_TOGGLE),

    {NULL, NULL, 0}
};

#define BTN(name) {#name, NULL, BTN_##name}
static const struct key buttons[] = {
    BTN(MISC),
    BTN(0),
    BTN(1),
    BTN(2),
    BTN(3),
    BTN(4),
    BTN(5),
    BTN(6),
    BTN(7),
    BTN(8),
    BTN(9),
    BTN(MOUSE),
    BTN(LEFT),
    BTN(RIGHT),
    BTN(MIDDLE),
    BTN(SIDE),
    BTN(EXTRA),
    BTN(FORWARD),
    BTN(BACK),
    BTN(TASK),
    BTN(JOYSTICK),
    BTN(TRIGGER),
    BTN(THUMB),
    BTN(THUMB2),
    BTN(TOP),
    BTN(TOP2),
    BTN(PINKIE),
    BTN(BASE),
    BTN(BASE2),
    BTN(BASE3),
    BTN(BASE4),
    BTN(BASE5),
    BTN(BASE6),
    BTN(DEAD),
    BTN(GAMEPAD),
    BTN(SOUTH),
    BTN(A),
    BTN(EAST),
    BTN(B),
    BTN(C),
    BTN(NORTH),
    BTN(X),
    BTN(WEST),
    BTN(Y),
    BTN(Z),
    BTN(TL),
    BTN(TR),
    BTN(TL2),
    BTN(TR2),
    BTN(SELECT),
    BTN(START),
    BTN(MODE),
    BTN(THUMBL),
    BTN(THUMBR),
    BTN(DIGI),
    BTN(TOOL_PEN),
    BTN(TOOL_RUBBER),
    BTN(TOOL_BRUSH),
    BTN(TOOL_PENCIL),
    BTN(TOOL_AIRBRUSH),
    BTN(TOOL_FINGER),
    BTN(TOOL_MOUSE),
    BTN(TOOL_LENS),
    BTN(TOOL_QUINTTAP),
    BTN(STYLUS3),
    BTN(TOUCH),
    BTN(STYLUS),
    BTN(STYLUS2),
    BTN(TOOL_DOUBLETAP),
    BTN(TOOL_TRIPLETAP),
    BTN(TOOL_QUADTAP),
    BTN(WHEEL),
    BTN(GEAR_DOWN),
    BTN(GEAR_UP),
    BTN(DPAD_UP),
    BTN(DPAD_DOWN),
    BTN(DPAD_LEFT),
    BTN(DPAD_RIGHT),

    {NULL, NULL, 0}
};

/**
 * Output the key list to console.
 * */
void outputKeyList()
{
        for (int i = 0; keys[i].name != NULL; i++)
        {
            if (keys[i].symbol != NULL)
            {
                printf("% 4d:  KEY_%s    %s    KEY_%s    %s\n", keys[i].code, keys[i].name, keys[i].name, keys[i].symbol, keys[i].symbol);
            }
            else
            {
                printf("% 4d:  KEY_%s    %s\n", keys[i].code, keys[i].name, keys[i].name);
            }
        }

        for (int i = 0; buttons[i].name != NULL; i++)
        {
            printf("% 4d:  BTN_%s\n", buttons[i].code, buttons[i].name);
        }
}

/**
 * Converts a key string (e.g. "KEY_I") to its corresponding code.
 * Buttons only support BTN_{name}.
 * Keys support several forms: KEY_{name}, {name}, KEY_{symbol}, or {symbol}.
 * */
int convertKeyStringToCode(char* keyString)
{
    if (keyString == NULL) return 0;

    if (strncmp(keyString, "BTN_", 4) == 0)
    {
        keyString += 4;
        for (int i = 0; buttons[i].name != NULL; i++)
        {
            if (strcmp(keyString, buttons[i].name) == 0) return buttons[i].code;
        }
    }
    else
    {
        if (strncmp(keyString, "KEY_", 4) == 0)
        {
            keyString += 4;
        }
        for (int i = 0; keys[i].name != NULL; i++)
        {
            if (keys[i].symbol != NULL && strcmp(keyString, keys[i].symbol) == 0) return keys[i].code;
            if (strcmp(keyString, keys[i].name) == 0) return keys[i].code;
        }
    }

    return 0;
}

/**
 * Checks if the event is key down.
 * Linux input sends value=2 for repeated key down.
 * We treat them as keydown events for processing.
 * */
int isDown(int value)
{
    return value == 1 || value == 2;
}

/**
 * Checks if the key is a keypad key.
 * */
int isKeypad(int code)
{
    switch (code)
    {
        case KEY_KPASTERISK:
        case KEY_KP7:
        case KEY_KP8:
        case KEY_KP9:
        case KEY_KPMINUS:
        case KEY_KP4:
        case KEY_KP5:
        case KEY_KP6:
        case KEY_KPPLUS:
        case KEY_KP1:
        case KEY_KP2:
        case KEY_KP3:
        case KEY_KP0:
        case KEY_KPDOT:
        {
            return 1;
        }
        default:
        {
            return 0;
        }
    }
}

/**
 * Checks if the key is a modifier key.
 * */
int isModifier(int code)
{
    switch (code)
    {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
        case KEY_LEFTALT:
        case KEY_RIGHTALT:
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
        case KEY_CAPSLOCK:
        case KEY_NUMLOCK:
        case KEY_SCROLLLOCK:
        {
            return 1;
        }
        default:
        {
            return 0;
        }
    }
}
