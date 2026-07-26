/************************************************
	??????USB????��???????????
	?????��??
	???????overlong
************************************************/


/************************************************
	????
************************************************/
#include "shell_control.h"
#include "lwip/netif.h"
#include "lwip/ip.h"
#include "safe_utils.h"
#include "devConfig.h"
#include "os_mutex.h"
#include "ping.h"
#include "flash_manage.h"
#include "dev_manage.h"
#include "stm32f4x7_eth.h"
#include "stm32f4x7_phy.h"
#include "bsp_spi_flash.h"
#include <string.h>
#include <stdlib.h>

extern SYS_THREAD_INFO_T sys_handle_info;
extern uint8_t byUseBinary;
extern void partition_info_show(void);
/************************************************
	??????
************************************************/
char* tab_arg[4] ={0};


/************************************************
	????
************************************************/
void aaa(void)
{
	printk("\r\nshow all command ID");
}

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
	RCC_ClocksTypeDef  get_rcc_clock;  
	RCC_GetClocksFreq(&get_rcc_clock);
	uint32_t data[] = {get_rcc_clock.SYSCLK_Frequency ,get_rcc_clock.HCLK_Frequency ,get_rcc_clock.PCLK1_Frequency ,
										 get_rcc_clock.PCLK2_Frequency  /*,get_rcc_clock.ADCCLK_Frequency*/};
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
static void software_version(void * arg)
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
		for(char i = 0 ;i < 16 ;i++){
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
	printk("\r\n?????????%s_Pin%s?%s" ,argv[1] ,argv[2] ,argv[3]);
	for(int i = 0 ;i < 4 ;i++){
		tab_arg[i] = argv[i];
	}
	shell_confirm_GPIO(&shellx ,"",set_GPIO);
}


/* command added by liu */
extern struct netif gnetif;
//extern uint8_t g_byDHCPEnabled;

static void sys_reboot_now(void *arg)
{
    UNUSED_ARG(arg);
    /* ??????reboot */
    NVIC_SystemReset();
}


static void sys_ifconfig(void *arg)
{
    ip_addr_t ipaddr = gnetif.ip_addr;
    ip_addr_t netmask = gnetif.netmask;
    ip_addr_t gw = gnetif.gw;

    char* argv[2] = {0};
    int argc = cmdline_strtok((char*)arg,argv,2);

    if(argc < 3)
    {
        goto end;
    }

    if(strcmp(argv[1] ,"ip") == 0)
    {
        ipaddr_aton(argv[2], &ipaddr);
    }
    else if(strcmp(argv[1] ,"gw") == 0)
    {
        ipaddr_aton(argv[2], &gw);
    }
    else if(strcmp(argv[1] ,"mask") == 0)
    {
        ipaddr_aton(argv[2], &netmask);
    }
    netif_set_addr(&gnetif, &ipaddr, &netmask, &gw);

end:
    /* ?????????????? */
    printk("eth0 MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
           gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);

    printk("IP: %d.%d.%d.%d\n",(uint8_t)(gnetif.ip_addr.addr),(uint8_t)(gnetif.ip_addr.addr >> 8), \
    				   (uint8_t)(gnetif.ip_addr.addr >> 16),(uint8_t)(gnetif.ip_addr.addr >> 24));
    printk("NETMASK: %d.%d.%d.%d\n",(uint8_t)(gnetif.netmask.addr),(uint8_t)(gnetif.netmask.addr >> 8), \
    				   (uint8_t)(gnetif.netmask.addr >> 16),(uint8_t)(gnetif.netmask.addr >> 24));
    printk("Gateway: %d.%d.%d.%d\n",(uint8_t)(gnetif.gw.addr),(uint8_t)(gnetif.gw.addr >> 8), \
    			           (uint8_t)(gnetif.gw.addr >> 16),(uint8_t)(gnetif.gw.addr >> 24));
    {
        extern __IO uint32_t EthStatus;
        uint16_t bsr = ETH_ReadPHYRegister(ETHERNET_PHY_ADDRESS, PHY_BSR);
        printk("flags: up=%d link=%d EthStatus=0x%lx PHY_BSR=0x%04x\n",
               netif_is_up(&gnetif) ? 1 : 0,
               netif_is_link_up(&gnetif) ? 1 : 0,
               (unsigned long)EthStatus,
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
        stDevParam.byDebugLevel = byLevel;
        setDevParam(&stDevParam);
        devParamSave();
    }
    else
    {
        printk("err level\n");
    }
end:
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
        snprintf(buf, buf_size, "%ud %02u:%02u:%02u", days, hours, minutes, seconds);
    } else {
        snprintf(buf, buf_size, "%02u:%02u:%02u", hours, minutes, seconds);
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
    TaskStatus_t *taskStatusArray;
    volatile UBaseType_t numTasks;
    uint32_t totalRunTime = 0;
    uint32_t idlePercent = 0;
	uint32_t cpuUsage = 0;

    // �Ȼ�ȡϵͳ����������
    numTasks = uxTaskGetNumberOfTasks();
    taskStatusArray = pvPortMalloc(numTasks * sizeof(TaskStatus_t));
    if (taskStatusArray == NULL) return 0; // ����ʧ��

    // ��ȡ����״̬��������ʱ��
   	numTasks = uxTaskGetSystemState(taskStatusArray, numTasks, &totalRunTime);
	/* For percentage calculations. */
	totalRunTime /= ( ( configRUN_TIME_COUNTER_TYPE ) 100U );

    for(UBaseType_t i = 0; i < numTasks; i++) {
        if(strcmp(taskStatusArray[i].pcTaskName, "IDLE") == 0) {
            if(totalRunTime > 0) {
                idlePercent = taskStatusArray[i].ulRunTimeCounter / totalRunTime;
            }
            break;
        }
    }
    //printk("idlePercent:%d\r\n", idlePercent);
    vPortFree(taskStatusArray);
	cpuUsage = 100 - idlePercent;
    return cpuUsage;
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
    vTaskGetRunTimeStats(buffer);
    printk("%s\r\n", buffer);
    os_free(buffer);
	printk("%s", SHELL_PROMPT);
	return;
}


void CheckHeapUsage(void *arg) 
{
    /* ���ص�ǰ���õĶ��ڴ��С */
    size_t freeHeap = xPortGetFreeHeapSize();
    /* ������ϵͳ����������С�Ŀ��ж��ڴ��С */
    size_t minFreeHeap = xPortGetMinimumEverFreeHeapSize();
    UNUSED_ARG(arg);

    printk("Current free heap size: %u bytes\n", freeHeap);
    printk("Minimum free heap size ever: %u bytes\n", minFreeHeap);
	printk("%s", SHELL_PROMPT);
	return;
}


uint8_t getMemUsage(void)
{
	size_t freeHeap = 0;
	uint32_t memUsage = 0;

	freeHeap = xPortGetFreeHeapSize();
	freeHeap *= 100;
	memUsage = freeHeap / (configTOTAL_HEAP_SIZE);

	//printk("memUsage:%d\r\n", memUsage);
    return (100 - memUsage);
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
    TaskHandle_t taskHandle = NULL;

    UNUSED_ARG(arg);

    for(i = 0; i < 32; i++)
	{
		taskHandle = sys_handle_info.sys_handle_array[i];
		if(taskHandle)
		{
			/* �ú�������ָ���������С��ջ��������Ϊ "High Water Mark"�����ֵԽС��˵������ʹ����Խ��Ķ�ջ */
    		stackHighWaterMark = uxTaskGetStackHighWaterMark(taskHandle);
			printk("%-32s stack high water mark: %u\r\n", pcTaskGetName(taskHandle), stackHighWaterMark);
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
    id = SPI_FLASH_ReadID();
    printk("\r\nSPI JEDEC ID=0x%06lX (expect 0x%06X)\r\n",
           (unsigned long)id, (unsigned)sFLASH_ID);
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

void sys_ping(void *arg)
{
    char* argv[4] = {0};
    int argc = 0;
    ip_addr_t ping_target;
    int ping_count = 4;  // 默认ping 4次
    int current_count = 0;
    const char* ip_address = NULL;
    int i = 0;

    argc = cmdline_strtok((char*)arg, argv, 4);
    
    if(argc < 2)
    {
        printk("Usage: ping <ip_address> [-n count]\r\n");
        printk("Example: ping 192.168.1.35 -n 4\r\n");
        printk("         ping 192.168.1.35 (default 4 times)\r\n");
        goto end;
    }

	ping_init(NULL);

    // 第一个参数是IP地址
    ip_address = argv[1];
    
    // 检查是否有-n参数
    if(argc >= 3 && strcmp(argv[2], "-n") == 0 && argc >= 4)
    {
        ping_count = atoi(argv[3]);
        if(ping_count <= 0)
        {
            printk("Invalid ping count, using default 4 times\r\n");
            ping_count = 4;
        }
    }
    else if(argc == 2)
    {
        // 只有IP地址参数，默认ping 4次
        ping_count = 4;
    }
    else
    {
        printk("Usage: ping <ip_address> [-n count]\r\n");
        goto end;
    }

    // 解析IP地址
    if(ipaddr_aton(ip_address, &ping_target) == 0)
    {
        printk("Invalid IP address: %s\r\n", ip_address);
        goto end;
    }

    printk("PING %s: 32 data bytes\r\n", ip_address);

    // 执行ping操作
    for(current_count = 0; current_count < ping_count; current_count++)
    {
        // 调用ping发送函数
        ping_send(&ping_target);
        
        // 等待响应或延时（这里可能需要根据实际的ping实现调整）
        // sys_msleep(PING_DELAY); // 如果需要间隔时间，可以取消注释
        os_sleep_ms(20);
        // 注意：这里需要根据ping的实际实现来处理响应和超时
    }

    printk("PING %s completed, sent %d packets\r\n", ip_address, ping_count);

end:
    printk("%s", SHELL_PROMPT);
    return;
}

#if 0
/* ĳ������Ķ�ջʹ����� */
void CheckTaskStackUsage(uint8_t* param, void *arg) {
    UBaseType_t stackHighWaterMark = 0;
    TaskHandle_t taskHandle = NULL;

    UNUSED_ARG(arg);
    
    taskHandle = (TaskHandle_t)arg;

    /* �ú�������ָ���������С��ջ��������Ϊ "High Water Mark"�����ֵԽС��˵������ʹ����Խ��Ķ�ջ */
    stackHighWaterMark = uxTaskGetStackHighWaterMark(taskHandle);
    OS_SHELL_PRINTF(param,"Task stack high water mark: %u\n", stackHighWaterMark);
    SHELL_PRINTF_END();
}


void CheckHeapUsage(uint8_t* param, void *arg) {
    /* ���ص�ǰ���õĶ��ڴ��С */
    size_t freeHeap = xPortGetFreeHeapSize();
    /* ������ϵͳ����������С�Ŀ��ж��ڴ��С */
    size_t minFreeHeap = xPortGetMinimumEverFreeHeapSize();
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
}


