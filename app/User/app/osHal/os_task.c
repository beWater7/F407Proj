/***************************************************************
 * @file    :  os_task.c
 * @brief   :  FreeRTOS 后端实现（仅此文件及 os_hooks.c 应直接 include FreeRTOS）
 ***************************************************************/
#include "os_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stm32f4xx.h"

os_task_handle os_task_spawn(const char *name, os_task_fn_t fn, void *arg,
                             uint16_t stack_words, uint8_t prio)
{
    TaskHandle_t handle = NULL;
    if (xTaskCreate((TaskFunction_t)fn, name, stack_words, arg, prio, &handle) != pdPASS) {
        return NULL;
    }
    return (os_task_handle)handle;
}

void os_task_suspend(os_task_handle task)
{
    if (task) {
        vTaskSuspend((TaskHandle_t)task);
    }
}

void os_task_resume(os_task_handle task)
{
    if (task) {
        vTaskResume((TaskHandle_t)task);
    }
}

void os_task_delete(os_task_handle task)
{
    vTaskDelete((TaskHandle_t)task);
}

void os_task_exit(void)
{
    vTaskDelete(NULL);
}

os_task_handle os_task_self(void)
{
    return (os_task_handle)xTaskGetCurrentTaskHandle();
}

os_task_handle os_task_find_by_name(const char *name)
{
    return (os_task_handle)xTaskGetHandle(name);
}

const char *os_task_name(os_task_handle task)
{
    if (!task) {
        return "";
    }
    return pcTaskGetName((TaskHandle_t)task);
}

uint16_t os_task_stack_free(os_task_handle task)
{
    if (!task) {
        return 0;
    }
    return (uint16_t)uxTaskGetStackHighWaterMark((TaskHandle_t)task);
}

int os_task_runtime_stats(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return -1;
    }
    vTaskGetRunTimeStats(buf);
    return 0;
}

uint8_t os_cpu_usage(void)
{
    TaskStatus_t *task_status;
    UBaseType_t num_tasks;
    uint32_t total_run_time = 0;
    uint32_t idle_run_time = 0;
    uint32_t idle_percent;
    TaskHandle_t idle_handle;

    num_tasks = uxTaskGetNumberOfTasks();
    if (num_tasks == 0) {
        return 0;
    }

    task_status = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
    if (!task_status) {
        return 0;
    }

    num_tasks = uxTaskGetSystemState(task_status, num_tasks, &total_run_time);
    if (total_run_time < 100U) {
        vPortFree(task_status);
        return 0;
    }

    idle_handle = xTaskGetIdleTaskHandle();
    for (UBaseType_t i = 0; i < num_tasks; i++) {
        if (idle_handle != NULL) {
            if (task_status[i].xHandle == idle_handle) {
                idle_run_time = task_status[i].ulRunTimeCounter;
                break;
            }
        } else if (task_status[i].pcTaskName != NULL &&
                   task_status[i].pcTaskName[0] == 'I' &&
                   task_status[i].pcTaskName[1] == 'D') {
            idle_run_time = task_status[i].ulRunTimeCounter;
            break;
        }
    }
    vPortFree(task_status);

    idle_percent = idle_run_time / (total_run_time / 100U);
    if (idle_percent > 100U) {
        idle_percent = 100U;
    }
    return (uint8_t)(100U - idle_percent);
}

os_queue_t os_queue_create(uint32_t length, uint32_t item_size)
{
    return (os_queue_t)xQueueCreate((UBaseType_t)length, (UBaseType_t)item_size);
}

os_status_t os_queue_send(os_queue_t queue, const void *item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0U) ? 0U : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE) {
        return OS_OK;
    }
    return OS_ERR;
}

os_status_t os_queue_recv(os_queue_t queue, void *item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0U) ? 0U : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE) {
        return OS_OK;
    }
    return OS_TIMEOUT;
}

size_t os_heap_free(void)
{
    return (size_t)xPortGetFreeHeapSize();
}

size_t os_heap_min_free(void)
{
    return (size_t)xPortGetMinimumEverFreeHeapSize();
}

uint8_t os_heap_usage_percent(void)
{
    size_t free_heap = os_heap_free();
    free_heap *= 100U;
    return (uint8_t)(100U - (free_heap / (size_t)configTOTAL_HEAP_SIZE));
}

void *os_mem_alloc(size_t size)
{
    return pvPortMalloc(size);
}

void os_mem_free(void *ptr)
{
    if (ptr) {
        vPortFree(ptr);
    }
}

void os_scheduler_start(void)
{
    vTaskStartScheduler();
}

int os_scheduler_running(void)
{
    return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

void os_reboot_delay_sec(uint32_t sec)
{
    if (sec == 0U) {
        sec = 1U;
    }
    vTaskDelay(pdMS_TO_TICKS(sec * 1000U));
    NVIC_SystemReset();
}
