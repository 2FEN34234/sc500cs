#ifndef WIFI_CALENDAR_H
#define WIFI_CALENDAR_H
#include <stdint.h>
static inline unsigned wf_month_days(unsigned y,unsigned m) {
    static const uint8_t d[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    return d[m-1]+(m==2 && y%4==0);
}
/* DS3231 range, including explicit local UTC offset. No libc time/float. */
static inline int wf_epoch_calendar(uint64_t epoch,int offset,unsigned v[6]) {
    if(epoch>4102444799ULL || offset< -720 || offset>840) return 0;
    int64_t local=(int64_t)epoch+(int64_t)offset*60;
    if(local<946684800LL || local>=4102444800LL) return 0;
    uint32_t seconds=(uint32_t)local-946684800u,days=seconds/86400u;
    v[0]=2000;while(days>=365u+(v[0]%4==0)) {days-=365+(v[0]%4==0);v[0]++;}
    v[1]=1;while(days>=wf_month_days(v[0],v[1])) {days-=wf_month_days(v[0],v[1]);v[1]++;}
    v[2]=days+1;seconds%=86400;v[3]=seconds/3600;v[4]=(seconds/60)%60;v[5]=seconds%60;return 1;
}
static inline uint32_t wf_fat_epoch(uint32_t stamp) {
    unsigned y=(stamp>>25)+1980,m=(stamp>>21)&15,d=(stamp>>16)&31;
    if(y<2000 || y>2099 || !m || m>12 || !d || d>wf_month_days(y,m)) return 0;
    unsigned h=(stamp>>11)&31,minute=(stamp>>5)&63,sec=(stamp&31)*2;
    if(h>23 || minute>59 || sec>59) return 0;
    uint32_t days=(y-2000)*365+(y-2000+3)/4+d-1;
    for(unsigned i=1;i<m;i++) days+=wf_month_days(y,i);
    uint32_t epoch=946684800u+days*86400u+h*3600u+minute*60u+sec-480u*60u;
    return epoch<946684800u?0:epoch;
}
#endif
