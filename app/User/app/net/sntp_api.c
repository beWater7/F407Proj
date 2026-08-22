#include <time.h>
#include <string.h>
#include "sntp_api.h"
#if defined(CONFIG_APP_SNTP)
#include "sntp.h"
#endif

#include "stm32f4xx.h"
#include "bsp_rtc.h"
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
void bsp_sntp_init(void)
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
	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef sDate = {0};

	sntp_time += (8 * 60 * 60); ///北京时间是东8区需偏移8小时

	time = localtime(&sntp_time);

	/*
	 * 设置 RTC 的 时间
	 */
	sTime.RTC_H12 = RTC_H12_AMorPM;
	sTime.RTC_Hours = time->tm_hour;	  
	sTime.RTC_Minutes = time->tm_min;		
	sTime.RTC_Seconds = time->tm_sec;	
	RTC_SetTime(RTC_Format_BINorBCD, &sTime);
	RTC_WriteBackupRegister(RTC_BKP_DRX, RTC_BKP_DATA);

	/*
	 * 设置 RTC 的 日期
	 */ 
	sDate.RTC_WeekDay = time->tm_wday;	 
	sDate.RTC_Date    = time->tm_mday;	
	sDate.RTC_Month = time->tm_mon + 1; //tm_mon:0-11, 0表示1月份，依次类推  
	sDate.RTC_Year = (time->tm_year) + 1900 - 2000;	//tm_mon表示自1900以来的年数(2024对应124)，RTC_Year(0-99, 2024对应24) 	
	RTC_SetDate(RTC_Format_BINorBCD, &sDate);
	RTC_WriteBackupRegister(RTC_BKP_DRX, RTC_BKP_DATA);

	print_log("sntp_set_time: c02, decode time: 20%d-%02d-%02d %d:%d:%d\n", \
				sDate.RTC_Year, sDate.RTC_Month, sDate.RTC_Date, sTime.RTC_Hours, sTime.RTC_Minutes, sTime.RTC_Seconds);
	
	print_log("sntp_set_time: c03, test get = %lu\n", (unsigned long)get_timestamp());
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
    if(parse_uptime(uptime, &t) != 0)
    {
        printf("uptime err format!\n");
        return;
    }

    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    /* 设置 RTC 时间 */
    sTime.RTC_H12 = RTC_H12_AMorPM;
    sTime.RTC_Hours   = t.tm_hour;
    sTime.RTC_Minutes = t.tm_min + 9; // baidu min + 9m
    sTime.RTC_Seconds = t.tm_sec;
    RTC_SetTime(RTC_Format_BINorBCD, &sTime);
    RTC_WriteBackupRegister(RTC_BKP_DRX, RTC_BKP_DATA);

    /* 设置 RTC 日期 */
    sDate.RTC_WeekDay = t.tm_wday;    
    sDate.RTC_Date    = t.tm_mday;
    sDate.RTC_Month   = t.tm_mon + 1;
    sDate.RTC_Year    = t.tm_year + 1900 - 2000;
    RTC_SetDate(RTC_Format_BINorBCD, &sDate);
    RTC_WriteBackupRegister(RTC_BKP_DRX, RTC_BKP_DATA);

    os_printf("RTC to → %04d-%02d-%02d %02d:%02d:%02d\n",
           t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

	writeLog("esp8266 init");
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
    struct tm stm;
    static RTC_DateTypeDef g_Date = {0};
    static RTC_TimeTypeDef g_Time = {0};

    ///获取时间必须在获取日期前
    RTC_GetTime(RTC_Format_BIN, &g_Time);
    RTC_GetDate(RTC_Format_BIN, &g_Date);

    stm.tm_year = g_Date.RTC_Year + 100;    //RTC_Year rang 0-99,but tm_year since 1900

    stm.tm_mon = g_Date.RTC_Month - 1;      //RTC_Month rang 1-12,but tm_mon rang 0-11

    stm.tm_mday = g_Date.RTC_Date;          //RTC_Date rang 1-31 and tm_mday rang 1-31

    stm.tm_hour = g_Time.RTC_Hours;         //RTC_Hours rang 0-23 and tm_hour rang 0-23

    stm.tm_min = g_Time.RTC_Minutes;        //RTC_Minutes rang 0-59 and tm_min rang 0-59

    stm.tm_sec = g_Time.RTC_Seconds;

	return (mktime(&stm) - (8 * 60 * 60));///配置时由于东八区增加8小时，现为时间戳，需减去
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
    static RTC_DateTypeDef g_Date = {0};
    static RTC_TimeTypeDef g_Time = {0};
	char byTmp[32] = {0};

    ///获取时间必须在获取日期前
    RTC_GetTime(RTC_Format_BIN, &g_Time);
    RTC_GetDate(RTC_Format_BIN, &g_Date);

    stm.tm_year = g_Date.RTC_Year /*+ 100*/;    //RTC_Year rang 0-99,but tm_year since 1900

    stm.tm_mon = g_Date.RTC_Month /*- 1*/;      //RTC_Month rang 1-12,but tm_mon rang 0-11

    stm.tm_mday = g_Date.RTC_Date;          //RTC_Date rang 1-31 and tm_mday rang 1-31

    stm.tm_hour = g_Time.RTC_Hours;         //RTC_Hours rang 0-23 and tm_hour rang 0-23

    stm.tm_min = g_Time.RTC_Minutes;        //RTC_Minutes rang 0-59 and tm_min rang 0-59

    stm.tm_sec = g_Time.RTC_Seconds;

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



