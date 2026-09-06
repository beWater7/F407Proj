/***************************************************************
 * @file    :  os_task.c
 * @brief   :  FreeRTOS 后端实现（仅此文件及 os_hooks.c 应直接 include FreeRTOS）
 ***************************************************************/
#include "os_task.h"
#include "hal_cpu.h"
#include "os_debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

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
    configRUN_TIME_COUNTER_TYPE total_run_time = 0;
    uint32_t idle_run_time = 0;
    TaskHandle_t idle_handle;
    static configRUN_TIME_COUNTER_TYPE s_prev_total = 0;
    static uint32_t s_prev_idle = 0;
    static TickType_t s_last_ms = 0;
    static uint8_t s_last_pct = 0;
    static uint8_t s_armed = 0;   /* 已取过首帧基准 */
    static uint8_t s_warned = 0;  /* 时间基停摆只告警一次 */
    uint32_t d_total;
    uint32_t d_idle;
    uint32_t idle_pct;
    uint8_t cur_pct;

    num_tasks = uxTaskGetNumberOfTasks();
    if (num_tasks == 0) {
        return 0;
    }

    task_status = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
    if (task_status == NULL) {
        return s_last_pct;   /* 尽力而为：分配失败返回上次值 */
    }

    /* 采样窗口下限 200ms：调用比这更密(web 高频轮询)时直接复用上次结果，
     * 否则差分窗口太短，占用率会被周期性算成 0 或满格。 */
    {
        TickType_t now = xTaskGetTickCount();

        if (s_armed && (TickType_t)(now - s_last_ms) < pdMS_TO_TICKS(200U)) {
            vPortFree(task_status);
            return s_last_pct;
        }
        s_last_ms = now;
    }

    num_tasks = uxTaskGetSystemState(task_status, num_tasks, &total_run_time);
    idle_handle = xTaskGetIdleTaskHandle();
    for (UBaseType_t i = 0; i < num_tasks; i++) {
        if (idle_handle != NULL) {
            if (task_status[i].xHandle == idle_handle) {
                idle_run_time = (uint32_t)task_status[i].ulRunTimeCounter;
                break;
            }
        } else if (task_status[i].pcTaskName != NULL &&
                   task_status[i].pcTaskName[0] == 'I' &&
                   task_status[i].pcTaskName[1] == 'D') {
            idle_run_time = (uint32_t)task_status[i].ulRunTimeCounter;
            break;
        }
    }
    vPortFree(task_status);

    if (!s_armed) {
        /* 首帧只做基准，无窗口可算 */
        s_prev_total = total_run_time;
        s_prev_idle = idle_run_time;
        s_last_pct = 0;
        s_armed = 1;
        if (total_run_time == 0 && !s_warned) {
            s_warned = 1;
            os_debug("cpu stats: total=0 — run-time timer not running!\r\n");
        }
        return 0;
    }

    /* 差分：两次采样间的总时间与 idle 增量（无符号减法自动抗回绕） */
    d_total = (uint32_t)(total_run_time - s_prev_total);
    d_idle = idle_run_time - s_prev_idle;
    s_prev_total = total_run_time;
    s_prev_idle = idle_run_time;

    if (d_total == 0) {
        /* 时间基停摆：无新增刻度，读不出占用。首次打印一次便于定位
         * （历史教训：USB host 曾重配并停掉共享的 TIM2）。 */
        if (!s_warned) {
            s_warned = 1;
            os_debug("cpu stats: timer stalled (total=%lu idle=%lu)\r\n",
                     (unsigned long)total_run_time, (unsigned long)idle_run_time);
        }
        return s_last_pct;
    }

    idle_pct = d_idle * 100U / d_total;
    if (idle_pct > 100U) {
        idle_pct = 100U;
    }
    cur_pct = (uint8_t)(100U - idle_pct);
    s_last_pct = cur_pct;
    return cur_pct;
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
    hal_cpu_reset();
}
