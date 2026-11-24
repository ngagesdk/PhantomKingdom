/* SPDX-License-Identifier: MIT */
#include "input/pk_input.h"
#include "input/pk_haptics.h"

#define REPEAT_DELAY 4   /* logic frames (90 ms each) */
#define TOUCH_SLOP   5   /* game pixels a tap may move */
#define LONG_PRESS   550 /* ms: a press held this long without moving = back */
#define REPEAT_RATE  1

static struct
{
    Uint32 held;
    Uint32 latched;
    Uint32 pressed;
    int hold_frames[PK_KEY_COUNT];
    SDL_Gamepad *pad;
} I;

/* ---- touch and mouse ---- */
static struct
{
    bool down;  /* the tracked pointer */
    bool mouse; /* it is the mouse (otherwise a finger) */
    SDL_FingerID finger;
    float x0, y0, x, y; /* start and current position (game pixels) */
    float ax, ay;       /* movement not yet turned into steps */
    Uint64 t0;
    bool dragging, axis_y, long_fired;
    int fingers; /* fingers on the screen */
    bool multi;  /* a second finger came: a two-finger tap */
    /* latched since the last frame / published for this frame */
    bool tap_l, tap;
    int tx_l, ty_l, tx, ty;
    int sy_l, sx_l, sy, sx;
} P;

static bool s_focus_lost; /* latched until pk_input_focus_lost() */
static bool s_touch;      /* the last pointer was a finger */

bool pk_input_focus_lost(void)
{
    bool r = s_focus_lost;
    s_focus_lost = false;
    return r;
}

static int map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP:
    case SDLK_W:
        return PK_KEY_UP;
    case SDLK_DOWN:
    case SDLK_S:
        return PK_KEY_DOWN;
    case SDLK_LEFT:
    case SDLK_A:
        return PK_KEY_LEFT;
    case SDLK_RIGHT:
    case SDLK_D:
        return PK_KEY_RIGHT;
    case SDLK_RETURN:
    case SDLK_SPACE:
    case SDLK_KP_ENTER:
    case SDLK_KP_5:
        return PK_KEY_SELECT;
    case SDLK_F1:
    case SDLK_Q:
        return PK_KEY_LSK;
    case SDLK_F2:
    case SDLK_E:
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        return PK_KEY_RSK;
    case SDLK_BACKSPACE:
    case SDLK_DELETE:
        return PK_KEY_CLEAR;
    case SDLK_0:
    case SDLK_KP_0:
        return PK_KEY_0;
    case SDLK_1:
    case SDLK_KP_1:
        return PK_KEY_1;
    case SDLK_2:
    case SDLK_KP_2:
        return PK_KEY_2;
    case SDLK_3:
    case SDLK_KP_3:
        return PK_KEY_3;
    case SDLK_4:
    case SDLK_KP_4:
        return PK_KEY_4;
    case SDLK_5:
        return PK_KEY_5;
    case SDLK_6:
    case SDLK_KP_6:
        return PK_KEY_6;
    case SDLK_7:
    case SDLK_KP_7:
        return PK_KEY_7;
    case SDLK_8:
    case SDLK_KP_8:
        return PK_KEY_8;
    case SDLK_9:
    case SDLK_KP_9:
        return PK_KEY_9;
    case SDLK_ASTERISK:
    case SDLK_KP_MULTIPLY:
        return PK_KEY_STAR;
    case SDLK_HASH:
    case SDLK_KP_HASH:
        return PK_KEY_HASH;
    default:
        return -1;
    }
}

static int map_button(int b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return PK_KEY_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return PK_KEY_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return PK_KEY_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return PK_KEY_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return PK_KEY_SELECT;
    case SDL_GAMEPAD_BUTTON_EAST:
        return PK_KEY_RSK;
    case SDL_GAMEPAD_BUTTON_WEST:
    case SDL_GAMEPAD_BUTTON_START:
        return PK_KEY_LSK;
    case SDL_GAMEPAD_BUTTON_BACK:
        return PK_KEY_CLEAR;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return PK_KEY_5;
    default:
        return -1;
    }
}

void pk_input_init(void)
{
    SDL_zero(I);
    SDL_zero(P);
}

void pk_input_quit(void)
{
    if (I.pad) {
        SDL_CloseGamepad(I.pad);
    }
    SDL_zero(I);
    SDL_zero(P);
}

void pk_input_inject(pk_key k, bool down)
{
    Uint32 bit = 1u << k;
    if (k >= PK_KEY_COUNT) {
        return;
    }
    if (down) {
        if (!(I.held & bit)) {
            I.latched |= bit;
        }
        I.held |= bit;
    } else {
        I.held &= ~bit;
    }
}

static void press_back(void)
{
    pk_input_inject(PK_KEY_RSK, true);
    pk_input_inject(PK_KEY_RSK, false);
}

static void pointer_down(bool mouse, SDL_FingerID f, float x, float y)
{
    P.down = true;
    P.mouse = mouse;
    s_touch = !mouse;
    P.finger = f;
    P.x0 = P.x = x;
    P.y0 = P.y = y;
    P.ax = P.ay = 0;
    P.t0 = SDL_GetTicks();
    P.dragging = P.long_fired = false;
}

static void pointer_move(float x, float y)
{
    float dx = x - P.x, dy = y - P.y;
    P.x = x;
    P.y = y;
    if (!P.dragging) {
        float mx = x - P.x0, my = y - P.y0;
        if (mx * mx + my * my < TOUCH_SLOP * TOUCH_SLOP) {
            return;
        }
        P.dragging = true;
        P.axis_y = SDL_fabsf(my) >= SDL_fabsf(mx);
        dx = mx;
        dy = my;
    }
    if (P.axis_y) {
        P.ay -= dy; /* the finger goes up: the list goes on */
        while (P.ay >= PK_TOUCH_STEP_Y) {
            P.ay -= PK_TOUCH_STEP_Y;
            P.sy_l++;
        }
        while (P.ay <= -PK_TOUCH_STEP_Y) {
            P.ay += PK_TOUCH_STEP_Y;
            P.sy_l--;
        }
    } else {
        P.ax -= dx;
        while (P.ax >= PK_TOUCH_STEP_X) {
            P.ax -= PK_TOUCH_STEP_X;
            P.sx_l++;
        }
        while (P.ax <= -PK_TOUCH_STEP_X) {
            P.ax += PK_TOUCH_STEP_X;
            P.sx_l--;
        }
    }
}

static void pointer_up(void)
{
    if (P.down && !P.dragging && !P.long_fired && !P.multi) {
        P.tap_l = true;
        P.tx_l = (int)P.x0;
        P.ty_l = (int)P.y0;
    }
    P.down = false;
}

static void touch_poll(void)
{
    /* a long press without moving is the back key */
    if (P.down && !P.dragging && !P.long_fired && !P.multi && SDL_GetTicks() - P.t0 >= LONG_PRESS) {
        P.long_fired = true;
        press_back();
        pk_haptics_play(PK_HAPTIC_BACK); /* felt while the finger is still down */
    }
}

bool pk_input_tap(int *x, int *y)
{
    if (!P.tap) {
        return false;
    }
    P.tap = false;
    if (x) {
        *x = P.tx;
    }
    if (y) {
        *y = P.ty;
    }
    return true;
}

int pk_input_scroll_y(void) { return P.sy; }
int pk_input_scroll_x(void) { return P.sx; }
bool pk_input_pointer_down(void) { return P.down; }
bool pk_input_pointer_is_touch(void) { return s_touch; }

void pk_input_test_touch(int x0, int y0, int x1, int y1, int ms)
{
    pointer_down(true, 0, (float)x0, (float)y0);
    P.t0 = SDL_GetTicks() - (Uint64)ms;
    if (x1 != x0 || y1 != y0) {
        int i;
        for (i = 1; i <= 8; i++) {
            pointer_move(x0 + (x1 - x0) * i / 8.0f, y0 + (y1 - y0) * i / 8.0f);
        }
    }
    touch_poll();
    pointer_up();
}

/* port: keyboard text entry (the phone's text editor for chat) */
static struct
{
    bool on;
    char buf[80];
    int max;
    bool digits;
    char edit[80]; /* SDL_EVENT_TEXT_EDITING: not yet committed */
} T;

/* the window for the on-screen keyboard: the keyboard focus, or (Android:
 * no focus before the first key event) the game's only window */
static SDL_Window *text_window(void)
{
    SDL_Window *w = SDL_GetKeyboardFocus();
    if (!w) {
        int n = 0;
        SDL_Window **all = SDL_GetWindows(&n);
        if (all && n > 0) {
            w = all[0];
        }
        SDL_free(all);
    }
    return w;
}

static void text_start(SDL_Window *w)
{
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetNumberProperty(p, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                          T.digits ? SDL_TEXTINPUT_TYPE_NUMBER : SDL_TEXTINPUT_TYPE_TEXT);
    /* letters as they are typed: no suggestions or auto-correction, which
     * keep Android keyboards composing (SDL_EVENT_TEXT_EDITING) until a
     * word is committed */
    SDL_SetBooleanProperty(p, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
    if (!T.digits) {
        /* TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_VISIBLE_PASSWORD |
         * TYPE_TEXT_FLAG_NO_SUGGESTIONS: the keyboard commits each key */
        SDL_SetNumberProperty(p, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 0x00000001 | 0x00000090 | 0x00080000);
    }
    SDL_StartTextInputWithProperties(w, p);
    SDL_DestroyProperties(p);
}

void pk_input_text_begin(int max_chars)
{
    SDL_Window *w = text_window();
    T.on = true;
    T.buf[0] = 0;
    T.edit[0] = 0;
    T.max = max_chars < (int)sizeof(T.buf) - 1 ? max_chars : (int)sizeof(T.buf) - 1;
    T.digits = false;
    if (w) {
        text_start(w);
    }
}

void pk_input_text_begin_number(int max_digits)
{
    SDL_Window *w = text_window();
    T.on = true;
    T.buf[0] = 0;
    T.edit[0] = 0;
    T.max = max_digits < (int)sizeof(T.buf) - 1 ? max_digits : (int)sizeof(T.buf) - 1;
    T.digits = true;
    if (w) {
        text_start(w);
    }
}

void pk_input_text_end(void)
{
    SDL_Window *w = text_window();
    if (T.edit[0]) { /* keep a word still being composed */
        char word[sizeof(T.edit)];
        SDL_strlcpy(word, T.edit, sizeof(word));
        T.edit[0] = 0;
        pk_input_text_append(word);
    }
    T.on = false;
    if (w) {
        SDL_StopTextInput(w);
    }
}

bool pk_input_text_active(void) { return T.on; }

void pk_input_text_show(void)
{
    SDL_Window *w = text_window();
    if (T.on && w && !SDL_ScreenKeyboardShown(w)) {
        text_start(w);
    }
}
/* the typed text, with a word still being composed on the keyboard */
const char *pk_input_text(void)
{
    static char shown[160];
    if (!T.edit[0]) {
        return T.buf;
    }
    SDL_strlcpy(shown, T.buf, sizeof(shown));
    SDL_strlcat(shown, T.edit, sizeof(shown));
    return shown;
}

void pk_input_text_append(const char *utf8)
{
    size_t n = SDL_strlen(T.buf);
    for (; *utf8 && (int)n < T.max; utf8++) {
        unsigned char c = (unsigned char)*utf8;
        if (c < 0x20 || c > 0x7e) {
            continue; /* the message font is 8-bit ASCII */
        }
        if (T.digits && (c < '0' || c > '9')) {
            continue;
        }
        T.buf[n++] = (char)c;
    }
    T.buf[n] = 0;
}

void pk_input_event(const SDL_Event *e)
{
    int k = -1;
    if (T.on && e->type == SDL_EVENT_TEXT_INPUT) {
        T.edit[0] = 0;
        pk_input_text_append(e->text.text);
        return;
    }
    if (T.on && e->type == SDL_EVENT_TEXT_EDITING) {
        size_t n = SDL_strlen(T.buf), j = 0;
        const char *c;
        for (c = e->edit.text ? e->edit.text : ""; *c && j + 1 < sizeof(T.edit) && (int)(n + j) < T.max; c++) {
            if ((unsigned char)*c >= 0x20 && (unsigned char)*c <= 0x7e) {
                T.edit[j++] = *c;
            }
        }
        T.edit[j] = 0;
        return;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (T.on && e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_BACKSPACE) {
            size_t n = SDL_strlen(T.buf);
            if (n) {
                T.buf[n - 1] = 0;
            }
            return;
        }
        if (e->key.repeat) {
            return;
        }
        if (T.on && e->key.key != SDLK_RETURN && e->key.key != SDLK_KP_ENTER && e->key.key != SDLK_ESCAPE &&
            e->key.key != SDLK_UP && e->key.key != SDLK_DOWN && e->key.key != SDLK_F1 && e->key.key != SDLK_F2) {
            return; /* typing */
        }
        k = map_key(e->key.key);
        if (k >= 0) {
            pk_input_inject((pk_key)k, e->type == SDL_EVENT_KEY_DOWN);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.which == SDL_TOUCH_MOUSEID) {
            break;
        }
        if (e->button.button == SDL_BUTTON_LEFT && !P.down) {
            pointer_down(true, 0, e->button.x, e->button.y);
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            s_touch = false;
            press_back();
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (e->motion.which != SDL_TOUCH_MOUSEID && P.down && P.mouse) {
            pointer_move(e->motion.x, e->motion.y);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.which != SDL_TOUCH_MOUSEID && e->button.button == SDL_BUTTON_LEFT && P.down && P.mouse) {
            pointer_move(e->button.x, e->button.y);
            pointer_up();
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        int n = (int)SDL_roundf(e->wheel.y * (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f));
        P.sy_l -= n; /* wheel up: the list goes back */
        break;
    }
    case SDL_EVENT_FINGER_DOWN:
        P.fingers++;
        if (P.fingers == 1 && !P.down) {
            P.multi = false;
            pointer_down(false, e->tfinger.fingerID, e->tfinger.x, e->tfinger.y);
        } else if (P.fingers == 2 && P.down && !P.dragging) {
            P.multi = true; /* a two-finger tap: back, when they lift */
        }
        break;
    case SDL_EVENT_FINGER_MOTION:
        if (P.down && !P.mouse && e->tfinger.fingerID == P.finger && !P.multi) {
            pointer_move(e->tfinger.x, e->tfinger.y);
        }
        break;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (P.fingers > 0) {
            P.fingers--;
        }
        if (P.down && !P.mouse && e->tfinger.fingerID == P.finger) {
            if (e->type == SDL_EVENT_FINGER_CANCELED) {
                P.dragging = true; /* no tap */
            } else if (!P.multi) {
                pointer_move(e->tfinger.x, e->tfinger.y);
            }
            pointer_up();
        }
        if (P.fingers == 0 && P.multi) {
            P.multi = false;
            if (e->type == SDL_EVENT_FINGER_UP) {
                press_back();
                pk_haptics_play(PK_HAPTIC_BACK);
            }
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!I.pad) {
            I.pad = SDL_OpenGamepad(e->gdevice.which);
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (I.pad && SDL_GetGamepadID(I.pad) == e->gdevice.which) {
            SDL_CloseGamepad(I.pad);
            I.pad = NULL;
            I.held = 0;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        k = map_button(e->gbutton.button);
        if (k >= 0) {
            pk_input_inject((pk_key)k, e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        int v = e->gaxis.value;
        if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            pk_input_inject(PK_KEY_LEFT, v < -16000);
            pk_input_inject(PK_KEY_RIGHT, v > 16000);
        } else if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            pk_input_inject(PK_KEY_UP, v < -16000);
            pk_input_inject(PK_KEY_DOWN, v > 16000);
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        s_focus_lost = true;
        I.held = 0;
        P.down = false;
        P.fingers = 0;
        P.multi = false;
        break;
    default:
        break;
    }
}

void pk_input_frame(void)
{
    int k;
    touch_poll();
    P.tap = P.tap_l;
    P.tx = P.tx_l;
    P.ty = P.ty_l;
    P.sy = P.sy_l;
    P.sx = P.sx_l;
    P.tap_l = false;
    P.sy_l = P.sx_l = 0;
    I.pressed = I.latched;
    I.latched = 0;
    for (k = 0; k < PK_KEY_COUNT; k++) {
        if (I.held & (1u << k)) {
            I.hold_frames[k]++;
        } else {
            I.hold_frames[k] = 0;
        }
    }
}

bool pk_input_held(pk_key k) { return (I.held >> k) & 1; }
bool pk_input_pressed(pk_key k) { return (I.pressed >> k) & 1; }
Uint32 pk_input_pressed_mask(void) { return I.pressed; }
Uint32 pk_input_held_mask(void) { return I.held; }

bool pk_input_repeat(pk_key k)
{
    int f = I.hold_frames[k];
    if (pk_input_pressed(k)) {
        return true;
    }
    return f > REPEAT_DELAY && ((f - REPEAT_DELAY) % REPEAT_RATE) == 0;
}
