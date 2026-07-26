#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef char int8;
typedef short int16;
typedef int int32;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned int size_t;
#include <stdbool.h>
#define BOOL bool
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* 如果编译器支持 C99，就直接用标准库 */
#if __STDC_VERSION__ >= 199901L || defined(__GNUC__) || defined(_MSC_VER)
    #include <stdint.h>
#else
    /* 否则我们自己定义 */
    typedef signed char     int8_t;
    typedef short           int16_t;
    typedef int             int32_t;
    typedef unsigned char   uint8_t;
    typedef unsigned short  uint16_t;
    typedef unsigned int    uint32_t;

    /* 如果需要 64 位整数，也可以加上 */
    #if defined(_MSC_VER) && (_MSC_VER < 1300)
        typedef __int64            int64_t;
        typedef unsigned __int64   uint64_t;
    #else
        typedef long long          int64_t;
        typedef unsigned long long uint64_t;
    #endif

#endif /* __STDC_VERSION__ >= 199901L */


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


#define INT8_MIN   (-128)
#define MIN(a, b) (a > b ? b : a)
#define MAX(a, b) (a > b ? a : b)


#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
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


