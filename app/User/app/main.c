#include "hal_board.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_flash.h"
#include "hal_eth.h"
#include "hal_cpu.h"
//#include "ff.h"
#include "lwip/tcp.h"
#include "netconf.h"
//#include "myTaskSchedule.h"
#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/dhcp.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
//#include "Protothreads/pt.h"
#include "shell.h"
#include "shell_func.h"
#include "rx_data_queue.h"
#include "flash_manage.h"
#include "os_debug.h"
#include "os_log.h"
//#include "lwip/ping.h"
#include "devConfig.h"
#include "dev_manage.h"
#include "shell_control.h"
#if defined(CONFIG_APP_DHT11)
#include "drv_dht11.h"
#endif
#include "log.h"
#include "export.h"
#include "os_mutex.h"
#include "FreeRTOSConfig.h"
#if defined(CONFIG_APP_MULTIBUTTON)
#include "multi_button.h"
#endif
#if defined(CONFIG_APP_ESP8266)
#include "drv_esp8266.h"
#endif
#if defined(CONFIG_APP_USB_HOST)
#include "usb_host.h"
#endif
#include "crc.h"
#include "httpd.h"
#include "dts.h"
#if defined(CONFIG_APP_TELNET)
#include "telnet.h"
#endif
#include "lwip/tcpip.h"
#include "netconf.h"
#include "sntp_api.h"
#include "os_task.h"
#include "upgrade.h"
#include "malloc.h"

#define SOFT_VERSION_LOCAL "V2.0.0"

/* ?????????, ????????????, ???��???????????????????????????? */

#define FREERTOS_HEAP_USE_EXRAM  1 /* 1=外部PSRAM堆(web/OTA大块分配) 0=内部SRAM */


/* FreeRTOS heap_4：放 PSRAM，由 os_malloc(=pvPortMalloc) 分配 web/OTA 等大块 */
#if FREERTOS_HEAP_USE_EXRAM
__attribute__((aligned(8))) __EXRAM uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
#else
__attribute__((aligned(8))) uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
#endif

//__EXRAM uint8_t g_byDHCPEnabled = 0;

static uint8_t g_byDHCPEnabled = 0;

volatile uint32_t LocalTime = 0; /* this variable is used to create a time reference incremented by 10ms */

extern struct netif gnetif;

#ifdef USE_DHCP
extern volatile uint8_t DHCP_state;
#endif

uint8_t byWebUpgrade = 1;
os_sem_t eth_rx_sem;
#if defined(CONFIG_APP_MULTIBUTTON)
static Button btn1, btn2;
#endif

/* ??????? */
os_queue_t MQTT_Data_Queue = NULL;
/* MQTT ?????????????? */
#if defined(CONFIG_APP_DHT11)
DHT11_Data_TypeDef DHT11_Data;
#endif
#define  MQTT_QUEUE_LEN    4   /* ???��???????????????????? */
#define  MQTT_QUEUE_SIZE   4   /* ??????????????��?????? */

/* Private function prototypes -----------------------------------------------*/
void user_app_init();

const char* app_logo_calvin_s =
"\n"
" ________  ________  ________   \n"
"|\\   __  \\|\\   __  \\|\\   __  \\  \n"
"\\ \\  \\|\\  \\ \\  \\|\\  \\ \\  \\|\\  \\ \n"
" \\ \\   __  \\ \\   ____\\ \\   ____\\\n"
"  \\ \\  \\ \\  \\ \\  \\___|\\ \\  \\___|\n"
"   \\ \\__\\ \\__\\ \\__\\    \\ \\__\\   \n"
"    \\|__|\\|__|\\|__|     \\|__|   \n"
"                                \n";

typedef enum{
    SPI_NOR_FLASH,
    INTERNAL_FLASH
}STORAGE_INDEX_EN;


/*0x000000~0x020000 归 loader（主区+staging），0x600000 起为 app 区 */
STORAGE_PART_INFO_T spi_flash_table[] = {
    [PART_LOADER]   = { "loader",    0x000000,  64 * 1024,  PART_CRCCHECK_EN},
    [PART_LOADER_BK]= { "loader_bk", 0x010000,  64 * 1024,  PART_CRCCHECK_EN},
    [PART_APP1]   = { "app1",       0x600000,  2 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_APP2]   = { "app2",       0x800000,  2 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_OTA]    = { "ota_info",   0xa00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_LOG]    = { "log",        0xb00000,  2 * 1024 * 1024},
    [PART_WEB]    = { "web",        0xd00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_CONFIG] = { "config",     0xe00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    /* dtb 保留区 0xF00000~0xF03FFF (主槽/备份槽 各4KB) 归 dts 模块, 不占分区索引;
     * custom 从 0xF04000 起, 大小 1MB - 16KB (与 f407zg.dts 一致) */
    [PART_CUSTOM] = { "custom",     0xf04000,  (1 * 1024 * 1024) - (16 * 1024),  0}
};

STORAGE_HW_OPS_T spi_flash_ops = {
    .hw_read  = hal_spi_flash_read,
    .hw_write = hal_spi_flash_write,
    .hw_erase = hal_spi_flash_erase_sector,
};

#define SPIFLASH_PART_NUM (sizeof(spi_flash_table)/sizeof(spi_flash_table[0]))

STORAGE_CTRL_T g_stSpiFlashPart;


#if 1
/* 0x08000000~0x08060000 ?????????bootloader ????1 */
STORAGE_PART_INFO_T internal_flash_table[] = {
    [PART_FW1] =  { "FW1",      0x08008000,  256 * 1024 }, // ???1
    [PART_FW2] =  { "FW2",      0x08060000,  256 * 1024 }, // ???2
    [PART_RES] =  { "ota_info", 0x080A0000,  128 * 1024 }  // ??????????
};


STORAGE_HW_OPS_T internal_flash_ops = {
    .hw_read  = hal_int_flash_read,
    .hw_write = hal_int_flash_write,
    .hw_erase = hal_int_flash_erase,
};

#define INTERNALFLASH_PART_NUM (sizeof(internal_flash_table)/sizeof(internal_flash_table[0]))

STORAGE_CTRL_T g_stInternalFlashPart;
#endif


/* ?????? */
STORAGE_MANAGE_T gstFlashManage[] = {
    { "spi nor",   0xA00000,  1,  SPIFLASH_PART_NUM,      &g_stSpiFlashPart},
    { "internal",  0x100000,  1,  INTERNALFLASH_PART_NUM, &g_stInternalFlashPart},
};


#if STACK_TRACE_DETECT
extern int __initial_sp;
extern int __stack_base;
extern int __heap_base;
extern int __heap_limit;

/* ?????????? */
const int s_magic = 0x43218765;
/* ??????????? */
const int h_magic = 0x56781234;

int stack_set_guard(void)
{
    hal_cpu_irq_disable();

    int* msp = (int *)hal_cpu_get_msp();
    int* base = &__stack_base;

    printf("%p, %p\n", msp, base);
    if(msp < base) {
    		hal_cpu_irq_enable();
    		return -1;
    }
    		

    for( ; base != msp; base++)
    		*base = s_magic;

    hal_cpu_irq_enable();

    return (uint32_t)msp - (uint32_t)&__stack_base;
}


int stack_detect_guard(void)
{
    hal_cpu_irq_disable();

    int* msp = (int *)hal_cpu_get_msp();
    int* base = &__stack_base;

    if(msp < base || *base != s_magic) {
    		hal_cpu_irq_enable();
    		return -1;
    }

    for( ; base != msp; base++) {
    		if(*base != s_magic)
    				break;
    }

    hal_cpu_irq_enable();

    return (uint32_t)base - (uint32_t)&__stack_base;
}
#endif

volatile uint32_t idle_counter = 0;

void vApplicationIdleHook(void)
{
    // ????????????????????????????
    idle_counter++;
    /* 看门狗在空闲任务里喂：任何忙等循环都必须让出 CPU 让 idle 运行 */
    hal_board_feed_watchdog();
}

/* ????????????? */
void Delay(volatile uint32_t nCount)
{
    for(; nCount != 0; nCount--);
}


void setDhcpEnabled(uint8_t byEnabled)
{
    /*TODO ?????????? */
    g_byDHCPEnabled = byEnabled;
}


uint8_t getDhcpEnabled(void)
{
    /*TODO ?????????? */
    return g_byDHCPEnabled;
}


int setNetworkParam(NETWORK_PARAM_T *p)
{
    if (p == NULL) {
        return -1;
    }
    setDhcpEnabled(p->byDhcpEnabled);
    return 0;
}


void setWebUpgrade(uint8_t byStatus)
{
    /*TODO ?????????? */
    byWebUpgrade = byStatus;
}


uint8_t getWebUpgrade(void)
{
    /*TODO ?????????? */
    return byWebUpgrade;
}

/*****************************************************
 * @fn       spi_flash_region_init
 * @brief    ?????????
 * @note     ???????? SPI Flash ?????????
 * @param    ??
 * @retval   ??
 *****************************************************/
void spi_flash_region_init(void)
{
    //SPI_FLASH_BulkErase();
    dev_register(SPI_FLASH_DEV_ID, &g_stSpiFlashPart.dev);

    /* SPI 与 bootloader 约定：每分区前 4K 管理头，size_used 写回 Flash。
     * 以前 byManage 默认为 0，写入不偏移、不更新头 → boot 读 APP1 偏移错位且 used_size=0，OTA 不生效。 */
    g_stSpiFlashPart.byManage = 1;

    /* DTS: 分区表优先读 SPI flash 中的动态 dtb, 失败回落固件内置静态表。
     * 表内 addr/size/flags 会被 dts 覆盖; dts 未描述的分区保持代码默认。 */
    dts_load_default();
    dts_apply_partitions();

    FlashPartition_Init(&g_stSpiFlashPart, spi_flash_table, SPIFLASH_PART_NUM, &spi_flash_ops);
}


/*****************************************************
 * @fn       spi_flash_region_init
 * @brief    ?????????
 * @note     ???????? SPI Flash ?????????
 * @param    ??
 * @retval   ??
 *****************************************************/
void internal_flash_region_init(void)
{
    // ?????????????, ?????
    //internal_flash_erase_all();
    //flash_data_print((uint8_t *)0x080A0000, 128);
    dev_register(INTERNAL_FLASH_DEV_ID, &g_stInternalFlashPart.dev);

    FlashPartition_Init(&g_stInternalFlashPart, internal_flash_table, INTERNALFLASH_PART_NUM, &internal_flash_ops);
}



/*****************************************************
 * @fn       partition_init_all
 * @brief    ?????????
 * @note     ???????? SPI Flash ?????????
 * @param    ??
 * @retval   ??
 *****************************************************/
void partition_info_show(void)
{
    int i = 0;
    int j = 0;
    uint32_t calc_crc = 0;
    STORAGE_PART_INFO_T *part = NULL;
    PartitionHeader hdr = {0};

    /* 从分区管理头刷新 size_used（PartitionRead 读的是数据区，不能用来读头） */
    if (g_stSpiFlashPart.byManage) {
        for(i = 0; i < SPI_FLASH_PART_MAX; i++)
        {
            hal_spi_flash_read(spi_flash_table[i].start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));

            calc_crc = crc32_checksum((uint8_t *)&hdr, sizeof(hdr) - sizeof(hdr.crc));

            if ((hdr.magic == PARTITION_MAGIC) && (hdr.crc == calc_crc))
            {
               spi_flash_table[i].size_used = hdr.used_size;
            }
        }
    }

    for(i = 0; i < sizeof(gstFlashManage)/sizeof(gstFlashManage[0]); i++)
    {
        if(gstFlashManage[i].byUsed && gstFlashManage[i].pStorageCtrl)
        {
            __os_printf("\n--------------  %-8s flash table info   --------------\n", gstFlashManage[i].name);

            for(j = 0; j < gstFlashManage[i].byPartNum; j++)
            {
                CUSTOM_ASSERT(NULL == gstFlashManage[i].pStorageCtrl, return);
                part = &(gstFlashManage[i].pStorageCtrl->pPartInfo[j]);
                CUSTOM_ASSERT(NULL == part, return);
                __os_printf("%-8s   addr:0x%08lx  size:0x%-8lx  size_used:%-6lu\n", part->name,    \
                    (unsigned long)part->start_addr, (unsigned long)part->size, (unsigned long)part->size_used);

            }
            __os_printf("----------------------------------------------------------\n\n");
        }
    }
}


static void shell_task(void *arg)
{
    QUEUE_DATA_TYPE *rx_data;

    (void)arg;
    FOREVER {
        rx_data = cbRead((QueueBuffer *)&rx_queue);
        if (rx_data != NULL && rx_data->head != NULL && rx_data->len > 0) {
            shell_input(&shellx, (char *)rx_data->head, (int)rx_data->len);
            rx_data->len = 0;
            cbReadFinish((QueueBuffer *)&rx_queue);
        } else {
            os_sleep_ms(20);
        }
    }
}


static void Network_task(void *arg)
{
    uint8_t flag = 1;
    uint8_t diag_cnt = 0;
    FOREVER
    {
        /* FreeRTOS 路径原先不调 LwIP_Periodic_Handle，链路后插无法恢复 */
        hal_eth_check_link();
        if (diag_cnt < 5)
        {
            #if 0
            uint16_t bsr = hal_eth_read_phy(0x01);
            printf("ETH diag: up=%d link=%d EthStatus=0x%lx BSR=0x%04x IP=%d.%d.%d.%d\n",
                   netif_is_up(&gnetif) ? 1 : 0,
                   netif_is_link_up(&gnetif) ? 1 : 0,
                   (unsigned long)hal_eth_get_status(),
                   (unsigned)bsr,
                   (int)ip4_addr1(&gnetif.ip_addr), (int)ip4_addr2(&gnetif.ip_addr),
                   (int)ip4_addr3(&gnetif.ip_addr), (int)ip4_addr4(&gnetif.ip_addr));
            #endif
            diag_cnt++;
        }
#ifdef USE_DHCP
        if(g_byDHCPEnabled && flag)
        {
            os_printf(KERN_REPORT"dhcp start...\r\n");
            dhcp_start(&gnetif);
            flag = 0;
        }
        else if(!g_byDHCPEnabled)
        {
            dhcp_stop(&gnetif);
            flag = 1;
        }
#endif
        os_sleep(2);
    }
}


uint8_t key1_input_func(void) {
    return hal_key_is_pressed(HAL_KEY_1);
}

uint8_t key2_input_func(void) {
    return hal_key_is_pressed(HAL_KEY_2);
}

uint32_t time_input_func(void) {
    return (uint32_t)xTaskGetTickCount();
}

/* key_sm 状态机未合入时：任务体空转，避免未定义类型阻断编译 */
#if 0
void key1_up_callback(void) { printf("KEY1 up\r\n"); }
void key1_down_callback(void) { printf("KEY1 down\r\n"); }
void key1_click_callback(void) { printf("KEY1 click\r\n"); }
void key1_long_callback(void) { printf("KEY1 long press\r\n"); }
void key1_double_click_callback(void) { printf("KEY1 double click\r\n"); }
void key1_idle_callback(void) { printf("KEY1 idle\r\n"); }

void key2_up_callback(void) { printf("KEY2 up\r\n"); }
void key2_down_callback(void) { printf("KEY2 down\r\n"); }
void key2_click_callback(void) { printf("KEY2 click\r\n"); }
void key2_long_callback(void) { printf("KEY2 long press\r\n"); }
void key2_double_click_callback(void) { printf("KEY2 double click\r\n"); }
void key2_idle_callback(void) { printf("KEY2 idle\r\n"); }

static void key_event_task(void *arg)
{
    key_sm_t key1_sm;
    key_sm_t key2_sm;
    key_sm_init(&key1_sm, NULL, NULL, key1_click_callback, 
                key1_long_callback, key1_double_click_callback, NULL,
                key1_input_func, time_input_func);

    key_sm_init(&key2_sm, key2_up_callback, key2_down_callback, key2_click_callback, 
                key2_long_callback, key2_double_click_callback, key2_idle_callback,
                key2_input_func, time_input_func);
    FOREVER
    {
        key_sm_proc(&key1_sm);
        key_sm_proc(&key2_sm);
        os_sleep_ms(200);
    }
}
#endif /* key_sm */


static void ETH_CheckFrameReceived_task(void *arg)
{
    // os_sem_init(eth_rx_sem);
    // if (NULL == eth_rx_sem)
    // {
    //     os_debug("eth_rx_sem create failed!\n");
    // }
    while(1) 
    {
        #if 1
        hal_eth_check_frame_received();

        /* ??????????????????????????????��????CPU???
         * pdMS_TO_TICKS(ms): ms ????> ????
         * vTaskDelay: ?????????????
         */
        os_sleep_ms(1);
        #else
        // ????��????????1?????????
        if (os_sem_take(eth_rx_sem, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            hal_eth_check_frame_received(); // ???????????????
        }
        else
        {
            // ??? -> ?????????��???????
        }
        #endif
    }
}


static void sysInfo_task(void *arg) __attribute__((unused));
static void sysInfo_task(void *arg)
{
    while(1)
    {
        print_timestamp(NULL);
        /* ??????????????????????????????��????CPU???
         * pdMS_TO_TICKS(ms): ms ????> ????
         * vTaskDelay: ?????????????
         */
        os_sleep(5);
    }
}


/**
  * @brief  TIM3 由 hal_board_f4.c 统一封装（hal_board_delay_ms）
  */


void hard_fault_handler_c(uint32_t *stack)
{
    uint32_t r0  = stack[0];
    uint32_t r1  = stack[1];
    uint32_t r2  = stack[2];
    uint32_t r3  = stack[3];
    uint32_t r12 = stack[4];
    uint32_t lr  = stack[5];
    uint32_t pc  = stack[6];  // ????????????
    uint32_t psr = stack[7];

    printf("HardFault Detected!\n");
    printf("R0 : 0x%08lX\n", (unsigned long)r0);
    printf("R1 : 0x%08lX\n", (unsigned long)r1);
    printf("R2 : 0x%08lX\n", (unsigned long)r2);
    printf("R3 : 0x%08lX\n", (unsigned long)r3);
    printf("R12: 0x%08lX\n", (unsigned long)r12);
    printf("LR : 0x%08lX (return address)\n", (unsigned long)lr);
    printf("PC : 0x%08lX (fault address)\n", (unsigned long)pc);
    printf("PSR: 0x%08lX\n", (unsigned long)psr);

    // ??? FreeRTOS ???????
    if (os_scheduler_running())
    {
        os_task_handle current_task = os_task_self();
        printf("Current Task: %s\n", os_task_name(current_task));
    }

    /* IWDG 会在 ~8s 后兜底复位；这里主动复位以缩短停机时间 */
    printf("HardFault: rebooting in 3s...\n");
    {
        volatile uint32_t d;
        for (d = 0; d < 3000000U; d++) {
        }
    }
    hal_board_reboot();
    while (1);  // 复位失败才走到这里
}


/*****************************************************
 * @fn       F407?????????
 * @brief    
 * @note     ${3:???????? SPI Flash ?????????}
 * @param    ${4:??}
 * @retval   ${5:??}
 *****************************************************/
/*****************************************************
 * @fn       board_iwdg_init
 * @brief    独立看门狗：HardFault/死循环时 ~8s 自动复位
 * @note     已迁移至 hal_board_f4.c（hal_board_init 内统一初始化）
 *****************************************************/

/* ==================== reset reason ==================== */
/* boot/loader already read+cleared RCC_CSR, so this normally prints "none":
 * it proves no reset happened between loader handing over and app start.
 * FLASH_OPTCR bit0 = IWDG mode: 1 = software (armed by hal_board_init below),
 * 0 = hardware (armed at power-on and cannot be disabled, so a hung loader or
 * a boot stage without a watchdog feeder also gets reset). */
static void app_report_reset_reason(void)
{
    uint32_t csr = RCC->CSR;
    uint32_t iwdg_sw = FLASH->OPTCR & 0x1u;

    LOGR(LOG_MOD_SYS, "RST:%s%s%s%s%s%s%s%s | IWDG=%s\r\n",
         (csr & RCC_CSR_LPWRRSTF) ? " LPWR" : "",
         (csr & RCC_CSR_WWDGRSTF) ? " WWDG" : "",
         (csr & RCC_CSR_WDGRSTF)  ? " IWDG" : "",
         (csr & RCC_CSR_SFTRSTF)  ? " SW"   : "",
         (csr & RCC_CSR_PORRSTF)  ? " POR"  : "",
         (csr & RCC_CSR_PADRSTF)  ? " NRST" : "",
         (csr & RCC_CSR_BORRSTF)  ? " BOR"  : "",
         ((csr & 0xFE000000u) == 0u) ? " none" : "",
         iwdg_sw ? "SW" : "HW");

    RCC->CSR |= RCC_CSR_RMVF;   /* clear so the next reset is unambiguous */
}

void board_init(void)
{
    /* 串口/LED/按键/时基/RTC/PSRAM/SPI Flash/看门狗 等硬件初始化 */
    hal_board_init();

    LOGR(LOG_MOD_SYS, "board bsp init success %s\r\n", __DATE__);
    app_report_reset_reason();

    return;
}


/*****************************************************
 * @fn       network_init
 * @brief    ${2:?????????}
 * @note     ${3:???????? SPI Flash ?????????}
 * @param    ${4:??}
 * @retval   ${5:??}
 *****************************************************/
void network_init()
{
    /*????????????��???? (GPIOs, clocks, MAC, DMA)
     *??????????????????��?, ??????��??????, ????
     */
    hal_eth_config();

    /* ?????��?????tcpip_thread */
    tcpip_init(NULL, NULL);

    /* ?????????, added by ldy */
    lwip_netif_init();

    /* ????????????????????????????freeRTOS???????��?????128(??????) */
    os_task_spawn("check_Ethframe_task", ETH_CheckFrameReceived_task, NULL, 256, TASK_PRIORITY_HIGH);

    LOGR(LOG_MOD_SYS, "eth driver init\r\n");
}


void network_app_init(void)
{
#if defined(CONFIG_APP_SNTP)
    sntp_api_init();
#endif
    /* webserver Init */
    httpd_init();
    /* 勿用 setWebUpgrade(1)：空分区会把垃圾当 web 头解析并刷爆串口。
     * 仅在 upgrade_web() 写成功后置位。 */

#if defined(CONFIG_APP_TELNET)
    telnet_server_init();
#endif

    LOGR(LOG_MOD_SYS, "network app init\r\n");
}


/* GCC: Reset_Handler 直接调 main；Keil: 用 $Sub$$main 拦截 main。
 * 无论哪种工具链，都必须先 board_init（串口/TIM3），再起 FreeRTOS，
 * 否则 --gc-sections 会把未引用的 BSP 初始化整段丢掉，printf 全无输出。 */
#if !(defined(__CC_ARM) || defined(__ARMCC_VERSION))
static int app_main(void);
#endif

static void main_task_entry(void *arg)
{
    (void)arg;
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
    extern int $Super$$main(void);
    $Super$$main();
#else
    app_main();
#endif
}

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
int $Sub$$main(void)
#else
int main(void)
#endif
{
    board_init();
    hal_cpu_irq_enable();

    if (os_task_spawn("main", main_task_entry, NULL, 512, 4) == NULL) {
        printf("create main task fail!\r\n");
        while (1) { }
    }

    os_scheduler_start();
    os_debug("vTaskStartScheduler returned!\r\n");
    while (1) {
        hal_board_delay_ms(100);
    }
}
/* test code for mutex */    
#if 0
os_mutex_t lock;
int counter = 0;

// ??????????????
void busy_work(void) {
    for (volatile int i = 0; i < 1000000; i++); // ??? CPU????????
}
static void
task1(void *arg)
{
    while(1) {

        os_mutex_lock(lock);           // ????
                busy_work();
                taskYIELD();
        counter++;
        printf("Task1: counter = %d\n", counter);
        os_mutex_unlock(lock);         // ????
        os_sleep_ms(10);                // ??? 100ms
    }
}


static void
task2(void *arg)
{
    while(1) {

        os_mutex_lock(lock);           // ????
                busy_work();
                taskYIELD();
        counter++;
        printf("Task2: counter = %d\n", counter);
        os_mutex_unlock(lock);         // ????
        os_sleep_ms(10);                // ??? 100ms
    }
}
#endif
void getBuildDate(char *buildInfo)
{
    uint8 year = 0, mon = 0, day = 0;
    const char *monthStr = NULL;
    (void)monthStr;
    const char *buildDate = __DATE__; // ???????: "Oct 19 2025"
    char monthStrBuf[4] = {0};
    int fullYear = 0;
    int i = 0;

    // ?????��???3???????
    strncpy(monthStrBuf, buildDate, 3);
    monthStrBuf[3] = '\0';

    // ????��???????????
    const char *months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };

    for (i = 0; i < 12; i++)
    {
        if (strcmp(monthStrBuf, months[i]) == 0)
        {
            mon = i + 1;
            break;
        }
    }

    // ??????????
    sscanf(buildDate + 4, "%hhu %hhu", &day, &year); 
    // ?????year?????????????????????? __DATE__ ?? "Oct 19 2025"

    // ????????????????

    sscanf(buildDate + 7, "%d", &fullYear);
    year = fullYear % 100; // ?????��????2025??25??

    if(buildInfo)
    {
        snprintf(buildInfo, 16, "build%02u%02u%02u\r\n", year, mon, day);
    }
    else
    {
        os_printf("build time: %04u%02u%02u %s  VTOR=0x%08lx %s\n",
                  fullYear, mon, day, __TIME__,
                  (unsigned long)hal_cpu_get_vtor(),
                  (hal_cpu_get_vtor() == 0x08060000UL) ? "(APP2)" : "(APP1)");
    }
}


#if defined(CONFIG_APP_DHT11)
uint8_t DH11_read(DHT11_Data_TypeDef *DH11_data)
{
    return DHT11_Read_TempAndHumidity(DH11_data);
}


/**********************************************************************
  * @ ??????  ?? Test1_Task
  * @ ????????? Test1_Task????????
  * @ ????    ??   
  * @ ?????  ?? ??
  ********************************************************************/
static void mqtt_data_send_task(void* parameter)
{	
    uint8_t res = 0;
    os_status_t qret;
    DHT11_Data_TypeDef* send_data = NULL;
    while (1)
    {
        /* DHT11 内部已关中断采样，勿再包一层过长 critical */
        res = DHT11_Read_TempAndHumidity(&DHT11_Data);
        send_data = &DHT11_Data;
        if(SUCCESS == res)
        {
            printf("humidity = %.1f%% , temperature = %.1fC\n",
                    DHT11_Data.humidity, DHT11_Data.temperature);
            qret = os_queue_send(MQTT_Data_Queue, /* ??????��??? */
                                 &send_data,       /* ???????????? */
                                 0 );              /* ?????? 0 */
            if(xReturn == pdTRUE)
            {
                os_printf(KERN_REPORT"MQTT data send to Queue suc!\r\n");
            }
        }

        hal_led_toggle(HAL_LED_1);
        os_sleep(1);
    }
}


#endif /* CONFIG_APP_DHT11 */

#if defined(CONFIG_APP_ESP8266)
static void esp8266_recv_task(void* parameter)
{
    /* 启动后先处理 Init 置的 pending：拉高 CH_PD、AT 连 AP。
     * 不能在 ESP8266_Init() 里同步 JoinAP，会长时间占住 app_main。 */
    FOREVER
    {
        ESP8266_ProcessPendingWifiReconfig();
        /* 先排空接收帧（含上一次请求的 +IPD），再决定是否发起新一轮天气请求 */
        ESP8266_CheckRecvDataTest();
        ESP8266_WeatherPoll();
        os_sleep_ms(200);
    }
}
#endif /* CONFIG_APP_ESP8266 */

/*----------------DRIVER---------------- */
INIT_EXPORT(dev_init, EXPORT_DRIVER);
INIT_EXPORT(network_init, EXPORT_DRIVER);


/*----------------MIDWARE---------------- */
INIT_EXPORT(network_app_init, EXPORT_MIDWARE);
INIT_EXPORT(spi_flash_region_init, EXPORT_MIDWARE);
INIT_EXPORT(internal_flash_region_init, EXPORT_MIDWARE);


/*----------------DEVICE module---------------- */
/* 同 level 按函数名字母序：devCfg_init → devPara_init → log_init */
INIT_EXPORT(log_init, EXPORT_DEVICE);
INIT_EXPORT(devCfg_init, EXPORT_DEVICE);
INIT_EXPORT(devPara_init, EXPORT_DEVICE);


/*----------------APP---------------- */
INIT_EXPORT(shell_init_all, EXPORT_APP);


#if defined(CONFIG_APP_MULTIBUTTON)
// Hardware abstraction layer function
// This simulates reading GPIO states
uint8_t read_button_gpio(uint8_t button_id)
{
    switch (button_id) {
        case 1:
            //return btn1_state;
            return hal_key_is_pressed(HAL_KEY_1);
        case 2:
            //return btn2_state;
            return hal_key_is_pressed(HAL_KEY_2);
        default:
            return 0;
    }
}

// Callback functions for button 1
void btn1_single_click_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("🔘 Button 1: Single Click\n");
}

void btn1_double_click_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("🔘🔘 Button 1: Double Click\n");
}

void btn1_long_press_start_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("⏹️ Button 1: Long Press Start\n");
}

void btn1_long_press_hold_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("⏸️ Button 1: Long Press Hold...\n");
}

void btn1_press_repeat_handler(Button* btn)
{
    printf("🔄 Button 1: Press Repeat (count: %d)\n", button_get_repeat_count(btn));
}

// Callback functions for button 2
void btn2_single_click_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("🔵 Button 2: Single Click\n");
}

void btn2_double_click_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("🔵🔵 Button 2: Double Click\n");
}

void btn2_press_down_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("⬇️ Button 2: Press Down\n");
}

void btn2_press_up_handler(Button* btn)
{
    (void)btn;  // suppress unused parameter warning
    printf("⬆️ Button 2: Press Up\n");
}

// Initialize buttons
void buttons_init(void)
{
    // Initialize button 1 (active high for simulation)
    button_init(&btn1, read_button_gpio, 1, 1);
    
    // Attach event handlers for button 1
    button_attach(&btn1, BTN_SINGLE_CLICK, btn1_single_click_handler);
    button_attach(&btn1, BTN_DOUBLE_CLICK, btn1_double_click_handler);
    button_attach(&btn1, BTN_LONG_PRESS_START, btn1_long_press_start_handler);
    button_attach(&btn1, BTN_LONG_PRESS_HOLD, btn1_long_press_hold_handler);
    button_attach(&btn1, BTN_PRESS_REPEAT, btn1_press_repeat_handler);
    
    // Initialize button 2 (active high for simulation)
    button_init(&btn2, read_button_gpio, 1, 2);
    
    // Attach event handlers for button 2
    button_attach(&btn2, BTN_SINGLE_CLICK, btn2_single_click_handler);
    button_attach(&btn2, BTN_DOUBLE_CLICK, btn2_double_click_handler);
    button_attach(&btn2, BTN_PRESS_DOWN, btn2_press_down_handler);
    button_attach(&btn2, BTN_PRESS_UP, btn2_press_up_handler);
    
    // Start button processing
    button_start(&btn1);
    button_start(&btn2);
}

#endif /* CONFIG_APP_MULTIBUTTON */


#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
int main(void)
#else
static int app_main(void)
#endif
{
    uint32_t vtor = hal_cpu_get_vtor();
    uint32_t slot;

    if (vtor == APP2_ADDRESS) {
        slot = 2;
    } else if (vtor == APP1_ADDRESS) {
        slot = 1;
    } else {
        slot = 0; /* 未知 */
    }
    os_printf("VTOR=0x%08lX slot=APP%lu  fn=0x%08lX\r\n",
           (unsigned long)vtor,
           (unsigned long)slot,
           (unsigned long)(uint32_t)(void *)app_main);
    xplat_run();
    /* ????shell cmd ???? */
    //sys_thread_new("sysInfo_task", sysInfo_task, NULL, 128, TASK_PRIORITY_NORMAL);

#if defined(CONFIG_APP_MULTIBUTTON)
    buttons_init();
#endif

    {
        os_task_handle h;
        h = os_task_spawn("network_task", Network_task, NULL, 256, TASK_PRIORITY_NORMAL);
        ota_register_background_task(h);
    }

    //sys_thread_new("key_event", key_event_task, NULL, 128, TASK_PRIORITY_NORMAL);

    // sysInfo 任务暂不启用
#if 0 /* test code for mutex */
    os_mutex_init(lock);
    /* ????shell cmd ???? */
    sys_thread_new("task1", task1, NULL, 128, 3);
    sys_thread_new("task2", task2, NULL, 128, 3);
#endif
    //hex_dump(PART_CONFIG, 0, 128, SPI_FLASH_DEV_ID);

    /* MQTT test start */
    /* ????Test_Queue */
    MQTT_Data_Queue = os_queue_create(MQTT_QUEUE_LEN, MQTT_QUEUE_SIZE);/* ??????�� */
    if(NULL == MQTT_Data_Queue)
    {
        os_debug("create MQTT_Data_Queue fail!\r\n");
    }
#if 0 /* MQTT test */
    mqtt_thread_init();

    sys_thread_new("MQTT_send", mqtt_data_send_task, NULL, 256, TASK_PRIORITY_ABOVE_NORMAL);
#endif

#if 1 /* 串口 shell：从 rx_queue 取数交给 shell_input */
    {
        os_task_handle h;
        h = os_task_spawn("shell_task", shell_task, NULL, 384, TASK_PRIORITY_NORMAL);
        ota_register_background_task(h);
    }
#endif
    LOGR(LOG_MOD_SYS, "APP init success\r\n");
    getBuildDate(NULL);
    LOGR(LOG_MOD_SYS, "soft version:%s\r\n", SOFT_VERSION_LOCAL);

#if defined(CONFIG_APP_ESP8266)
	ESP8266_Init();
    os_task_spawn("esp8266_recv", esp8266_recv_task, NULL, 1024, TASK_PRIORITY_NORMAL);
#endif

#if defined(CONFIG_APP_USB_HOST)
	usb_host_init();
    os_task_spawn("usb_host", usb_host_task, NULL, 1024, TASK_PRIORITY_NORMAL);
#endif
    // SPI_FLASH_ERASE_ALL(PART_WEB);

    // os_sleep(5);
    // SPI_FLASH_SectorErase(0xf01000);
    // hex_dump(PART_CUSTOM, 4096, 1024, SPI_FLASH_DEV_ID);
    // uint32 test = 0x11223344;
    // SPI_FLASH_WRITE(PART_CUSTOM, 0, (uint8 *)&test, 4);
    // //SPI_FLASH_BufferWrite(0xf01000, (uint8 *)&test, 4);
    // hex_dump(PART_CUSTOM, 4096, 1024, SPI_FLASH_DEV_ID);


    os_task_exit();
    return 0;
}


