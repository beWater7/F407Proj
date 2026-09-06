#ifndef _HAL_RTC_H
#define _HAL_RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 芯片无关的 RTC 时间结构体（本地钟表时间） */
typedef struct
{
    uint16_t year;   /* 完整年份，如 2026 */
    uint8_t  month;  /* 1~12 */
    uint8_t  day;    /* 1~31 */
    uint8_t  wday;   /* 0~6，0=周日 */
    uint8_t  hour;   /* 0~23 */
    uint8_t  minute; /* 0~59 */
    uint8_t  second; /* 0~59 */
} hal_rtc_time_t;

/* RTC 硬件初始化 */
void hal_rtc_init(void);

/* 设置/读取 RTC 时间（本地钟表时间），成功返回 0 */
int  hal_rtc_set_time(const hal_rtc_time_t *t);
int  hal_rtc_get_time(hal_rtc_time_t *t);

/* 时间戳接口：参数/返回值均为本地时区（东八区）时间戳 */
int     hal_rtc_set_timestamp(uint32_t local_ts);
uint32_t hal_rtc_get_timestamp(void);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_RTC_H */
