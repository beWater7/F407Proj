/*
 * FreeRTOS Kernel V11.1.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx.h"

/******************************************************************************/
/* Hardware description related definitions. **********************************/
/******************************************************************************/

#define configCPU_CLOCK_HZ    ( ( unsigned long ) 168000000 ) //20000000 ->168000000

/******************************************************************************/
/* Scheduling behaviour related definitions. **********************************/
/******************************************************************************/

#define configTICK_RATE_HZ                         ( 1000U )
#define configUSE_PREEMPTION                       1
#define configUSE_TIME_SLICING                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0
#define configUSE_TICKLESS_IDLE                    0
#define configMAX_PRIORITIES                       8U
#define configMINIMAL_STACK_SIZE                   128U
#define configMAX_TASK_NAME_LEN                    4U
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                    1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES      1U
#define configQUEUE_REGISTRY_SIZE                  0U
#define configENABLE_BACKWARD_COMPATIBILITY        1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS    0
#define configSTACK_DEPTH_TYPE                     size_t
#define configMESSAGE_BUFFER_LENGTH_TYPE           size_t
#define configUSE_NEWLIB_REENTRANT                 0

/******************************************************************************/
/* Software timer related definitions. ****************************************/
/******************************************************************************/

#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       ( configMAX_PRIORITIES - 1U )
#define configTIMER_TASK_STACK_DEPTH    configMINIMAL_STACK_SIZE
#define configTIMER_QUEUE_LENGTH        10U

/******************************************************************************/
/* Memory allocation related definitions. *************************************/
/******************************************************************************/

#define configSUPPORT_STATIC_ALLOCATION              1
#define configSUPPORT_DYNAMIC_ALLOCATION             1
/* PSRAM 1020KB，绝大部分给 FreeRTOS heap_4，web/OTA 走 os_malloc */
#define configTOTAL_HEAP_SIZE                        (960U * 1024U)
#define configAPPLICATION_ALLOCATED_HEAP             1
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP    0
#define configUSE_MINI_LIST_ITEM                     0

/******************************************************************************/
/* Interrupt nesting behaviour configuration. *********************************/
/******************************************************************************/

/* STM32F4: 4 位优先级，需左移到高 4 位写入 NVIC/BASEPRI，不能写裸的 0/3 */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                        __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                        4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_API_CALL_INTERRUPT_PRIORITY  configMAX_SYSCALL_INTERRUPT_PRIORITY

/******************************************************************************/
/* Hook and callback function related definitions. ****************************/
/******************************************************************************/

#define configUSE_IDLE_HOOK                   0
#define configUSE_TICK_HOOK                   0
#define configUSE_MALLOC_FAILED_HOOK          0
#define configUSE_DAEMON_TASK_STARTUP_HOOK    0
#define configCHECK_FOR_STACK_OVERFLOW        0

/******************************************************************************/
/* Run time and task stats gathering related definitions. *********************/
/******************************************************************************/

#define configGENERATE_RUN_TIME_STATS           1 //用于计算任务运行时间的定时器
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1

/******************************************************************************/
/* Definitions that include or exclude functionality. *************************/
/******************************************************************************/

#define configUSE_TASK_NOTIFICATIONS           1
#define configUSE_MUTEXES                      1
#define configUSE_RECURSIVE_MUTEXES            1
#define configUSE_COUNTING_SEMAPHORES          1
#define configUSE_QUEUE_SETS                   1
#define configUSE_APPLICATION_TASK_TAG         1
#define INCLUDE_vTaskPrioritySet               1
#define INCLUDE_uxTaskPriorityGet              1
#define INCLUDE_vTaskDelete                    1
#define INCLUDE_vTaskSuspend                   1
#define INCLUDE_xResumeFromISR                 1
#define INCLUDE_vTaskDelayUntil                1
#define INCLUDE_vTaskDelay                     1
#define INCLUDE_xTaskGetSchedulerState         1
#define INCLUDE_xTaskGetCurrentTaskHandle      1
#define INCLUDE_uxTaskGetStackHighWaterMark    1
#define INCLUDE_xTaskGetIdleTaskHandle         1
#define INCLUDE_eTaskGetState                  1
#define INCLUDE_xEventGroupSetBitFromISR       1
#define INCLUDE_xTimerPendFunctionCall         1
#define INCLUDE_xTaskAbortDelay                1
#define INCLUDE_xTaskGetHandle                 1
#define INCLUDE_xTaskResumeFromISR             1


/* configGENERATE_RUN_TIME_STATS 需要定义以下宏 */

/* 初始化和启动 用于统计的定时器 */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()          \
do{                                                       \
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;    \
	NVIC_InitTypeDef NVIC_InitStructure;                  \
	/* 使能TIM2时钟 */                                        \
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);   \
	/* 定时器分频 */                                           \
	TIM_TimeBaseInitStructure.TIM_Prescaler= 8;         \
    /* 向上计数模式 */                                          \
	TIM_TimeBaseInitStructure.TIM_CounterMode=TIM_CounterMode_Up; \
    /* 自动重装载值 */                                         \
	TIM_TimeBaseInitStructure.TIM_Period= 999;           \
	TIM_TimeBaseInitStructure.TIM_ClockDivision=TIM_CKD_DIV1;   \
	                                                      \
	TIM_TimeBaseInit(TIM2,&TIM_TimeBaseInitStructure);    \
	/* 使能定时器2 */                                          \
	TIM_Cmd(TIM2,ENABLE);                                 \
}while(0)


/* 获取硬件定时器的值 替换 */
#define portALT_GET_RUN_TIME_COUNTER_VALUE(x) (x = TIM_GetCounter(TIM2))
/* 获取硬件定时器的值 */
#define portGET_RUN_TIME_COUNTER_VALUE() (TIM_GetCounter(TIM2))


#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler
/* SysTick 由 stm32f4xx_it.c 里自定义 SysTick_Handler 转发到 xPortSysTickHandler */
#define configCHECK_HANDLER_INSTALLATION 0

#endif /* FREERTOS_CONFIG_H */
