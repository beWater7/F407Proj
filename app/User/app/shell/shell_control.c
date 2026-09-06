/************************************************
	??????USB????��???????????
	?????��??
	???????overlong
************************************************/


/************************************************
	????
************************************************/
#include "shell_control.h"
#include "net_api.h"
#include "safe_utils.h"
#include "devConfig.h"
#include "os_mutex.h"
#include "os_task.h"
#include "os_log.h"
#include "ping.h"
#include "flash_manage.h"
#include "dts.h"
#include "dev_manage.h"
#include "hal_board.h"
#include "hal_eth.h"
#include "hal_flash.h"
#if defined(CONFIG_APP_DHT11)
#include "drv_dht11.h"
#endif
#include "sntp_api.h"
#include <string.h>
#include <stdlib.h>
#if defined(CONFIG_APP_ESP8266)
#include "drv_esp8266.h"
#endif

extern SYS_THREAD_INFO_T sys_handle_info;
extern uint8_t byUseBinary;
extern void partition_info_show(void);
/************************************************
	??????
************************************************/
char* tab_arg[4] ={0};



/*******************************************************************************
??????look_time
????????????
?????????
	arg???????????
???????????
???????
*******************************************************************************/
static void look_time(void * arg)
{
	hal_board_clocks_t clk;
	hal_board_get_clocks(&clk);
	uint32_t data[] = {clk.sysclk_hz, clk.hclk_hz, clk.pclk1_hz, clk.pclk2_hz};
	printk("\r\n\tSYSCLK:%uHz"  ,data[0]);
	printk("\r\n\tHCLK:%uHz"    ,data[1]);
	printk("\r\n\tPCLK1:%uHz"   ,data[2]);
	printk("\r\n\tPCLK2:%uHz"   ,data[3]);
	//printk("\r\n\tADCCLK:%uHz" ,data[4]);
	
	printk("%s", SHELL_PROMPT);
}


/*******************************************************************************
??????shell_control_explain
?????????????ID????help?��?????????
?????????
	ID???????ID????32��??
???????????
???????
*******************************************************************************/
void shell_control_explain(unsigned int* ID)
{
uint32_t tab[] = {  0x8CC6D608 ,0x8D4C533C ,
                    0x8d598e99 ,0x8DC87250 ,
                    0x8E09AF53 ,0x9152F754 ,
                    0xA08971F3 ,0xCD1A7A1A ,
                    0xD9BDA819 ,0xDA1061B3 };
    char* str[] =  {  "\r\nscreen clean"             ,"\r\nshowGPIOx pin status[CMD] [GPIOx(GPIOA~G)]",
                      "\r\nshow all command ID"       , "\r\nshow chip sysclock param",
                      "\r\nshow clk status"           ,"\r\ndebug info",
                      "\r\nshow all registed command","\r\nset GPIO pin [CMD] [GPIOx(GPIOA~G)] [GPIO_Pin(0~15)] [1/0]",
                      "\r\nshow shell version"        ,"\r\nshow softversion"};
	for(int i = 0 ;i < 10 ;i++){
		if(*ID == tab[i]){
			printk("%s" ,str[i]);
		}
	}
}



/*******************************************************************************
??????shell_version
???????????????��
?????????
	arg???????????
???????????
???????
*******************************************************************************/
static void __attribute__((unused)) software_version(void * arg)
{
	printk("\r\n\t%s\r\n",software_VERSION);
	printk("%s", SHELL_PROMPT);
}



/*******************************************************************************
??????check_peripheral
????????????????
?????????
	arg???????????
???????????
???????
*******************************************************************************/
static void check_peripheral(void * arg)
{
	uint16_t temporary = 0;
	uint16_t a = 0;
	uint8_t array[][array_len] = {"AFIOEN" ,"IOPAEN" ,"IOPBEN" ,"IOPCEN" ,"IOPDEN" ,"IOPEEN" ,"ADC1EN" ,"ADC2EN" ,"TIM1EN" ,"SPI1EN" ,"USART1EN"};
	uint16_t APB2 = (uint16_t)(RCC->APB2ENR & 0x00007FFF);
	
	a = APB2;
	APB2 =  (a & 0x0001) | ((a & 0x007c) >> 1) | ((a & 0x1E00) >> 3) | ((a & 0x4000) >> 4);
	for(int i = 0 ;i < 11 ;i++){
		temporary = (APB2 >> i) & 0x0001;
		printk("\r\n\t%s:%d" ,array[i] ,temporary);
	}
	printk("%s", SHELL_PROMPT);
}


/*******************************************************************************
??????check_GPIO
???????GPIO??
?????????
	arg???????????
???????????
???????
*******************************************************************************/
static void check_GPIO(void * arg)
{
	char* argv[2] = {0};
	uint16_t ReadValue = 0;
	int data_falg = NO;
	int argc = cmdline_strtok((char*)arg,argv,2);
	(void)argc;
	if(strcmp(argv[1] ,"GPIOA") == 0){
		ReadValue = GPIO_ReadOutputData(GPIOA);
		data_falg = EN;
	}
	else if(strcmp(argv[1] ,"GPIOB") == 0){
		ReadValue = GPIO_ReadOutputData(GPIOB);
		data_falg = EN;
	}
	else if(strcmp(argv[1] ,"GPIOC") == 0){
		ReadValue = GPIO_ReadOutputData(GPIOC);
		data_falg = EN;
	}
	else if(strcmp(argv[1] ,"GPIOD") == 0){
		ReadValue = GPIO_ReadOutputData(GPIOD);
		data_falg = EN;
	}
	else
		data_falg = NO;
	if(data_falg == NO){
		printk("\r\n\tError");
	}
	else{
		uint16_t temporary = 0;
		for(int i = 0 ;i < 16 ;i++){
			temporary = (ReadValue >> i) & 0x0001;
			printk("\r\n\tP%c%d:%d" ,*(argv[1]+4) ,i ,temporary);
		}
	}
	data_falg = NO;
	printk("%s", SHELL_PROMPT);
}


/*******************************************************************************
??????set_GPIO
????????GPIO
?????????
	arg???????????
???????????
???????
*******************************************************************************/
static void set_GPIO(void * arg)
{
	//???��????????????????
	char* argv[4] = {0};
//	int argc = cmdline_strtok((char*)arg,argv,4);
	for(int i = 0 ;i < 4 ;i++){
		argv[i] = tab_arg[i];
	}
	uint8_t GPIOPIN = 0;
	const uint16_t GPIO_PIN_arrat[] = {GPIO_Pin_0 ,GPIO_Pin_1 ,GPIO_Pin_2 ,GPIO_Pin_3 ,GPIO_Pin_4 ,GPIO_Pin_5 ,
															 GPIO_Pin_6 ,GPIO_Pin_7 ,GPIO_Pin_8 ,GPIO_Pin_9 ,GPIO_Pin_10,GPIO_Pin_11,
															 GPIO_Pin_12,GPIO_Pin_13,GPIO_Pin_14,GPIO_Pin_15};
	const char* GPIO_PIN_nmb[16] = {"0" ,"1" ,"2" ,"3" ,"4" ,"5" ,"6" ,"7" ,"8" ,"9" ,
															 "10","11","12","13","14","15"};
	const char* GPIO_str_array[7] = {"GPIOA" ,"GPIOB" ,"GPIOC" ,"GPIOD" ,"GPIOE" ,"GPIOF" ,"GPIOG"};
	GPIO_TypeDef * GPIOx[7] = {GPIOA ,GPIOB ,GPIOC ,GPIOD ,GPIOE ,GPIOF ,GPIOG};
	int data_falg = NO;		//?????????��
	
	//?��??????????
	if(strcmp(argv[1] ,"GPIOA") == 0 || strcmp(argv[1] ,"GPIOB") == 0 || strcmp(argv[1] ,"GPIOC") || strcmp(argv[1] ,"GPIOD") == 0){
		for(int i = 0 ;i < 16 ;i++){
			if(strcmp(argv[2] ,GPIO_PIN_nmb[i]) == 0){
				if(*argv[3] == '0' || *argv[3] == '1')
					data_falg = EN;
				GPIOPIN = i;
			}
		}
	}

	//?????????GPIO????
	if(data_falg == EN){
		data_falg = NO;
		for(int i = 0 ;i < 7 ;i++){
			if(strcmp(argv[1] ,GPIO_str_array[i]) == 0){
				if(*argv[3] == '1'){
					GPIO_SetBits(GPIOx[i] ,GPIO_PIN_arrat[GPIOPIN]);
					data_falg = EN;
					if((GPIO_ReadOutputData(GPIOx[i]) & (0x0001 << GPIOPIN)) == 0){//?��???????????
						data_falg = NO;
					}
				}
				else{
					GPIO_ResetBits(GPIOx[i] ,GPIO_PIN_arrat[GPIOPIN]);
					data_falg = EN;
					if((GPIO_ReadOutputData(GPIOx[i]) & (0x0001 << GPIOPIN)) == 1){//?��???????????
						data_falg = NO;
					}
				}
			}
		}

		//?��???????��??
		if(data_falg == EN)
			printk("\r\n\t??????%s_Pin%s?%s" ,argv[1] ,argv[2] ,argv[3]);
		else
			printk("\r\n\tError");
	}
	else
		printk("\r\n\tError");
	data_falg = NO;
	printk("%s", SHELL_PROMPT);
}


/*******************************************************************************
??????really_set_GPIO
?????????????????GPIO
?????????
	arg???????????
???????????
???????
*******************************************************************************/
static void really_set_GPIO(void * arg)
{
	char* argv[4] = {0};
	int argc = cmdline_strtok((char*)arg,argv,4);
	(void)argc;
	printk("\r\n?????????%s_Pin%s?%s" ,argv[1] ,argv[2] ,argv[3]);
	for(int i = 0 ;i < 4 ;i++){
		tab_arg[i] = argv[i];
	}
	shell_confirm_GPIO(&shellx ,"",set_GPIO);
}


/* command added by liu */

static void sys_reboot_now(void *arg)
{
    UNUSED_ARG(arg);
    /* ??????reboot */
    hal_board_reboot();
}


static void sys_ifconfig(void *arg)
{
    char* argv[4] = {0};
    net_ipv4_t ip;
    net_ipv4_t mask;
    net_ipv4_t gw;
    char ipbuf[16];
    uint8_t mac[6];

    int argc = cmdline_strtok((char*)arg,argv,4);
    net_get_ip(&ip, &mask, &gw);

    if(argc >= 3)
    {
        if(strcmp(argv[1] ,"ip") == 0)
        {
            net_parse_ipv4(argv[2], &ip);
        }
        else if(strcmp(argv[1] ,"gw") == 0)
        {
            net_parse_ipv4(argv[2], &gw);
        }
        else if(strcmp(argv[1] ,"mask") == 0)
        {
            net_parse_ipv4(argv[2], &mask);
        }
        net_set_ip(&ip, &mask, &gw);
    }

    net_get_mac(mac);
    printk("eth0 MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    net_ipv4_to_str(&ip, ipbuf, sizeof(ipbuf));
    printk("IP: %s\n", ipbuf);
    net_ipv4_to_str(&mask, ipbuf, sizeof(ipbuf));
    printk("NETMASK: %s\n", ipbuf);
    net_ipv4_to_str(&gw, ipbuf, sizeof(ipbuf));
    printk("Gateway: %s\n", ipbuf);
    {
        uint16_t bsr = hal_eth_read_phy(0x01);
        printk("flags: up=%d link=%d EthStatus=0x%lx PHY_BSR=0x%04x\n",
               net_is_up(),
               net_is_link_up(),
               (unsigned long)hal_eth_get_status(),
               (unsigned)bsr);
    }
    printk("%s", SHELL_PROMPT);
}


static void dhcp_config(void *arg)
{
    char* argv[2] = {0};
	NETWORK_PARAM_T stNetworkParam = {0};

    int argc = 0;

	argc = cmdline_strtok((char*)arg,argv,2);
    if(argc < 2)
    {
        goto end;
    }

	printk("set dhcpc to %d\r\n", atoi(argv[1]));
	stNetworkParam.byDhcpEnabled = atoi(argv[1]);
	setNetworkParam(&stNetworkParam);
	devParamSave();
	setDhcpEnabled(stNetworkParam.byDhcpEnabled);
end:
    printk("DHCP status:%s\r\n", getDhcpEnabled()?"enabled":"disabled");
    printk("%s", SHELL_PROMPT);
    return;
}

#if 0
static void getDebugLevelShell(void *arg)
{
    UNUSED_ARG(arg);
    printk("current Level:%s  %d\n",log_level_str[__printf_level__],__printf_level__);
    return;
}
#endif

static void debugLevelShell(void *arg)
{
    char* argv[2] = {0};
    int argc = cmdline_strtok((char*)arg,argv,3);
    DEVINFO_PARAM_T stDevParam = {0};
    uint8_t byLevel = 0;

    if(argc < 2)
    {
        goto end;
    }

	/* TEST */
    //printf("test %d, %s %s %s", argc, argv[0], argv[1], argv[2]);
    byLevel = atoi(argv[1]);
    printf(" %s %d\n", argv[1], byLevel);
    if(DLEVEL_ALERT <= byLevel && byLevel <= DLEVEL_TRACE)
    {
        setDebugLevel(byLevel);
        if (getDevInfoParam(&stDevParam) != RET_OK) {
            memset(&stDevParam, 0, sizeof(stDevParam));
        }
        stDevParam.byDebugLevel = byLevel;
        setDevParam(&stDevParam);
        devParamSave();
    }
    else
    {
        printk("err level\n");
    }
end:
	/* DEBUG: 临时插桩，诊断 log_level_str 指针/字节与 __printf_level__（验证后移除） */
	{
		const char *dbg_p;
		int dbg_i, dbg_j;
		printk("\r\n[DBG] lvl=%u arr=%p", (unsigned)__printf_level__, (const void *)log_level_str);
		for (dbg_i = 0; dbg_i < DLEVEL_MAX; dbg_i++) {
			dbg_p = log_level_str[dbg_i];
			printk("\r\n[DBG] [%d]=%p", dbg_i, (const void *)dbg_p);
			if (dbg_p) {
				printk(" bytes");
				for (dbg_j = 0; dbg_j < 8; dbg_j++)
					printk(" %02X", (unsigned char)dbg_p[dbg_j]);
			} else {
				printk(" NULL");
			}
		}
		printk("\r\n");
	}
	/*  debug level info */
	byLevel = DLEVEL_ALERT;
	for(; byLevel < DLEVEL_MAX; byLevel++)
	{
		printk("%s:%d ", log_level_str[byLevel], byLevel);
	}
    printk("current Level:%s  %d\n",log_level_str[__printf_level__],__printf_level__);
    printk("%s", SHELL_PROMPT);
    return;
}

/* dbg: 统一模块日志开关(见 os_log.h)
 *   dbg             列出所有模块与开关
 *   dbg <mod>       查单个模块状态
 *   dbg <mod> 0|1   关/开模块 (0=整模块静默, 含错误)
 *   dbg all 0|1     一键全关/全开
 */
static void sys_log_dbg(void *arg)
{
    char *argv[3] = {0};
    int argc = 0;
    int i;
    int on = -1;

    argc = cmdline_strtok((char *)arg, argv, 3);

    /* 设开关: dbg <mod|all> <0|1> */
    if (argc >= 3) {
        on = atoi(argv[2]) ? 1 : 0;
        if (strcmp(argv[1], "all") == 0) {
            for (i = 0; i < LOG_MOD_MAX; i++) {
                g_log_mods[i].enabled = (uint8_t)on;
            }
        } else {
            for (i = 0; i < LOG_MOD_MAX; i++) {
                if (strcmp(argv[1], g_log_mods[i].name) == 0) {
                    g_log_mods[i].enabled = (uint8_t)on;
                    break;
                }
            }
            if (i >= LOG_MOD_MAX) {
                printk("dbg: unknown module '%s'\r\n", argv[1]);
                printk("%s", SHELL_PROMPT);
                return;
            }
        }
        printk("dbg: %s -> %s\r\n", argv[1], on ? "ON" : "OFF");
        printk("%s", SHELL_PROMPT);
        return;
    }

    /* 查单个: dbg <mod> */
    if (argc == 2 && strcmp(argv[1], "all") != 0) {
        for (i = 0; i < LOG_MOD_MAX; i++) {
            if (strcmp(argv[1], g_log_mods[i].name) == 0) {
                printk("%-8s %-10s %s\r\n", g_log_mods[i].name,
                       g_log_mods[i].prefix,
                       g_log_mods[i].enabled ? "ON" : "OFF");
                printk("%s", SHELL_PROMPT);
                return;
            }
        }
        printk("dbg: unknown module '%s'\r\n", argv[1]);
        printk("%s", SHELL_PROMPT);
        return;
    }

    /* dbg / dbg all: 列出全部模块 */
    printk("dbg: <mod> [0|1]  (0=静默 1=开)\r\n");
    for (i = 0; i < LOG_MOD_MAX; i++) {
        printk("  %-8s %-10s %s\r\n", g_log_mods[i].name,
               g_log_mods[i].prefix,
               g_log_mods[i].enabled ? "ON" : "OFF");
    }
    printk("%s", SHELL_PROMPT);
}


void format_uptime(uint64_t ms, char *buf, size_t buf_size)
{
    uint64_t total_seconds = ms / 1000;
    uint32_t days = total_seconds / 86400;      // 1 d = 86400 s
    total_seconds %= 86400;
    uint32_t hours = total_seconds / 3600;
    total_seconds %= 3600;
    uint32_t minutes = total_seconds / 60;
    uint32_t seconds = total_seconds % 60;

    // ???????????????????????????
    // ???????? "2d 05:34:27"
    if (days > 0) {
        snprintf(buf, buf_size, "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)seconds);
    } else {
        snprintf(buf, buf_size, "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
    }
}


static void uptime(void *arg)
{
    UNUSED_ARG(arg);
    char time_str[32] = {0};
    uint64_t ms_data = 0;

	ms_data = sys_jiffies();  // sys uptime
    format_uptime(ms_data, time_str, sizeof(time_str));
    printk("%12s: %s\r\n", "dev-uptime", time_str);

	memset_s(time_str, sizeof(time_str), 0, sizeof(time_str));
	print_timestamp(time_str);
	printk("%12s: %s", "system-time", time_str);
    printk("%s", SHELL_PROMPT);
    return;
}


uint8_t getCpuUsage(void)
{
    return os_cpu_usage();
}


/* �����CPUռ�� */
void PrintRunTimeStats(void *arg) 
{
    char *buffer = NULL;
    UNUSED_ARG(arg);
    buffer = (char *)os_malloc(1024);  // �����������ڴ洢ͳ�ƽ��
    if(NULL == buffer)
    {
       os_debug("buffer os_malloc fail!\r\n");
	   return;
    }
    memset(buffer, 0, 1024);
    os_task_runtime_stats(buffer, 1024);
    printk("%s\r\n", buffer);
    os_free(buffer);
	printk("%s", SHELL_PROMPT);
	return;
}


void CheckHeapUsage(void *arg) 
{
    /* ���ص�ǰ���õĶ��ڴ��С */
    size_t freeHeap = os_heap_free();
    /* ������ϵͳ����������С�Ŀ��ж��ڴ��С */
    size_t minFreeHeap = os_heap_min_free();
    UNUSED_ARG(arg);

    printk("Current free heap size: %u bytes\n", (unsigned)freeHeap);
    printk("Minimum free heap size ever: %u bytes\n", (unsigned)minFreeHeap);
	printk("%s", SHELL_PROMPT);
	return;
}


uint8_t getMemUsage(void)
{
    return os_heap_usage_percent();
}


void PrintStorageStats(void *arg)
{
	UNUSED_ARG(arg);
	partition_info_show();
}


void CheckTaskStackUsage(void *arg) 
{
	uint8_t i = 0;
    UBaseType_t stackHighWaterMark = 0;
    os_task_handle taskHandle = NULL;

    UNUSED_ARG(arg);

    for(i = 0; i < 32; i++)
	{
		taskHandle = (os_task_handle)sys_handle_info.sys_handle_array[i];
		if(taskHandle)
		{
			/* �ú�������ָ���������С��ջ��������Ϊ "High Water Mark"�����ֵԽС��˵������ʹ����Խ��Ķ�ջ */
    		stackHighWaterMark = os_task_stack_free(taskHandle);
			printk("%-32s stack high water mark: %u\r\n", os_task_name(taskHandle), stackHighWaterMark);
		}
	}
	return;
}

void sysSetWeb(void *arg)
{
	char* argv[2] = {0};
    int argc = 0;

	argc = cmdline_strtok((char*)arg,argv,2);
    if(argc < 2)
    {
        goto end;
    }

	printk("set web to %d\r\n", atoi(argv[1]));
	byUseBinary = atoi(argv[1]);
end:
    printk("web id:%d\r\n", byUseBinary);
    printk("%s", SHELL_PROMPT);
    return;
}

/* spi flash only */
void sys_hex_dump(void *arg)
{
	uint8 index = 0;
	uint32 dwOffset = 0;
	uint32 dwLen = 0;
	char* argv[4] = {0};
    int argc = 0;

	argc = cmdline_strtok((char*)arg,argv,4);
    if(argc < 4)
    {
        printk("\r\nusage: hex_dump <part> <offset> <len>\r\n"
               "  part: 0=app1 1=app2 2=ota 3=log 4=web 5=config 6=custom\r\n"
               "  eg:   hex_dump 4 0 64\r\n");
        printk("%s", SHELL_PROMPT);
        return;
    }
	index = atoi(argv[1]);
	dwOffset = atoi(argv[2]);
	dwLen = atoi(argv[3]);
	printk("index:%d dwOffset:%d dwLen:%d\r\n", index, dwOffset, dwLen);
	CUSTOM_ASSERT(index >= SPI_FLASH_PART_MAX, return);
	CUSTOM_ASSERT(dwOffset >= spi_flash_table[index].size, return);
	CUSTOM_ASSERT(dwLen >= spi_flash_table[index].size, return);

	hex_dump(index, dwOffset, dwLen, SPI_FLASH_DEV_ID);
	printk("%s", SHELL_PROMPT);
}

/* 读 SPI JEDEC ID */
static void sys_spi_id(void *arg)
{
    uint32_t id;
    (void)arg;
    id = hal_spi_flash_read_id();
    printk("\r\nSPI JEDEC ID=0x%06lX\r\n", (unsigned long)id);
    printk("%s", SHELL_PROMPT);
}

/* 向 web 分区写 64 字节测试图案并读回 */
static void sys_web_test(void *arg)
{
    uint8_t pat[64];
    uint8_t rd[64];
    (void)arg;

    memset(pat, 0xA5, sizeof(pat));
    memcpy(pat, "WEBTEST!", 8);
    printk("\r\nweb_test: write 64B to PART_WEB...\r\n");
    if (SPI_FLASH_WRITE(PART_WEB, 0, pat, sizeof(pat))) {
        printk("web_test WRITE FAIL\r\n");
        printk("%s", SHELL_PROMPT);
        return;
    }
    memset(rd, 0, sizeof(rd));
    SPI_FLASH_READ(PART_WEB, 0, rd, sizeof(rd));
    flash_data_print(rd, sizeof(rd));
    if (memcmp(pat, rd, sizeof(pat)) == 0) {
        printk("web_test OK (SPI write/read good)\r\n");
    } else {
        printk("web_test MISMATCH (SPI write broken)\r\n");
    }
    printk("%s", SHELL_PROMPT);
}


void sys_clear_log(void *arg)
{
	UNUSED_ARG(arg);
	SPI_FLASH_ERASE_ALL(PART_LOG);
	printk("log erase success!\r\n");
	printk("%s", SHELL_PROMPT);
}

#if defined(CONFIG_APP_DHT11)
static void sys_dht11(void *arg)
{
    DHT11_Data_TypeDef d = {0};
    uint8_t ret;
    uint8_t idle;
    int i;
    const char *stage;
    (void)arg;

    idle = DHT11_PinLevel();
    printk("\r\nDHT11 DATA=PG9 idle=%u (expect 1 if pull-up OK)\r\n",
           (unsigned)idle);
    if (0 == idle) {
        printk("WARN: line stuck LOW — check short/VCC/wrong pin\r\n");
    }

    printk("read x3, interval 1.5s...\r\n");
    for (i = 0; i < 3; i++) {
        ret = DHT11_Read_TempAndHumidity(&d);
        if (SUCCESS == ret) {
            printk("[%d] OK  RH=%u.%u%%  T=%u.%uC  raw=%02X %02X %02X %02X chk=%02X\r\n",
                   i + 1,
                   (unsigned)d.humi_int, (unsigned)d.humi_deci,
                   (unsigned)d.temp_int, (unsigned)d.temp_deci,
                   (unsigned)d.humi_int, (unsigned)d.humi_deci,
                   (unsigned)d.temp_int, (unsigned)d.temp_deci,
                   (unsigned)d.check_sum);
        } else {
            switch (d.err_stage) {
            case DHT11_ERR_NO_RESP_LOW:  stage = "no ACK low (sensor silent)"; break;
            case DHT11_ERR_NO_RESP_HIGH: stage = "ACK low ok, no high"; break;
            case DHT11_ERR_NO_DATA_START:stage = "no data bit start"; break;
            case DHT11_ERR_BIT:          stage = "bit timeout"; break;
            case DHT11_ERR_CHECKSUM:     stage = "checksum"; break;
            default:                     stage = "unknown"; break;
            }
            printk("[%d] FAIL stage=%u %s\r\n", i + 1,
                   (unsigned)d.err_stage, stage);
        }
        if (i < 2) {
            os_sleep_ms(1500);
        }
    }
    printk("hint: DATA->PG9, VCC->3.3/5V, GND->GND; module needs >1s after power\r\n");
    printk("%s", SHELL_PROMPT);
}
#endif

void sys_ping(void *arg)
{
    char* argv[4] = {0};
    int argc = 0;
    ip_addr_t ping_target;
    int ping_count = 4;
    const char* ip_address = NULL;

    argc = cmdline_strtok((char*)arg, argv, 4);

    if (argc < 2) {
        printk("usage: ping <ip> [count]\r\n");
        printk("  ping 192.168.137.1\r\n");
        printk("  ping 192.168.137.1 8\r\n");
        goto end;
    }

    ip_address = argv[1];
    if (argc >= 3) {
        ping_count = atoi(argv[2]);
        if (ping_count <= 0) {
            ping_count = 4;
        }
        if (ping_count > 20) {
            ping_count = 20;
        }
    }

    if (ipaddr_aton(ip_address, &ping_target) == 0) {
        printk("ping: invalid IP address: %s\r\n", ip_address);
        goto end;
    }

    printk("\r\n");
    ping_run(&ping_target, ping_count);

end:
    printk("%s", SHELL_PROMPT);
}

#if defined(CONFIG_APP_ESP8266)
static void sys_wifi(void *arg)
{
    char *argv[3] = {0};
    int argc;

    argc = cmdline_strtok((char *)arg, argv, 3);
    if ((argc >= 1 && 0 == strcmp(argv[0], "wifi_scan")) ||
        (argc >= 2 && 0 == strcmp(argv[1], "scan"))) {
        ESP8266_WifiScan();
    } else if (argc >= 2) {
        printk("usage: wifi\r\n");
        printk("       wifi scan\r\n");
    } else {
        ESP8266_WifiStatus();
    }
    printk("%s", SHELL_PROMPT);
}
static void sys_weather(void *arg)
{
    char weather[16] = {0};
    uint32_t ok = 0, fail = 0, last_ok_ms = 0;
    uint32_t ago_s = 0;

    ESP8266_WeatherStats(&ok, &fail, &last_ok_ms);
    getWeather(weather, sizeof(weather));

    if (last_ok_ms) {
        ago_s = (os_time() - last_ok_ms) / 1000U;
    }
    printk("weather: %s, temp=%d C\r\n", weather, getTemperature());
    printk("        ok=%lu fail=%lu last_ok=%lus ago\r\n",
           (unsigned long)ok, (unsigned long)fail, (unsigned long)ago_s);
    printk("%s", SHELL_PROMPT);
}
#endif

#if 0
/* 获取某个任务的栈使用情况 */
void CheckTaskStackUsage(uint8_t* param, void *arg) {
    UBaseType_t stackHighWaterMark = 0;
    os_task_handle taskHandle = NULL;

    UNUSED_ARG(arg);
    
    taskHandle = (TaskHandle_t)arg;

    /* �ú�������ָ���������С��ջ��������Ϊ "High Water Mark"�����ֵԽС��˵������ʹ����Խ��Ķ�ջ */
    stackHighWaterMark = uxTaskGetStackHighWaterMark(taskHandle);
    OS_SHELL_PRINTF(param,"Task stack high water mark: %u\n", stackHighWaterMark);
    SHELL_PRINTF_END();
}


void CheckHeapUsage(uint8_t* param, void *arg) {
    /* ���ص�ǰ���õĶ��ڴ��С */
    size_t freeHeap = os_heap_free();
    /* ������ϵͳ����������С�Ŀ��ж��ڴ��С */
    size_t minFreeHeap = os_heap_min_free();
    UNUSED_ARG(arg);

    OS_SHELL_PRINTF(param,"Current free heap size: %u bytes\n", freeHeap);
    OS_SHELL_PRINTF(param,"Minimum free heap size ever: %u bytes\n", minFreeHeap);
    SHELL_PRINTF_END();
}

// ���ڴ�ӡ�����б�
void printTaskList(uint8_t* param, void *arg) {

    // ��ȡ�����б��Ļ�����
    char taskListBuffer[512] = {0};
    UNUSED_ARG(arg);
    vTaskList(taskListBuffer);
    OS_SHELL_PRINTF(param,"Task List:\n%s", taskListBuffer);
    SHELL_PRINTF_END();
}
#endif

/* dts: 设备树信息/分区表/热重载 */
static void sys_dts(void *arg)
{
    char *argv[2] = {0};
    int argc = cmdline_strtok((char *)arg, argv, 2);
    const char *cmd = (argc >= 2) ? argv[1] : "info";
    const dts_ctx_t *ctx = dts_ctx();
    int i;

    if (strcmp(cmd, "reload") == 0) {
        dts_load_default();
        dts_apply_partitions();
        printk("dts reload done, src=%s model=%s\n",
               dts_source_str(), dts_model());
        printk("%s", SHELL_PROMPT);
        return;
    }

    if (strcmp(cmd, "list") == 0) {
        printk("dts %s v%u %uB src=%s nodes=%u\n",
               dts_model(), dts_version(), (unsigned)ctx->blob_len,
               dts_source_str(), (unsigned)ctx->hdr->node_cnt);
        printk("  %-10s %-10s %-10s %s\n", "name", "addr", "size", "crc");
        for (i = 1; i < ctx->hdr->node_cnt; i++) {
            const dts_node_t *n = &ctx->nodes[i];
            const char *nm = ctx->strtab + n->name_off;
            uint32_t crc = 0;
            if (n->reg_size == 0) {
                continue;
            }
            dts_prop_u32(n, "crc", &crc);
            printk("  %-10s 0x%06lX 0x%-8lX %u\n", nm,
                   (unsigned long)n->reg_addr, (unsigned long)n->reg_size,
                   (unsigned)crc);
        }
        printk("%s", SHELL_PROMPT);
        return;
    }

    /* info */
    printk("model=%s version=%u src=%s blob=%uB nodes=%u\n",
           dts_model(), dts_version(), dts_source_str(),
           (unsigned)(ctx ? ctx->blob_len : 0),
           (unsigned)(ctx ? ctx->hdr->node_cnt : 0));
    printk("%s", SHELL_PROMPT);
}

/* end */

/*******************************************************************************
??????shell_conteol_register
????????????
???????????
???????????
???????
*******************************************************************************/
void shell_conteol_register(void)
{
	//shell_register_command("version_software" 			,software_version);
	shell_register_command("check_sysclock" 				,look_time);
	shell_register_command("check_gpio" 						,check_GPIO);
	shell_register_command("check_peripheral" 			,check_peripheral);
	shell_register_command("set_gpio" 							,really_set_GPIO);

    shell_register_command("ifconfig", sys_ifconfig);
    shell_register_command("dhcp", dhcp_config);
    shell_register_command("debugLevel", debugLevelShell);
    shell_register_command("dbg", sys_log_dbg);
    shell_register_command("uptime", uptime);
    shell_register_command("reboot", sys_reboot_now);
	shell_register_command("free", CheckHeapUsage);
	shell_register_command("mem", CheckTaskStackUsage);
	shell_register_command("top", PrintRunTimeStats);
	shell_register_command("ls", PrintStorageStats);
	shell_register_command("hex_dump", sys_hex_dump);
	shell_register_command("spi_id", sys_spi_id);
	shell_register_command("web_test", sys_web_test);
	shell_register_command("clearLog", sys_clear_log);
	shell_register_command("setWeb", sysSetWeb);
	shell_register_command("ping", sys_ping);
	shell_register_command("dts", sys_dts);
#if defined(CONFIG_APP_ESP8266)
	shell_register_command("wifi", sys_wifi);
	shell_register_command("wifi_scan", sys_wifi);
	shell_register_command("weather", sys_weather);
#endif
#if defined(CONFIG_APP_DHT11)
	shell_register_command("dht11", sys_dht11);
#endif
}


