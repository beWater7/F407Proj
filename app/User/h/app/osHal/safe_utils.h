/***************************************************************
 * @file    :  storage_manage.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __SAFE_UTILS_H__ 
#define __SAFE_UTILS_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* 是否使用异常打印 */
#define SAFE_UTILS_DEBUG 1

int memcpy_s(void *dest, size_t dest_size, const void *src, size_t len);

int memmove_s(void *dest, size_t dest_size, const void *src, size_t len);

int memset_s(void *dest, size_t dest_size, int ch, size_t len);

char *strcpy_s(char *dest, size_t dest_size, const char *src);

char *strncpy_s(char *dest, size_t dest_size, const char *src, size_t len);

char *strcat_s(char *dest, size_t dest_size, const char *src);

char *strncat_s(char *dest, size_t dest_size, const char *src, size_t len);

int sprintf_s(char *dest, size_t dest_size, const char *format, ...);

int snprintf_s(char *dest, size_t dest_size, size_t len, const char *format, ...);


#endif /* __SAFE_UTILS_H__ */























