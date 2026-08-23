/***************************************************************
 * @file    :  os_types.h
 * @brief   :  与 RTOS / lwIP 无关的 OS 句柄类型（供应用层头文件使用）
 ***************************************************************/
#ifndef __OS_TYPES_H__
#define __OS_TYPES_H__

#include "typedef.h"

typedef void *os_task_handle;
typedef void *os_queue_t;

#endif /* __OS_TYPES_H__ */
