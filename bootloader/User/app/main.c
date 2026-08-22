#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "bsp_debug_usart.h"
#include "bsp_spi_flash.h"  
#include "bsp_internalFlash.h"
#include "bsp_timer.h"

#include "os_debug.h"
#include "flash_manage.h"
#include "dev_manage.h"
#include "upgrade.h"
#include "crc.h"
#include "cmd_src.h"
#include "iap.h"

#include "bsp_led.h"

extern uint32_t dwCurrentAppAddr;


#ifndef VECT_TAB_OFFSET
#define VECT_TAB_OFFSET  0x00 /*!< Vector Table base offset field.*/ 
#endif

/* USER CODE BEGIN PFP */


/* F407 内部flash（ROM）布局如下:
 * +—————————————+——————————+——————————+————————+
 * |             |          |          |        |
 * |  bootloader | APP1     |   APP2   | unused |
 * +—————————————+——————————+——————————+————————+
 * +---32KB------+- 352KB  -+ 384KB--- + 
 * +----------384K----------+ 
 * +        sector 0~7      +         sector11 的127K
 * 内部flash除去bootloader(32KB) 和预留区域1kb(sector_11)，
 * 可供双固件使用的大小为991KB，
 * 每个固件允许使用的最大大小990/2 ==》495KB
 * 当前F407项目的APP大小约为273KB，可以支持双固件
 * 
 * 为了实现APP升级(不是在bootloader下的升级)
 * 需要设计固件切换机制，当设备运行APP1时，需要擦写APP2区域、
 * 这是因为程序运行在APP1时不能擦写当前APP所在的区域，程序是运行在ROM上的
 * 因此可以设计在预留1个flag，
 */

typedef enum { FAILED = 0, PASSED = !FAILED} TestStatus;



volatile uint32_t current_time = 0; // 当前时间计数（ms），由 SysTick_Handler 每1ms累加

volatile uint8_t flash_write_request = 0;


typedef enum{
    SPI_NOR_FLASH,
    INTERNAL_FLASH
}STORAGE_INDEX_EN;



/*0-0x600000 预留6M给系统, 划分后10M空间 */
STORAGE_PART_INFO_T spi_flash_table[] = {
    [PART_APP1]   = { "app1",       0x600000,  2 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_APP2]   = { "app2",       0x800000,  2 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_OTA]    = { "ota_info",   0xa00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_LOG]    = { "log",        0xb00000,  2 * 1024 * 1024},
    [PART_WEB]    = { "web",        0xd00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_CONFIG] = { "config",     0xe00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_CUSTOM] = { "custom",     0xf00000,  1 * 1024 * 1024}  // 预留
};

STORAGE_HW_OPS_T spi_flash_ops = {
    .hw_read  = SPI_FLASH_BufferRead,
    .hw_write = SPI_FLASH_BufferWrite,
    .hw_erase = SPI_FLASH_SectorErase,
};

#define SPIFLASH_PART_NUM (sizeof(spi_flash_table)/sizeof(spi_flash_table[0]))

STORAGE_CTRL_T g_stSpiFlashPart;

#if 1
/* 0x08000000~0x08060000 部分预留给bootloader 和固件1 */
STORAGE_PART_INFO_T internal_flash_table[] = {
    [PART_FW1] =  { "FW1",      0x08008000,  256 * 1024 }, // 固件1
    [PART_FW2] =  { "FW2",      0x08060000,  256 * 1024 }, // 固件2
    [PART_RES] =  { "ota_info", 0x080A0000,  128 * 1024 }  // 预留
};

STORAGE_HW_OPS_T internal_flash_ops = {
    .hw_read  = internal_flash_read,
    .hw_write = internal_flash_write,
    .hw_erase = internal_flash_erase,
};

#define INTERNALFLASH_PART_NUM (sizeof(internal_flash_table)/sizeof(internal_flash_table[0]))

STORAGE_CTRL_T g_stInternalFlashPart;

#endif


/* 全局管理 */
STORAGE_MANAGE_T gstFlashManage[] = {
    { "spi nor",   0xA00000,  1,  SPIFLASH_PART_NUM,      &g_stSpiFlashPart},
    { "internal",  0x100000,  1,  INTERNALFLASH_PART_NUM, &g_stInternalFlashPart},
};


/*****************************************************
 * @fn       spi_flash_region_init
 * @brief    分区初始化
 * @note     初始化全部 SPI Flash 分区控制块
 * @param    无
 * @retval   无
 *****************************************************/
void spi_flash_region_init(void)
{
    // 全片擦除（需要重新划分分区时可选）
    //SPI_FLASH_BulkErase();
    dev_register(SPI_FLASH_DEV_ID, &g_stSpiFlashPart.dev);

    FlashPartition_Init(&g_stSpiFlashPart, spi_flash_table, SPIFLASH_PART_NUM, &spi_flash_ops);
}


/*****************************************************
 * @fn       spi_flash_region_init
 * @brief    分区初始化
 * @note     初始化全部 SPI Flash 分区控制块
 * @param    无
 * @retval   无
 *****************************************************/
void internal_flash_region_init(void)
{
    // （擦除指定分区, 可选）
    //internal_flash_erase_all();
    dev_register(INTERNAL_FLASH_DEV_ID, &g_stInternalFlashPart.dev);

    FlashPartition_Init(&g_stInternalFlashPart, internal_flash_table, INTERNALFLASH_PART_NUM, &internal_flash_ops);
}


/*****************************************************
 * @fn       partition_init_all
 * @brief    分区初始化
 * @note     初始化全部 SPI Flash 分区控制块
 * @param    无
 * @retval   无
 *****************************************************/
void partition_info_show(void)
{
    int i = 0;
    int j = 0;
    STORAGE_PART_INFO_T *part = NULL;

    for(i = 0; i < sizeof(gstFlashManage)/sizeof(gstFlashManage[0]); i++)
    {
        if(gstFlashManage[i].byUsed && gstFlashManage[i].pStorageCtrl)
        {
            printf("--------------  %-8s flash table info   --------------\n", gstFlashManage[i].name);

            for(j = 0; j < gstFlashManage[i].byPartNum; j++)
            {
                CUSTOM_ASSERT(NULL == gstFlashManage[i].pStorageCtrl, return);
                part = &(gstFlashManage[i].pStorageCtrl->pPartInfo[j]);
                CUSTOM_ASSERT(NULL == part, return);
                printf("%-10s   addr:0x%08lx  size:0x%-8lx  size_used:%-6lu\n", part->name,        \
                                                                             (unsigned long)part->start_addr,  \
                                                                             (unsigned long)part->size,        \
                                                                             (unsigned long)part->size_used);

            }
            printf("----------------------------------------------------------\n\n");
        }
    }
}

/* 简单延时函数（延时单位为毫秒）
 * 直接复用 SysTick_Handler 里维护的 1ms 节拍计数 current_time，
 * 不再手工折算 SysTick->VAL，避免和中断里的节拍计数各算一套、
 * 逻辑重复还容易在中断被抢占时产生误差 */
void DelayMs(uint32_t ms) {
    uint32_t start = current_time;
    while ((uint32_t)(current_time - start) < ms);
}


//  timeout_ms: 超时时间（毫秒），0 表示非阻塞
//  返回 0 成功，1 超时（勿用 -1，返回类型是 uint8_t）
static uint8_t UART_RecvByte(uint8_t *p, uint32_t timeout_ms)
{
    uint32_t timeout = timeout_ms;

    while (timeout--) 
    {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) 
        {
            *p = (uint8_t)USART_ReceiveData(USART1);
            return 0;   // success
        }
        DelayMs(1);
    }

    return 1;  // timeout
}


/* 按键PA0  */
void KEY_GPIO_Config(void)
{
    /* 第一步: 开启GPIO的时钟  --> rcc.c --> rcc.h */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	
	/* 第二步: 定义GPIO的初始化结构体 */
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 第三步: 配置GPIO初始化结构体 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;  //无上拉下拉

    /* 第四步: 调用GPIO初始化结构体，把配置好的结构体的成员参数写入寄存器*/
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}


void CheckAndUpdateApp(void) {
    uint32_t delayCount = 10000; // 5秒

    KEY_GPIO_Config();

    // 判断是否需要更新程序（5秒）
    for (uint32_t i = 0; i < delayCount; i++) {
        // 如果按键按下
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET) {
            DelayMs(20); // 延迟20ms消抖

            // 再次判断按键状态
            if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET) {
                // 按键按下，开始更新程序
                printf("Erase app...\r\n");
								if(stm32_flash_erase(APP_START_ADDR, APP_FLASH_SIZE))
								{
										BOOTLOADER_DEBUG("stm32_flash_erase err\n");
								}
								
                // 写入APP程序
                printf("Write app...\r\n");
								flash_write_request = 1;
                break;
            }
        }

        DelayMs(1); // 延迟1ms
    }
}

/* systick反初始化
 * 注意：SysTick_IRQn == -1，是内核异常不是 NVIC IRQ。
 * 对它调用 NVIC_ClearPendingIRQ()/NVIC_SetPriority() 会算出越界地址，
 * 写总线 → IMPRECISE bus fault → HardFault，表现为 Jump 后板子假死。 */
void SysTick_Deinit(void) {
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    /* 清 SysTick 挂起位（ICSR.PENDSTCLR），不要走 NVIC_*API */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
}


void JumpToApplication(uint32_t dwCurrentAppAddr)
{
    BOOTL_PRINT(BOOT_REPORT"---------- Jump to:%08x ----------\n\n", dwCurrentAppAddr);

    /* 关闭总中断，避免跳转被中断影响 */
    __disable_irq();

    #if 0
    /* 关闭 SysTick（强制） */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 关闭 + 清除所有中断 */
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    #endif
    /* 部分外设需要反初始化 */
    USART_DeInit(DEBUG_USART);
    DMA_DeInit(DMA2_Stream5);
    SPI_I2S_DeInit(FLASH_SPI);
    SysTick_Deinit();
    TIM3_DeInit();

    /* 跳转程序 */
    if(0 == jump_app(dwCurrentAppAddr))
    {
        BOOTL_PRINT(BOOT_ERROR"jump to app err!\n", dwCurrentAppAddr);
    }
}

static void ota_flag_clear(STORAGE_CTRL_PTR pInfoStorage, uint8_t byInfoPart,
                           uint32_t dwInfoStartAddr, uint32_t dwInfoEraseSize)
{
    ota_flag_t cleared;

    memset(&cleared, 0, sizeof(cleared));
    /* 先擦再写空结构，避免半写入脏数据被当成 pending */
    pInfoStorage->stPartOps.partition_erase(pInfoStorage, byInfoPart,
                                            dwInfoStartAddr,
                                            dwInfoStartAddr + dwInfoEraseSize);
    pInfoStorage->stPartOps.partition_write(pInfoStorage, byInfoPart, 0,
                                            (uint8 *)&cleared, sizeof(cleared));
}

static int s_host_flash_override;

/* 保留 magic+active_app 供下次选槽，但清掉 pending，避免反复搬运 */
static int ota_flag_commit_done(uint8_t active_slot)
{
    ota_flag_t done;

    memset(&done, 0, sizeof(done));
    done.magic = OTA_FLAG_MAGIC;
    done.active_app = (active_slot == 1U) ? 1U : 0U;
    done.upgrade_flag = 0;
    done.state = 0xAAu;
    if (SPI_FLASH_WRITE_VERIFY(PART_OTA, 0, (uint8 *)&done, sizeof(done))) {
        BOOTL_PRINT(BOOT_ERROR"commit OTA done flag failed (pending may stick)\n");
        return -1;
    }
    return 0;
}

/*
 * flash.sh / pyocd 直烧内部 APP 后写入 PART_RES。
 * 命中则：取消 SPI pending、把 active 指到刚烧的槽、擦掉 cookie（一次性）。
 * @retval 1 已处理（跳过 SPI 搬运）  0 无 cookie
 */
static int ota_apply_host_flash_override(uint32_t *pCurrentAppAddr)
{
    ota_host_flash_t host = {0};
    ota_host_flash_t zero;
    uint8_t slot;
    uint32_t addr;

    INTERNAL_FLASH_READ(PART_RES, 0, (uint8 *)&host, sizeof(host));
    if (host.magic != OTA_HOST_FLASH_MAGIC) {
        return 0;
    }

    slot = (host.slot == 1U) ? 1U : 0U;
    addr = slot ? APP2_ADDRESS : APP1_ADDRESS;
    BOOTL_PRINT(BOOT_WARN"host flash override: skip SPI OTA, run APP%u @0x%08lx\n",
                (unsigned)(slot + 1U), (unsigned long)addr);

    (void)ota_flag_commit_done(slot);

    memset(&zero, 0, sizeof(zero));
    if (INTERNAL_FLASH_WRITE(PART_RES, (uint8 *)&zero, sizeof(zero))) {
        BOOTL_PRINT(BOOT_WARN"host flash cookie clear failed (will retry next reset)\n");
    }

    if (pCurrentAppAddr != NULL) {
        *pCurrentAppAddr = addr;
    }
    s_host_flash_override = 1;
    return 1;
}

/*****************************************************
 * @fn       fw_upgrade_from
 * @brief    OTA升级的通用实现：从 pInfoStorage 的 byInfoPart 分区读取OTA信息，
 *           校验后从 pSrcStorage 里 target_app 对应分区取出固件，
 *           搬运并校验写入内部Flash的 PART_FW1，最后清空OTA控制信息区。
 * @note     断电保护（SPI OTA / MAIN_APP）：
 *           1) 仅 magic+upgrade_flag==1 才认为待升级（拒绝擦除态 0xFF..）
 *           2) 源 CRC 通过后才擦写 APP1
 *           3) APP1 写完并 CRC+向量校验通过后，才清除 OTA 标志
 *              —— 写 APP 中途掉电：标志仍在，下次上电从 SPI 重试
 *           4) 源损坏则清除标志，避免死循环升级
 * @param    pInfoStorage        : 存放 ota_flag_t 的存储介质控制块
 *           byInfoPart          : ota_flag_t 所在分区号(相对 pInfoStorage)
 *           dwInfoEraseSize     : 升级完成后清空OTA信息区时的擦除长度
 *           pSrcStorage         : 存放待升级固件源数据的存储介质控制块
 *           byShortcutSrcPart   : 若 target_app 等于这个值，说明固件源已经
 *                                 在目标分区(PART_FW1)上，不需要搬运；
 *                                 传 0xFF 表示不存在这种情况(介质不同时不可能相同)
 *           bEraseInfoBeforeWrite : 保留参数；断电安全路径忽略“先清标志”
 *           pCurrentAppAddr     : 输出跳转偏好地址
 * @retval   0: 成功(含"无需升级"情形)  -1: 失败
 *****************************************************/
static int fw_upgrade_from(STORAGE_CTRL_PTR pInfoStorage, uint8_t byInfoPart, uint32_t dwInfoEraseSize,
                            STORAGE_CTRL_PTR pSrcStorage, uint8_t byShortcutSrcPart, uint8_t bEraseInfoBeforeWrite,
                            uint32_t *pCurrentAppAddr)
{
    ota_flag_t stOtaFlag = {0};
    uint8 *pbyFwFile = NULL;
    uint32 dwFwCrc32 = 0;
    uint8_t byOtaRegion = 0;
    uint32_t dwInfoStartAddr = 0;

    (void)bEraseInfoBeforeWrite; /* 断电安全：禁止在写 APP 前清标志 */

    CUSTOM_ASSERT(NULL == pCurrentAppAddr, goto exit);

    /* 读取 OTA信息分区数据 */
    pInfoStorage->stPartOps.partition_read(pInfoStorage, byInfoPart, 0, (uint8 *)&stOtaFlag, sizeof(ota_flag_t));

    BOOTL_PRINT(BOOT_INFO"stOtaFlag.state: %lu \n", (unsigned long)stOtaFlag.state);
    BOOTL_PRINT(BOOT_INFO"stOtaFlag.upgrade_flag: %lu \n", (unsigned long)stOtaFlag.upgrade_flag);
    BOOTL_PRINT(BOOT_INFO"stOtaFlag.target_app: %lu \n", (unsigned long)stOtaFlag.target_app);
    BOOTL_PRINT(BOOT_INFO"stOtaFlag.len: %lu \n", (unsigned long)stOtaFlag.len);
    BOOTL_PRINT(BOOT_INFO"stOtaFlag.crc32: 0x%08lx \n", (unsigned long)stOtaFlag.crc32);
    BOOTL_PRINT(BOOT_INFO"stOtaFlag.magic: 0x%08lx \n", (unsigned long)stOtaFlag.magic);

    /* 严格条件：magic + pending + 合法长度（拒绝 Flash 擦除后的 0xFF） */
    if (!(stOtaFlag.magic == OTA_FLAG_MAGIC &&
          stOtaFlag.upgrade_flag == OTA_FLAG_UPGRADE_PENDING &&
          stOtaFlag.len > 0u && stOtaFlag.len <= APP_FLASH_SIZE))
    {
        BOOTL_PRINT(BOOT_INFO"no pending OTA (magic=0x%08lx flag=%lu len=%lu)\n",
                    (unsigned long)stOtaFlag.magic,
                    (unsigned long)stOtaFlag.upgrade_flag,
                    (unsigned long)stOtaFlag.len);
        return 0;
    }

    LED_YELLOW;

    if (pSrcStorage == &g_stSpiFlashPart &&
        stOtaFlag.target_app >= pSrcStorage->byPartNum) {
        BOOTL_PRINT(BOOT_ERROR"invalid target_app=%lu (spi parts=%u)\n",
                    (unsigned long)stOtaFlag.target_app,
                    pSrcStorage->byPartNum);
        goto exit;
    }

    BOOTL_PRINT(BOOT_INFO"FW size: %lu crc:0x%08lx\n",
                (unsigned long)stOtaFlag.len, (unsigned long)stOtaFlag.crc32);

    pbyFwFile = (uint8 *)malloc(stOtaFlag.len);
    if (NULL == pbyFwFile)
    {
        BOOTL_PRINT(BOOT_ERROR"[%s:%d] malloc(%lu) failed, keep OTA flag for retry\n",
                    __FUNCTION__, __LINE__, (unsigned long)stOtaFlag.len);
        goto exit;
    }

    dwInfoStartAddr = pInfoStorage->pPartInfo[byInfoPart].start_addr;

#if (OTA_MODE == MAIN_APP)
    byOtaRegion = (uint8_t)stOtaFlag.target_app;

    pSrcStorage->stPartOps.partition_read(pSrcStorage, byOtaRegion, 0, pbyFwFile, stOtaFlag.len);
    dwFwCrc32 = crc32_checksum(pbyFwFile, stOtaFlag.len);
    if (stOtaFlag.crc32 != dwFwCrc32)
    {
        BOOTL_PRINT(BOOT_ERROR"src crc err expect=0x%08lx got=0x%08lx, clear OTA\n",
                    (unsigned long)stOtaFlag.crc32, (unsigned long)dwFwCrc32);
        /* 源已坏：清标志避免每次开机死循环搬运 */
        ota_flag_clear(pInfoStorage, byInfoPart, dwInfoStartAddr, dwInfoEraseSize);
        free(pbyFwFile);
        LED_RED;
        return 0;
    }
    BOOTL_PRINT(BOOT_INFO"fw crc check success!\n");

    /* 固件已在运行区：只清标志 */
    if (byShortcutSrcPart == byOtaRegion)
    {
        ota_flag_clear(pInfoStorage, byInfoPart, dwInfoStartAddr, dwInfoEraseSize);
        free(pbyFwFile);
        LED_GREEN;
        return 0;
    }

    /*
     * 关键：先写 APP1，校验通过后再清 OTA。
     * 写到一半掉电 → APP1 可能损坏，但 SPI 源+标志仍在，下次开机重试。
     */
    BOOTL_PRINT(BOOT_INFO"Downloading (power-fail safe):\n");
    LED_BLUE;

    INTERNAL_FLASH_WRITE(PART_FW1, pbyFwFile, stOtaFlag.len);
    *pCurrentAppAddr = APP1_ADDRESS;

    memset(pbyFwFile, 0, stOtaFlag.len);
    INTERNAL_FLASH_READ(PART_FW1, 0, pbyFwFile, stOtaFlag.len);
    dwFwCrc32 = crc32_checksum(pbyFwFile, stOtaFlag.len);

    if (stOtaFlag.crc32 != dwFwCrc32 || !app_image_valid(APP1_ADDRESS))
    {
        BOOTL_PRINT(BOOT_ERROR"APP1 verify fail crc=0x%08lx valid=%u — KEEP OTA for retry\n",
                    (unsigned long)dwFwCrc32,
                    (unsigned)app_image_valid(APP1_ADDRESS));
        LED_RED;
        /* 不清除 SPI OTA：下次复位继续搬运。若 APP2 可用则暂跳 APP2 */
        if (app_image_valid(APP2_ADDRESS)) {
            *pCurrentAppAddr = APP2_ADDRESS;
            BOOTL_PRINT(BOOT_WARN"temp boot APP2 until OTA retry succeeds\n");
        }
        free(pbyFwFile);
        return 0;
    }

    /* 成功：现在才清标志 */
    ota_flag_clear(pInfoStorage, byInfoPart, dwInfoStartAddr, dwInfoEraseSize);
    BOOTL_PRINT(BOOT_INFO"APP1 image OK, OTA cleared, boot -> 0x%08lx\n",
                (unsigned long)APP1_ADDRESS);
    LED_GREEN;
#endif

#if (OTA_MODE == DUAL_APP)
    {
        uint8_t cur = (stOtaFlag.active_app == 1U) ? 1U : 0U;
        uint8_t nxt = cur ? 0U : 1U;
        uint8_t dst_part = nxt ? PART_FW2 : PART_FW1;
        uint32_t dst_addr = nxt ? APP2_ADDRESS : APP1_ADDRESS;

        byOtaRegion = (uint8_t)stOtaFlag.target_app;
        pSrcStorage->stPartOps.partition_read(pSrcStorage, byOtaRegion, 0, pbyFwFile, stOtaFlag.len);
        dwFwCrc32 = crc32_checksum(pbyFwFile, stOtaFlag.len);
        if (stOtaFlag.crc32 != dwFwCrc32) {
            BOOTL_PRINT(BOOT_ERROR"DUAL_APP src crc err, clear OTA\n");
            ota_flag_clear(pInfoStorage, byInfoPart, dwInfoStartAddr, dwInfoEraseSize);
            LED_RED;
        } else {
            uint32_t dest_crc;

            BOOTL_PRINT(BOOT_INFO"DUAL_APP write inactive APP%u @0x%08lx (keep APP%u)\n",
                        (unsigned)(nxt + 1U), (unsigned long)dst_addr,
                        (unsigned)(cur + 1U));

            /* 目标槽已是同一镜像（上次已搬完但标志没清干净，或 pyocd 刚烧了同一份）→ 不再擦写 */
            INTERNAL_FLASH_READ(dst_part, 0, pbyFwFile, stOtaFlag.len);
            dest_crc = crc32_checksum(pbyFwFile, stOtaFlag.len);
            if (dest_crc == stOtaFlag.crc32 && app_image_valid(dst_addr)) {
                BOOTL_PRINT(BOOT_INFO"DUAL_APP dest already matches, skip copy\n");
                (void)ota_flag_commit_done(nxt);
                *pCurrentAppAddr = dst_addr;
                LED_GREEN;
            } else {
                pSrcStorage->stPartOps.partition_read(pSrcStorage, byOtaRegion, 0, pbyFwFile, stOtaFlag.len);
                LED_BLUE;
                /* 只写空闲槽；写中途掉电 → 旧槽仍可启动，标志保留可重试 */
                INTERNAL_FLASH_WRITE(dst_part, pbyFwFile, stOtaFlag.len);

                memset(pbyFwFile, 0, stOtaFlag.len);
                INTERNAL_FLASH_READ(dst_part, 0, pbyFwFile, stOtaFlag.len);
                dwFwCrc32 = crc32_checksum(pbyFwFile, stOtaFlag.len);

                if (stOtaFlag.crc32 != dwFwCrc32 || !app_image_valid(dst_addr)) {
                    BOOTL_PRINT(BOOT_ERROR"DUAL_APP verify fail @0x%08lx crc=0x%08lx — KEEP OTA\n",
                                (unsigned long)dst_addr, (unsigned long)dwFwCrc32);
                    LED_RED;
                    *pCurrentAppAddr = cur ? APP2_ADDRESS : APP1_ADDRESS;
                } else {
                    if (ota_flag_commit_done(nxt) != 0) {
                        LED_RED;
                        *pCurrentAppAddr = dst_addr;
                    } else {
                        *pCurrentAppAddr = dst_addr;
                        BOOTL_PRINT(BOOT_INFO"DUAL_APP switch -> APP%u @0x%08lx\n",
                                    (unsigned)(nxt + 1U), (unsigned long)dst_addr);
                        LED_GREEN;
                    }
                }
            }
        }
    }
#endif

    free(pbyFwFile);
    return 0;

exit:
    LED_RED;
    if (pbyFwFile)
    {
        free(pbyFwFile);
    }
    return -1;
}


int fw_upgrade(uint32_t *pCurrentAppAddr)
{
    /* OTA信息和固件源都在内部Flash: PART_RES 存OTA信息，target_app 索引的也是
     * 内部Flash分区表(INTERNAL_PART_ID_EN)；固件源可能恰好就是PART_FW1本身，
     * 清OTA信息在写新固件之前进行(擦整个PART_RES分区)，与原 fw_upgrade() 行为一致 */
    return fw_upgrade_from(&g_stInternalFlashPart, PART_RES, g_stInternalFlashPart.pPartInfo[PART_RES].size,
                            &g_stInternalFlashPart, PART_FW1, 1,
                            pCurrentAppAddr);
}


int fw_upgrade_v2(uint32_t *pCurrentAppAddr)
{
    /* OTA信息存SPI Flash的PART_OTA分区，target_app 索引的是SPI Flash分区表
     * (SPI_PART_ID_EN)，固件源介质和目标(内部Flash PART_FW1)不同，不存在"已经
     * 在目标分区"的情况(0xFF 保证shortcut分支不会命中)；清OTA信息在写新固件
     * 之后进行(只擦头部4K)，与原 fw_upgrade_v2() 行为一致 */
    return fw_upgrade_from(&g_stSpiFlashPart, PART_OTA, 0x1000,
                            &g_stSpiFlashPart, 0xFF, 0,
                            pCurrentAppAddr);
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    当前的bootloader主函数仅负责判断跳转
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int main() 
{
    uint8_t boot_count = 5;
    uint8_t ch = 0;

    SysTick_Config(SystemCoreClock / 1000);

    TIM3_init();

    /* 初始化串口 */
    Debug_USART_Config();

    /* RGB LED：OTA 时闪蓝，成功绿 / 失败红 */
    LED_GPIO_Config();
    LED_RGBOFF;

    show_boot_info();

    /* 16M串行flash W25Q128初始化 */
    SPI_FLASH_Init();

    /* SPI FLASH初始化后延时一段, 否则SPI接口可能无法正常使用 */
    DelayMs(200);

    /* 设备初始化 */
    dev_init();

    /* spi_flash分区初始化 */
    spi_flash_region_init();

    /* inter flash分区初始化 */
    internal_flash_region_init();

#if OTA_REGION_SPI_FLASH
    if (!ota_apply_host_flash_override(&dwCurrentAppAddr)) {
        fw_upgrade_v2(&dwCurrentAppAddr);
    }
#else
    fw_upgrade(&dwCurrentAppAddr);
#endif
    /* 升级流程可能已指定 fail-over 地址；再按 active_app + 镜像校验做最终选择 */
    {
        uint32_t resolved = boot_resolve_app_addr();
        if (s_host_flash_override && app_image_valid(dwCurrentAppAddr)) {
            /* pyocd 直烧：保持刚指定的槽，忽略 SPI 里残留的 active_app */
        } else if (!(dwCurrentAppAddr == APP2_ADDRESS && app_image_valid(APP2_ADDRESS))) {
            dwCurrentAppAddr = resolved;
        }
        BOOTL_PRINT(BOOT_REPORT"will jump to 0x%08lx\n", (unsigned long)dwCurrentAppAddr);
    }

    /* 无合法 APP：留在 boot CLI，避免跳进损坏镜像 */
    if (!app_image_valid(dwCurrentAppAddr)) {
        BOOTL_PRINT(BOOT_ERROR"no valid APP image, stay in bootloader CLI\n");
        LED_RED;
        mini_cli_loop();
        /* CLI 里 goto/reset 可能改地址；再试一次 */
        if (!app_image_valid(dwCurrentAppAddr)) {
            while (1) {
                DelayMs(1000);
            }
        }
    }

    while(1)
    {
        __os_printf("\rPress \"U\" or \"u\" to stay in bootloader %d...", boot_count);
        if(1 == boot_count)
        {
            __os_printf("\n");
        }

        ch = 0;
        if (0 == UART_RecvByte(&ch, 1000))
        {
            if (ch == 'U' || ch == 'u' || ch == 0x0D || ch == 0x0A)
            {
                __os_printf("\nstay in bootloader, enter CLI...\n");
                break;
            }
            /* 其它按键忽略，继续等待，不消耗倒计时 */
            continue;
        }

        /* 1s 超时：倒计时减一 */
        boot_count--;
        if (0 == boot_count)
        {
            JumpToApplication(dwCurrentAppAddr);
        }
    }

    /* mini command loop；goto/reset 会改 dwCurrentAppAddr 并退出循环 */
    mini_cli_loop();
    JumpToApplication(dwCurrentAppAddr);
    while(1)
    {
        DelayMs(1000);
    }

    return 0;
}



