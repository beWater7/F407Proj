#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef char int8;
typedef short int16;
typedef int int32;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
#define BOOL bool
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif


#ifndef RET_OK
#define RET_OK    0
#endif
#ifndef RET_ERR
#define RET_ERR  -1
#endif

#ifndef FOREVER
#define FOREVER for(;;)
#endif

#ifndef TRUE
#define TRUE     1
#endif 
#ifndef true
#define true     1
#endif 

#ifndef FALSE
#define FALSE    0
#endif
#ifndef false
#define false     0
#endif 


#ifndef INT8_MIN
#define INT8_MIN   (-128)
#endif
#ifndef MIN
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#ifndef offsetof
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
#endif
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))


#define ALIGN_UP(val, align)   (((val) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(val, align) ((val) & ~((align) - 1))
#ifndef ALIGN
#define ALIGN(n) __attribute__((aligned(n)))
#endif

#if 0
// 向上对齐到 2^n
static inline uint32 align_up(uint32 val, uint32 align)
{
    return (val + align - 1) & ~(align - 1);
}

// 向下对齐到 2^n
static inline uint32 align_down(uint32 val, uint32 align)
{
    return val & ~(align - 1);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* __TYPEDEF_H__ */


