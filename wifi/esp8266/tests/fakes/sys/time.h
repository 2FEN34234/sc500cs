#pragma once
#include <time.h>
#ifndef _TIMEVAL_DEFINED
struct timeval { time_t tv_sec; long tv_usec; };
#endif
inline int settimeofday(const timeval*,const void*) { return 0; }
