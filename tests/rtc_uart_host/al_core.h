#ifndef RTC_TEST_CORE_H
#define RTC_TEST_CORE_H
#include <stdint.h>
typedef uintptr_t AL_REG;
typedef void AL_VOID;
typedef unsigned AL_U32;
typedef unsigned char AL_U8;
typedef unsigned short AL_U16;
typedef int AL_BOOL;
#define AL_TRUE 1
#define AL_FALSE 0
#define AL_NULL ((void *)0)
#define USART1_BASE_ADDR 0
#define SOC_USART1_IRQn 1
static unsigned uart_read(unsigned addr);
static void uart_write(unsigned addr,unsigned value);
#define AL_REG32_READ(a) uart_read(a)
#define AL_REG32_WRITE(a,v) uart_write(a,v)
#define AL_REG32_GET_BIT(a,b) ((uart_read(a)>>(b))&1u)
#define AL_REG32_SET_BIT(a,b,v) uart_write(a,(uart_read(a)&~(1u<<(b)))|((unsigned)(v)<<(b)))
#define AL_REG32_GET_BITS(a,b,n) ((uart_read(a)>>(b))&((1u<<(n))-1))
#define AL_REG32_SET_BITS(a,b,n,v) uart_write(a,(uart_read(a)&~(((1u<<(n))-1)<<(b)))|((unsigned)(v)<<(b)))
#endif
