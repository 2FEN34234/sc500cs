#pragma once
inline int os_get_random(unsigned char* p,size_t n){static unsigned next=1;while(n--) *p++=static_cast<unsigned char>(next++);return 0;}
