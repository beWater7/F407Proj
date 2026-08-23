/***************************************************************
 * @file    :  os_mutex.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  Provide an abstraction layer implementation for use with FreeRTOS, RT-Thread, and similar systems. 
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/


#ifndef __OS_MUTEX_H__
#define __OS_MUTEX_H__

#include "systemConfig.h"
#include "os_types.h"

#if defined(USE_FREERTOS)
#include "FreeRTOS.h"
#include "semphr.h"

typedef SemaphoreHandle_t os_mutex_t;
typedef SemaphoreHandle_t os_sem_t;
/* 兼容旧代码里的 os_mutex 类型名 */
typedef os_mutex_t os_mutex;

#define OS_WAIT_FOREVER   0xffffffff

#define os_mutex_init(p) (p = xSemaphoreCreateMutex())

/* 支持 os_mutex_lock(m) 或 os_mutex_lock(m, timeout) */
#define os_mutex_lock(...)       OS_MUTEX_LOCK_SEL(__VA_ARGS__, OS_WAIT_FOREVER)
#define OS_MUTEX_LOCK_SEL(p, t, ...)                                        \
    do                                                                      \
    {                                                                       \
        if (xSemaphoreTake((p), (t)) != pdTRUE)                             \
        {                                                                   \
            configASSERT(0);                                                \
        }                                                                   \
    } while (0)

#define os_mutex_unlock(p)                              \
    do                                                  \
    {                                                   \
        if (xSemaphoreGive(p) != pdTRUE)                \
        {                                               \
            configASSERT(0);                            \
        }                                               \
}while (0)

#define os_mutex_destroy(x)            vSemaphoreDelete((x))
#define os_task_create(pxTaskCode, pcName, uxStackDepth, pvParameters, uxPriority, pxCreatedTask)   \
                        xTaskCreate(pxTaskCode,      \
                            pcName,                  \
                            uxStackDepth,            \
                            pvParameters,            \
                            uxPriority,              \
                            pxCreatedTask)        

#define os_sem_init(x)                  do { (x) = xSemaphoreCreateBinary(); } while(0)
#define os_sem_take(x, timeout)         xSemaphoreTake((x), ((timeout)==OS_WAIT_FOREVER)?portMAX_DELAY:(timeout))
#define os_sem_give(x)                  xSemaphoreGive((x))
#define os_sem_give_from_isr(x)         do { \
                                            BaseType_t xHigherPriorityTaskWoken = pdFALSE; \
                                            xSemaphoreGiveFromISR((x), &xHigherPriorityTaskWoken); \
                                            portYIELD_FROM_ISR(xHigherPriorityTaskWoken); \
                                        } while(0)
#define os_sem_delete(x)                vSemaphoreDelete((x))

#define OS_MS          pdMS_TO_TICKS
#define os_sleep_ms(x) vTaskDelay(pdMS_TO_TICKS(x))
#define os_sleep(x) vTaskDelay(pdMS_TO_TICKS(x * 1000))

#define __os_malloc  pvPortMalloc
#define __os_free    vPortFree

#define os_malloc(l)  (__os_malloc(l))
#define os_free(p)    do{ void *_os_fp = (void *)(p); if (_os_fp) { __os_free(_os_fp); } }while(0)
/* 断言 */
#define OS_ASSERT(x)  do{if(!(x)){ os_debug_header(); os_sleep(1); NVIC_SystemReset();}}while(0)

#define os_time()      ((uint32_t)xTaskGetTickCount())
#ifndef sys_jiffies
#define sys_jiffies()  ((uint32_t)xTaskGetTickCount())
#endif

//#define os_task_create() 
#define TASK_PRIORITY_IDLE            0   /* 空闲任务 */
#define TASK_PRIORITY_LOW             1
#define TASK_PRIORITY_BELOW_NORMAL    2
#define TASK_PRIORITY_NORMAL          3  
#define TASK_PRIORITY_ABOVE_NORMAL    4
#define TASK_PRIORITY_HIGH            5
#define TASK_PRIORITY_REALTIME        6
#define TASK_PRIORITY_MAX             7   /* 最高优先级 */

/* os_task_handle 定义见 os_types.h */

#elif defined(USE_RTTHREAD) // 裸机，无需锁
#include "rtthread.h"
typedef rt_sem_t os_sem_t;

#define os_sem_init(x)                  do { (x) = rt_sem_create("sem", 0, RT_IPC_FLAG_FIFO); } while(0)
#define os_sem_take(x, timeout)         rt_sem_take((x), ((timeout)==OS_WAIT_FOREVER)?RT_WAITING_FOREVER:(timeout))
#define os_sem_give(x)                  rt_sem_release((x))
#define os_sem_give_from_isr(x)         rt_sem_release((x))   /* RT-Thread支持中断释放 */
#define os_sem_delete(x)                rt_sem_delete((x))

#elif defined(USE_BAREMETAL)
typedef struct {
    volatile int count;
} os_sem_t;

#define os_sem_init(x)                  do { (x).count = 0; } while(0)
#define os_sem_take(x, timeout)         do { while ((x).count == 0); (x).count--; } while(0)
#define os_sem_give(x)                  do { (x).count++; } while(0)
#define os_sem_give_from_isr(x)         os_sem_give(x)
#define os_sem_delete(x)                do { (x).count = 0; } while(0)

/* 其他系统 */

#else
typedef void *os_mutex_t;
#define os_mutex_init(p) ((void)0)
#define os_mutex_lock(p) ((void)0)
#define os_mutex_unlock(p) ((void)0)
#define os_mutex_destroy(p) ((void)0)
#endif


#define MAX_THREADS_NUM 32

/* 保存所有任务的handle */
typedef struct sys_thread_info{
    os_task_handle sys_handle_array[MAX_THREADS_NUM];
    uint8_t Index_free; //指向空闲地址
}SYS_THREAD_INFO_T, *LP_SYS_THREAD_INFO;

#endif


