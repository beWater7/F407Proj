/***************************************************************
 * @file    :  os_task.h
 * @brief   :  任务 / 队列 / 堆 抽象（应用层只依赖本头文件，不直接 include FreeRTOS）
 ***************************************************************/
#ifndef __OS_TASK_H__
#define __OS_TASK_H__

#include "os_types.h"

typedef void (*os_task_fn_t)(void *arg);

typedef enum {
    OS_OK = 0,
    OS_ERR = 1,
    OS_TIMEOUT = 2,
} os_status_t;

/* 任务 */
os_task_handle os_task_spawn(const char *name, os_task_fn_t fn, void *arg,
                             uint16_t stack_words, uint8_t prio);
void os_task_suspend(os_task_handle task);
void os_task_resume(os_task_handle task);
void os_task_delete(os_task_handle task);
void os_task_exit(void);
os_task_handle os_task_self(void);
os_task_handle os_task_find_by_name(const char *name);
const char *os_task_name(os_task_handle task);
uint16_t os_task_stack_free(os_task_handle task);
int os_task_runtime_stats(char *buf, size_t len);
uint8_t os_cpu_usage(void);

/* 队列 */
os_queue_t os_queue_create(uint32_t length, uint32_t item_size);
os_status_t os_queue_send(os_queue_t queue, const void *item, uint32_t timeout_ms);
os_status_t os_queue_recv(os_queue_t queue, void *item, uint32_t timeout_ms);

/* 堆 */
size_t os_heap_free(void);
size_t os_heap_min_free(void);
uint8_t os_heap_usage_percent(void);
void *os_mem_alloc(size_t size);
void os_mem_free(void *ptr);

/* 调度器（仅平台入口 main 使用） */
void os_scheduler_start(void);
int os_scheduler_running(void);

/* 延时后复位（OTA / 配置保存等） */
void os_reboot_delay_sec(uint32_t sec);

#endif /* __OS_TASK_H__ */
