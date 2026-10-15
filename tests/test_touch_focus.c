#include <assert.h>
#include <stdio.h>
#include "../FDwork/shared/touch_focus.h"
static TouchFocus g;
static MenuState s;
static unsigned event(unsigned type,unsigned x,unsigned y,unsigned ms) {
    TouchEvent e={(uint16_t)x,(uint16_t)y,(uint8_t)type};
    unsigned a=touch_focus(&g,&e,ms,touch_in_menu_area(&s,&e));
    assert(!(menu_touch(&s,&e)&MENU_AF));
    if(a&MENU_AF) s.servo_touch_active=0; else touch_servo_apply(&s,&e);
    return a;
}
static void reset(void) {
    g=(TouchFocus){0};s=(MenuState){.touch_capture=-1,.servo_pan_us=1500,.servo_tilt_us=1500};
}
int main(void) {
    const unsigned xy[][2]={{0,0},{1919,0},{1919,1079},{960,540},{600,1079},{0,700}};
    for(unsigned i=0;i<6;i++) {
        unsigned x=xy[i][0],y=xy[i][1]; reset();
        assert(!event(TOUCH_DOWN,x,y,100)); assert(!event(TOUCH_UP,x,y,180));
        assert(!event(TOUCH_DOWN,x,y,300)); assert(event(TOUCH_UP,x,y,380)==MENU_AF);
        assert(s.servo_pan_us==1500 && s.servo_tilt_us==1500);
        assert(!event(TOUCH_UP,x,y,400));
        assert(!event(TOUCH_DOWN,x,y,450));assert(!event(TOUCH_UP,x,y,500));
        unsigned roi=touch_focus_roi(x,y);
        assert((roi&2047)<=1760 && ((roi>>16)&2047)<=960 && roi>>31);
    }
    assert(touch_focus_roi(960,540)==0x81e00370u);
    assert(touch_focus_roi(0,0)==0x80000000u);
    assert(touch_focus_roi(1919,1079)==(0x80000000u|(960u<<16)|1760u));
    reset();event(TOUCH_DOWN,900,500,100);event(TOUCH_UP,908,504,150);
    event(TOUCH_DOWN,902,502,200);assert(event(TOUCH_UP,903,501,250)==MENU_AF);
    assert(s.servo_pan_us==1500 && s.servo_tilt_us==1500);
    reset();event(TOUCH_DOWN,900,500,100);event(TOUCH_UP,900,500,150);
    event(TOUCH_DOWN,900,500,600);assert(!event(TOUCH_UP,900,500,650)); // expired
    reset();event(TOUCH_DOWN,900,500,100);event(TOUCH_UP,900,500,150);
    event(TOUCH_DOWN,1200,500,200);assert(!event(TOUCH_UP,1200,500,250)); // far apart
    reset();event(TOUCH_DOWN,900,500,100);event(TOUCH_UP,900,500,150);
    event(TOUCH_DOWN,900,500,200);event(TOUCH_MOVE,1000,500,220);
    event(TOUCH_MOVE,900,500,240);assert(!event(TOUCH_UP,900,500,260)); // out and back
    event(TOUCH_DOWN,900,500,300);assert(!event(TOUCH_UP,900,500,350));
    reset();event(TOUCH_DOWN,900,500,100);assert(!event(TOUCH_UP,950,500,150)); // UP-only move
    assert(s.servo_pan_us==1550);
    event(TOUCH_DOWN,950,500,200);assert(!event(TOUCH_UP,950,500,250));
    reset();event(TOUCH_DOWN,900,500,100);assert(!event(TOUCH_UP,900,500,400)); // hold
    event(TOUCH_DOWN,900,500,450);assert(!event(TOUCH_UP,900,500,500));
    reset();event(TOUCH_DOWN,900,500,100);event(TOUCH_UP,900,500,150);
    event(TOUCH_CANCEL,0,0,180);event(TOUCH_DOWN,900,500,200);assert(!event(TOUCH_UP,900,500,250));
    reset();s.menu=1;
    event(TOUCH_DOWN,450,700,100);event(TOUCH_UP,450,700,150);
    event(TOUCH_DOWN,450,700,200);assert(!event(TOUCH_UP,450,700,250));
    assert(!s.servo_touch_active);
    reset();event(TOUCH_DOWN,900,500,0xffffff80u);event(TOUCH_UP,900,500,0xffffffc0u);
    event(TOUCH_DOWN,900,500,20);assert(event(TOUCH_UP,900,500,80)==MENU_AF);
    puts("PASS double tap: panel coverage, single/hold/drag/cancel rejection, menu ownership, timer wrap, ROI bounds");
}
