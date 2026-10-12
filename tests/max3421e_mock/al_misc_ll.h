#ifndef MOCK_AL_MISC_LL_H
#define MOCK_AL_MISC_LL_H

#include <stdint.h>

typedef void AL_VOID;
typedef uint32_t AL_U32;
typedef int AL_BOOL;
typedef enum { AL_MISC_QSPI1 = 5 } AL_MISC_ModulesEnum;

static inline AL_VOID AlMisc_ll_SetClkEn(AL_MISC_ModulesEnum module,
                                         AL_BOOL enabled)
{
    (void)module;
    (void)enabled;
}

static inline AL_VOID AlMisc_ll_SetReset(AL_MISC_ModulesEnum module,
                                         AL_BOOL asserted)
{
    (void)module;
    (void)asserted;
}

#endif
