#ifndef SNTP_API_H
#define SNTP_API_H

#include <stdint.h>
//#include "lwip/ip_addr.h"
void sntp_set_time(uint32_t sntp_time);
uint32_t get_timestamp(void);


/* 外部调用函数 */
void bsp_sntp_init(void);
void print_timestamp(char *buf);
void RTC_Set_From_Uptime(char *uptime);

#endif



