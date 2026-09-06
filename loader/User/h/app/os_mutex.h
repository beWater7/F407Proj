#ifndef __OS_MUTEX_H__
#define __OS_MUTEX_H__

/* os_mutex.h */


#if defined(USE_FREERTOS)
#include "FreeRTOS.h"
#include "semphr.h"
typedef SemaphoreHandle_t os_mutex_t;
#define os_mutex_init(p)         (*(p) = xSemaphoreCreateMutex())
//#define os_mutex_lock(p)         xSemaphoreTake(*(p), portMAX_DELAY)
#define os_mutex_lock(p) do {                                  \\
    if (xSemaphoreTake(*(p), portMAX_DELAY) != pdTRUE) {       \\
        configASSERT(0);                                       \\
    }                                                          \\
} while (0)

#define os_mutex_unlock(p)       xSemaphoreGive(*(p))
#define os_mutex_destroy(p)      vSemaphoreDelete(*(p))

#elif defined(USE_RTTHREAD)  // 裸机，无需锁

/* 其他系统 */


#else
typedef void* os_mutex_t;
#define os_mutex_init(p)         ((void)0)
#define os_mutex_lock(p)         ((void)0)
#define os_mutex_unlock(p)       ((void)0)
#define os_mutex_destroy(p)      ((void)0)
#endif





#endif


