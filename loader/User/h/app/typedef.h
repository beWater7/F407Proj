#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__

typedef char int8;
typedef short int16;
typedef int int32;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned int size_t;

typedef unsigned char BOOL;


#if 0
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
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

#ifndef FALSE
#define FALSE    0
#endif

#define MIN(a, b) (a > b ? b : a)
#define MAX(a, b) (a > b ? a : b)

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))


#define ALIGN_UP(val, align)   (((val) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(val, align) ((val) & ~((align) - 1))

#endif



