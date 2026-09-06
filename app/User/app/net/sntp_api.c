#include <time.h>
#include <string.h>
#include "sntp_api.h"
#if defined(CONFIG_APP_SNTP)
#include "sntp.h"
#endif

#include "hal_rtc.h"
#include "os_debug.h"
#include "log.h"
#include "safe_utils.h"


#define print_log printf

#if defined(CONFIG_APP_SNTP)
/*!
* @brief 设置 SNTP 的服务器地址，
* 		 加入多个 IP 以免某个 IP 获取不了时间
*        执行条件：无
*
* @retval: 无
*/
void set_sntp_server_list(void)
{
	uint32_t server_list[SNTP_MAX_SERVERS] =	{  
													0x279148D2,  //国家授时中心
													0x42041876,
													0x5F066CCA,
													0x0B6C1978,
													0x0B0C5CB6,
													0x58066BCB,
													0x14731978,
													0xC51F70CA,
													0x521D70CA,
													0x820176CA,
													0x510176CA,
												};
	ip_addr_t sntp_server;
												
	for(int i = 0; i < SNTP_MAX_SERVERS; i++)
	{
		//sntp_server.u_addr.ip4.addr = server_list[i];
		sntp_server.addr = server_list[i];
		sntp_setserver(i, &sntp_server);  // 国家授时中心
	}
}


/*!
* @brief Lwip 的 SNTP 初始化封装接口
*        执行条件：无
*
* @retval: 无
*/
void sntp_api_init(void)
{
	//设置 SNTP 的获取方式 -> 使用向服务器获取方式
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	
	//SNTP 初始化
	sntp_init();
	
	//加入授时中心的IP信息
	set_sntp_server_list();

	//sntp_setserver(i, &sntp_server);  //使用服务器域名？	
}


/*!
* @brief SNTP 获取时间戳的处理函数
*        执行条件：无
*
* @param [in] : sntp 获取的时间戳
*
* @retval: 无
*/
void sntp_set_time(uint32_t sntp_time)
{
	if(sntp_time == 0)
	{
		print_log("sntp_set_time: wrong!@@\n");
		return;
	}
	
	print_log("sntp_set_time: c00, enter!\n");
	print_log("sntp_set_time: c01, get time = %lu\n", (unsigned long)sntp_time);

	struct tm *time;
	sntp_time += (8 * 60 * 60); ///北京时间是东8区需偏移8小时

	time = localtime(&sntp_time);

	/*
	 * 设置 RTC 时间（含日期），并写入备份寄存器标记
	 */
	hal_rtc_set_timestamp(sntp_time);

	print_log("sntp_set_time: c02, decode time: 20%02d-%02d-%02d %d:%d:%d\n", \
				time->tm_year - 100, time->tm_mon + 1, time->tm_mday, \
				time->tm_hour, time->tm_min, time->tm_sec);
	
	print_log("sntp_set_time: c03, test get = %lu\n", (unsigned long)hal_rtc_get_timestamp());
	print_log("sntp_set_time: c04, set rtc time done\n");
}
#endif /* CONFIG_APP_SNTP */


static int parse_uptime(char* uptime, struct tm *t)
{
    if(strlen(uptime) != 14) return -1;

    // 解析字符串为数字
    t->tm_year = atoi((char[]){uptime[0],uptime[1],uptime[2],uptime[3],0}) - 1900;
    t->tm_mon  = atoi((char[]){uptime[4],uptime[5],0}) - 1;
    t->tm_mday = atoi((char[]){uptime[6],uptime[7],0});
    t->tm_hour = atoi((char[]){uptime[8],uptime[9],0});
    t->tm_min  = atoi((char[]){uptime[10],uptime[11],0});
    t->tm_sec  = atoi((char[]){uptime[12],uptime[13],0});

    // 计算星期（可选，但STM32 RTC要求）
    time_t raw = mktime(t);
    struct tm *tmp = localtime(&raw);
    t->tm_wday = tmp->tm_wday; // 0-6, 周日为0

    return 0;
}


void RTC_Set_From_Uptime(char *uptime)
{
    struct tm t;
    hal_rtc_time_t r;
    if(parse_uptime(uptime, &t) != 0)
    {
        printf("uptime err format!\n");
        return;
    }

    r.year   = t.tm_year + 1900;
    r.month  = t.tm_mon + 1;
    r.day    = t.tm_mday;
    r.wday   = t.tm_wday;
    r.hour   = t.tm_hour;
    r.minute = t.tm_min + 9; // baidu min + 9m
    r.second = t.tm_sec;
    hal_rtc_set_time(&r);

    os_printf("RTC to → %04d-%02d-%02d %02d:%02d:%02d\n",
          t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

}


/*!
 * @brief 获取当前时间戳
 *
 * @param [in] none
 *
 * @retval 当前时间戳
 */
uint32_t get_timestamp(void)
{
    return hal_rtc_get_timestamp();
}


/*!
 * @brief 获取当前时间戳
 *
 * @param [in] none
 *
 * @retval 当前时间戳
 */
void print_timestamp(char *buf)
{
    struct tm stm;
    hal_rtc_time_t r;
	char byTmp[32] = {0};

	if (hal_rtc_get_time(&r) != 0) {
        return;
    }

    stm.tm_year = r.year - 2000;    //RTC_Year rang 0-99,but tm_year since 1900
    stm.tm_mon  = r.month;
    stm.tm_mday = r.day;
    stm.tm_hour = r.hour;
    stm.tm_min  = r.minute;
    stm.tm_sec  = r.second;

	if(buf)
	{
		snprintf(byTmp, sizeof(byTmp), "20%02d-%02d-%02d %02d:%02d:%02d  ",       \
		stm.tm_year, stm.tm_mon, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);
		memcpy_s(buf, LOG_PREFIX_LEN, byTmp, strlen(byTmp));
	}
	else
	{
#if 1
    // os_printf("sys_time: 20%02d-%02d-%02d %02d:%02d:%02d\n",
	// 		   stm.tm_year, stm.tm_mon, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);

//    os_debug("sys_time: 20%02d-%02d-%02d %02d:%02d:%02d\n",
//           stm.tm_year, stm.tm_mon, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);

//
//    __os_printf("sys_time: 20%02d-%02d-%02d %02d:%02d:%02d\n",
//			   stm.tm_year, stm.tm_mon, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);
//
//    printf("sys_time: 20%02d-%02d-%02d %02d:%02d:%02d\n",
//		   stm.tm_year, stm.tm_mon, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);
//
//    __os_printf(KERN_WARN"sys_time: 20%02d-%02d-%02d %02d:%02d:%02d\n",
//			   stm.tm_year, stm.tm_mon, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);
#endif
	}
}



