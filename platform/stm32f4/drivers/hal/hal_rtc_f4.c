/* STM32F4 平台的 RTC 适配实现：封装 StdPeriph RTC 接口 */
#include "hal_rtc.h"
#include "bsp_rtc.h"
#include <time.h>

void hal_rtc_init(void)
{
    rtc_init();
}

/* 设置 RTC 硬件时间寄存器 */
static void rtc_write_hw(const hal_rtc_time_t *t)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    sTime.RTC_H12 = RTC_H12_AMorPM;
    sTime.RTC_Hours   = t->hour;
    sTime.RTC_Minutes = t->minute;
    sTime.RTC_Seconds = t->second;
    RTC_SetTime(RTC_Format_BINorBCD, &sTime);
    RTC_WriteBackupRegister(RTC_BKP_DRX, RTC_BKP_DATA);

    sDate.RTC_WeekDay = t->wday;
    sDate.RTC_Date    = t->day;
    sDate.RTC_Month   = t->month;
    sDate.RTC_Year    = (t->year >= 2000U) ? (t->year - 2000U) : t->year;
    RTC_SetDate(RTC_Format_BINorBCD, &sDate);
    RTC_WriteBackupRegister(RTC_BKP_DRX, RTC_BKP_DATA);
}

int hal_rtc_set_time(const hal_rtc_time_t *t)
{
    if (t == NULL) {
        return -1;
    }
    rtc_write_hw(t);
    return 0;
}

int hal_rtc_get_time(hal_rtc_time_t *t)
{
    static RTC_DateTypeDef g_date;
    static RTC_TimeTypeDef g_time;

    if (t == NULL) {
        return -1;
    }

    /* 必须先读时间，再读日期（硬件要求） */
    RTC_GetTime(RTC_Format_BIN, &g_time);
    RTC_GetDate(RTC_Format_BIN, &g_date);

    t->year   = g_date.RTC_Year + 2000U;
    t->month  = g_date.RTC_Month;
    t->day    = g_date.RTC_Date;
    t->wday   = g_date.RTC_WeekDay;
    t->hour   = g_time.RTC_Hours;
    t->minute = g_time.RTC_Minutes;
    t->second = g_time.RTC_Seconds;
    return 0;
}

int hal_rtc_set_timestamp(uint32_t local_ts)
{
    time_t t = (time_t)local_ts;
    struct tm *tm = localtime(&t);
    hal_rtc_time_t r;

    if (tm == NULL) {
        return -1;
    }

    r.year   = tm->tm_year + 1900;
    r.month  = tm->tm_mon + 1;
    r.day    = tm->tm_mday;
    r.wday   = tm->tm_wday;
    r.hour   = tm->tm_hour;
    r.minute = tm->tm_min;
    r.second = tm->tm_sec;
    return hal_rtc_set_time(&r);
}

uint32_t hal_rtc_get_timestamp(void)
{
    hal_rtc_time_t r;
    struct tm stm;

    if (hal_rtc_get_time(&r) != 0) {
        return 0;
    }

    stm.tm_year  = r.year - 1900;
    stm.tm_mon   = r.month - 1;
    stm.tm_mday  = r.day;
    stm.tm_hour  = r.hour;
    stm.tm_min   = r.minute;
    stm.tm_sec   = r.second;
    stm.tm_wday  = r.wday;
    stm.tm_isdst = 0;

    /* RTC 存的是本地时间（配置时已加 8 小时），换算回 UTC 时间戳 */
    return (uint32_t)mktime(&stm) - (8U * 60U * 60U);
}
