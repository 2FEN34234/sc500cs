#ifndef MENU_INPUT_H
#define MENU_INPUT_H
#include <stdint.h>

/* MCU input/state layer. Geometry matches the 1920x1080 ui_osd.v layout.
 * Both firmware targets include this file; no extra FPGA logic is required. */
enum { TOUCH_DOWN = 1, TOUCH_MOVE, TOUCH_UP, TOUCH_CANCEL };
typedef struct { uint16_t x, y; uint8_t type; } TouchEvent;
typedef struct { uint8_t down; } TouchDecoder;

/* This panel's verified report starts with ID 0x0d and puts the first contact
 * at bytes 1..5. Accept a longer report too, so firmware updates that append
 * contacts/buttons do not disable the existing menu controls. */
static inline int touch_decode(TouchDecoder *d, const uint8_t *r,
                               unsigned length, TouchEvent *e)
{
    unsigned x, y, down;
    if(length < 6u || r[0] != 0x0du) return 0;
    x = r[2] | ((unsigned)r[3] << 8);
    y = r[4] | ((unsigned)r[5] << 8);
    down = r[1] & 1u;
    if(x > 16383u || y > 9599u) {
        if(!d->down) return 0;
        d->down = 0;
        e->type = TOUCH_CANCEL; e->x = e->y = 0;
        return 1;
    }
    if(!down && !d->down) return 0;
    e->type = down ? (d->down ? TOUCH_MOVE : TOUCH_DOWN) : TOUCH_UP;
    e->x = (uint16_t)((x * 1919u + 8191u) / 16383u);
    e->y = (uint16_t)((y * 1079u + 4799u) / 9599u);
    d->down = (uint8_t)down;
    return 1;
}

typedef struct {
    uint32_t menu, edit, info, index;
    uint32_t exposure, gain, servo_pan_us, servo_tilt_us;
    uint32_t servo_menu_available;
    uint32_t image_step, image_effect, image_target, logo_enable;
    int touch_capture; /* -1: none, 0..3: slider, 4: toggle, 5: AF, 6: row */
} MenuState;
enum { MENU_AF = 1, MENU_FOCUS_UP = 2, MENU_FOCUS_DOWN = 4,
       MENU_REACQUIRE = 8 };

static inline uint32_t menu_step(uint32_t v, int direction,
                               uint32_t low, uint32_t high, uint32_t step)
{
    if(v < low) v = low;
    if(v > high) v = high;
    if(direction > 0) return high - v < step ? high : v + step;
    return v - low < step ? low : v - step;
}

/* One adjustment implementation for touch actions and physical keys. */
static inline unsigned menu_adjust(MenuState *s, int direction)
{
    switch(s->index) {
    case 0: s->exposure = menu_step(s->exposure, direction, 3, 5990, 120); break;
    case 1: s->gain = menu_step(s->gain, direction, 16, 143, 8); break;
    case 2: if(s->servo_menu_available)
                s->servo_pan_us = menu_step(s->servo_pan_us, direction, 1000, 2000, 25);
            break;
    case 3: if(s->servo_menu_available)
                s->servo_tilt_us = menu_step(s->servo_tilt_us, direction, 1000, 2000, 25);
            break;
    case 4: s->image_step = direction > 0 ? (s->image_step >= 5 ? 1 : s->image_step + 1)
                                        : (s->image_step <= 1 ? 5 : s->image_step - 1); break;
    case 5: if(s->image_step == 5) return MENU_REACQUIRE;
            if(s->image_step == 4)
                s->image_target = direction > 0 ? (s->image_target >= 2 ? 0 : s->image_target + 1)
                                                : (s->image_target == 0 ? 2 : s->image_target - 1);
            else if(s->image_step <= 2)
                s->image_effect = direction > 0 ? (s->image_effect >= 5 ? 0 : s->image_effect + 1)
                                                : (s->image_effect == 0 ? 5 : s->image_effect - 1);
            break;
    case 6: s->logo_enable = !s->logo_enable; break;
    }
    return 0;
}

static inline unsigned menu_key(MenuState *s, unsigned keys)
{
    /* Physical keys cancel a drag so old touch movement cannot overwrite it. */
    s->touch_capture = -1;
    if(!s->menu) {
        if(keys & 4) return MENU_AF;
        if(keys & 1) return MENU_FOCUS_UP;
        if(keys & 2) return MENU_FOCUS_DOWN;
        if(keys & 8) { s->menu = 1; s->index = s->edit = s->info = 0; }
    } else if(s->edit) {
        if(keys & 12) s->edit = 0;
        else if(keys & 1) return menu_adjust(s, 1);
        else if(keys & 2) return menu_adjust(s, -1);
    } else {
        if(keys & 4) { s->menu = 0; s->info = 0; }
        else if(keys & 1) s->index = s->index == 0 ? 6 : s->index - 1;
        else if(keys & 2) s->index = s->index == 6 ? 0 : s->index + 1;
        else if(keys & 8) { s->edit = 1; s->info = 0; }
    }
    return 0;
}

static inline void menu_slider(MenuState *s, unsigned row, unsigned x)
{
    unsigned p = x <= 330 ? 0 : x >= 457 ? 127 : x - 330;
    if(row == 0) s->exposure = 3 + (p * (5990 - 3) + 63) / 127;
    if(row == 1) s->gain = 16 + p;
    /* Keep the same safe range and 25 us steps as the physical buttons. */
    if(s->servo_menu_available) {
        unsigned value = 1000 + ((p * 40 + 63) / 127) * 25;
        if(row == 2) s->servo_pan_us = value;
        if(row == 3) s->servo_tilt_us = value;
    }
}

static inline unsigned menu_touch(MenuState *s, const TouchEvent *e)
{
    unsigned x = e->x, y = e->y, row;
    if(e->type == TOUCH_CANCEL || e->type == TOUCH_UP) {
        s->touch_capture = -1;
        return 0;
    }
    if(e->type == TOUCH_MOVE) {
        if(s->menu && s->touch_capture >= 0 && s->touch_capture < 4) {
            row = (unsigned)s->touch_capture;
            /* Ignore excursions into other rows; never transfer a drag. */
            if(y >= 728 + row * 36 && y < 764 + row * 36)
                menu_slider(s, row, x);
        }
        return 0;
    }
    if(e->type != TOUCH_DOWN) return 0;
    s->touch_capture = -1;
    if(x < 240 && y >= 1008 && y < 1080) {
        s->menu = !s->menu; s->edit = s->info = 0;
        s->touch_capture = 4;
        return 0;
    }
    /* Visible menu gets first hit-test priority over the picture. */
    if(s->menu && x >= 24 && x < 492 && y >= 680 && y < 1004) {
        s->touch_capture = 6;
        if(y < 728 || y >= 980) return 0;
        row = (y - 728) / 36;
        s->index = row; s->edit = 1; s->info = 0;
        if(row < 4 && x >= 306) {
            s->touch_capture = (int)row;
            menu_slider(s, row, x);
        } else if(row >= 4) return menu_adjust(s, 1);
        return 0;
    }
    if(x >= 760 && x < 1160 && y >= 390 && y < 690) {
        s->touch_capture = 5;
        return MENU_AF;
    }
    return 0;
}
#endif
