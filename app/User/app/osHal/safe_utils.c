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
#include "safe_utils.h"
#if SAFE_UTILS_DEBUG
#include "os_debug.h"
#define SAFE_UTILS_LOG  os_debug
#else
#define SAFE_UTILS_LOG(...) do { } while (0)
#endif


/***************************************************** 
 * @fn       memcpy_s
 * @brief    安全内存复制，防止越界溢出
 * @note     长度越界检测，调用者需保证参数非空
 * @param    dest      目标缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    src       源缓冲区指针
 * @param    len       复制长度（字节）
 * @retval   0:成功, -1:长度越界
 *****************************************************/
int memcpy_s(void *dest, size_t dest_size, const void *src, size_t len)
{
    if (len > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return -1;
    }

    memcpy(dest, src, len);

    return 0;
}


/***************************************************** 
 * @fn       memmove_s
 * @brief    安全内存移动，防止越界溢出
 * @note     长度越界检测，调用者需保证参数非空
 * @param    dest      目标缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    src       源缓冲区指针
 * @param    len       移动长度（字节）
 * @retval   0:成功, -1:长度越界
 *****************************************************/
int memmove_s(void *dest, size_t dest_size, const void *src, size_t len)
{
    if (len > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");    
        return -1;
    }

    memmove(dest, src, len);

    return 0;
}


/***************************************************** 
 * @fn       memset_s
 * @brief    安全内存设置，防止越界溢出
 * @note     长度越界检测，调用者需保证参数非空
 * @param    dest      目标缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    ch        设置的字节值
 * @param    len       设置长度（字节）
 * @retval   0:成功, -1:长度越界
 *****************************************************/
int memset_s(void *dest, size_t dest_size, int ch, size_t len)
{
    if (len > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return -1;
    }

    memset(dest, ch, len);

    return 0;
}


/***************************************************** 
 * @fn       strcpy_s
 * @brief    安全字符串复制，防止越界溢出
 * @note     长度越界检测（含'\0'），调用者需保证参数非空
 * @param    dest      目标字符串缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    src       源字符串指针
 * @retval   dest:成功, NULL:长度越界
 *****************************************************/
char *strcpy_s(char *dest, size_t dest_size, const char *src)
{
    size_t src_len = strlen(src);

    if (src_len + 1> dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return NULL;
    }

    strcpy(dest, src);

    return dest;
}


/***************************************************** 
 * @fn       strncpy_s
 * @brief    安全字符串拷贝，防止越界溢出
 * @note     长度越界检测，确保'\0'结尾，调用者需保证参数非空
 * @param    dest      目标字符串缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    src       源字符串指针
 * @param    len       拷贝最大长度（字节）
 * @retval   dest:成功, NULL:长度越界
 *****************************************************/
char *strncpy_s(char *dest, size_t dest_size, const char *src, size_t len)
{
    if (len > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return NULL;
    }

    strncpy(dest, src, len);

    return dest;
}


/***************************************************** 
 * @fn       strcat_s
 * @brief    安全字符串拼接，防止越界溢出
 * @note     长度越界检测（含'\0'），调用者需保证参数非空且dest是以'\0'结尾的字符串
 * @param    dest      目标字符串缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    src       源字符串指针
 * @retval   dest:成功, NULL:长度越界
 *****************************************************/
char *strcat_s(char *dest, size_t dest_size, const char *src)
{
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);

    if ((dest_len + src_len + 1) > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return NULL;
    }

    return strcat(dest, src);
}


/***************************************************** 
 * @fn       strncat_s
 * @brief    安全字符串拼接（指定长度），防止越界溢出
 * @note     长度越界检测，确保'\0'结尾，调用者需保证参数非空且dest是以'\0'结尾的字符串
 * @param    dest      目标字符串缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    src       源字符串指针
 * @param    len       拼接最大长度
 * @retval   dest:成功, NULL:长度越界
 *****************************************************/
char *strncat_s(char *dest, size_t dest_size, const char *src, size_t len)
{
    size_t dest_len = strlen(dest);

    if ((dest_len + len + 1) > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return NULL;
    }

    strncat(dest, src, len);
    //dest[dest_len + len] = '\0';

    return dest;
}


/***************************************************** 
 * @fn       sprintf_s
 * @brief    安全格式化字符串写入
 * @note     长度越界检测，调用者需保证参数非空
 * @param    dest      目标字符串缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    format    格式字符串
 * @param    ...       可变参数
 * @retval   写入字符数（不包含'\0'）:成功, 负值:错误（越界或格式错误）
 *****************************************************/
int sprintf_s(char *dest, size_t dest_size, const char *format, ...)
{
    int ret = 0;
    va_list args;

    va_start(args, format);
    ret = vsnprintf(dest, dest_size, format, args);
    va_end(args);

    if (ret < 0 || (size_t)ret >= dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return -1;
    }

    return ret;
}


/***************************************************** 
 * @fn       sprintf_s
 * @brief    安全格式化字符串写入
 * @note     长度越界检测，调用者需保证参数非空
 * @param    dest      目标字符串缓冲区指针
 * @param    dest_size 目标缓冲区大小（字节）
 * @param    format    格式字符串
 * @param    ...       可变参数
 * @retval   写入字符数（不包含'\0'）:成功, 负值:错误（越界或格式错误）
 *****************************************************/
int snprintf_s(char *dest, size_t dest_size, size_t len, const char *format, ...)
{
    int ret = 0;
    va_list args;

    if(len > dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return -1;
    }

    va_start(args, format);
    ret = vsnprintf(dest, dest_size, format, args);
    va_end(args);

    if (ret < 0 || (size_t)ret >= dest_size)
    {
        SAFE_UTILS_LOG("err src len!\n");
        return -1;
    }

    return ret;
}



