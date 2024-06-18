#ifndef pointer_h
#define pointer_h

#define pointer_move(x, y) emit_pointer_relative(EV_REL, REL_X, x, REL_Y, y)
#define pointer_scroll(x, y) emit_pointer_relative(EV_REL, REL_HWHEEL, x, REL_WHEEL, y)

/**
 * Emits a relative pointer event.
 * */
void emit_pointer_relative(int type, int code_x, int value_x, int code_y, int value_y);

#endif
