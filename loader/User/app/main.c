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
#include "loader_meta.h"
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

/* xfer 在 boot/loader 初始化期间就会发 U；1 字节 USART FIFO 会溢出，
 * 所以在 DelayMs 里顺手收 RX，倒计时前就能决定留下。 */
static uint8_t s_listen_stay;
static uint8_t s_stay_loader;


typedef enum{
    SPI_NOR_FLASH,
    INTERNAL_FLASH
}STORAGE_INDEX_EN;



/*0x000000~0x020000 归 loader（主区+staging），0x600000 起为 app 区 */
STORAGE_PART_INFO_T spi_flash_table[] = {
    [PART_LOADER]   = { "loader",    0x000000,  64 * 1024,            PART_CRCCHECK_EN},
    [PART_LOADER_BK]= { "loader_bk", 0x010000,  64 * 1024,            PART_CRCCHECK_EN},
    [PART_APP1]   = { "app1",       0x600000,  2 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_APP2]   = { "app2",       0x800000,  2 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_OTA]    = { "ota_info",   0xa00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_LOG]    = { "log",        0xb00000,  2 * 1024 * 1024},
    [PART_WEB]    = { "web",        0xd00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    [PART_CONFIG] = { "config",     0xe00000,  1 * 1024 * 1024,  PART_CRCCHECK_EN},
    /* dtb 保留区 0xF00000~0xF03FFF (主槽 0xF00000 / 备份槽 0xF01000, 各4KB)
     * 属 boot contract, 由 dts 模块自己管理, 不占 loader 分区索引;
     * 故 custom 从 0xF04000 起, 大小为 1MB - 16KB */
    [PART_CUSTOM] = { "custom",     0xf04000,  (1 * 1024 * 1024) - (16 * 1024)}  // 预留
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


/* DTB 保留区:0xF00000~0xF03FFF(主槽/备槽 各4KB), 归 dts 模块自行管理,
 * 不占 loader 分区索引, 分区表内仅作布局占位展示 */
#define PART_DTS_BASE   0x00F00000UL
#define PART_DTS_SIZE   (16UL * 1024)

/* 十进制可读尺寸:整 MiB -> "2M"; 整/余 KiB -> "158K"; 不足 1KiB -> "28B" */
static void part_fmt_size(uint32_t bytes, char *buf, size_t len)
{
    if (bytes == 0) {
        snprintf(buf, len, "0B");
    } else if (bytes % (1024UL * 1024) == 0) {
        snprintf(buf, len, "%luM", (unsigned long)(bytes / (1024UL * 1024)));
    } else if (bytes >= 1024) {
        snprintf(buf, len, "%luK", (unsigned long)(bytes / 1024));
    } else {
        snprintf(buf, len, "%luB", (unsigned long)bytes);
    }
}

/* 分区显示名:loader/loader_bk/boot 保留约定小写(与工具链命名一致),
 * 其余按大写展示; FW1/FW2 追加标注其对应的 SPI 槽位 */
static void part_disp_name(const char *name, char *buf, size_t len)
{
    size_t i = 0;
    int keep_lower = 0;
    char c;

    if (!name) {
        snprintf(buf, len, "?");
        return;
    }
    if (!strcmp(name, "FW1")) {
        snprintf(buf, len, "FW1(APP1)");
        return;
    }
    if (!strcmp(name, "FW2")) {
        snprintf(buf, len, "FW2(APP2)");
        return;
    }
    if (!strcmp(name, "loader") || !strcmp(name, "loader_bk") ||
        !strcmp(name, "boot")) {
        keep_lower = 1;
    }
    while (name[i] && i + 1 < len) {
        c = name[i];
        if (!keep_lower && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
}

/*****************************************************
 * @fn       partition_info_show
 * @brief    打印内部Flash(含stage0 boot区)与 SPI NOR 的完整分区布局
 * @note     须在两个 region_init 之后调用：SPI 分区 used 来自分区管理头
 *           （FlashPartition_Init 已把 header.used_size 刷入 size_used）
 *           HDR 列含义:CRC = 分区数据前带 0x1000 分区管理头(magic/used/crc),
 *           loader 靠它校验并得知实际 used; '-' = 裸分区, 无管理头
 * @param    无
 * @retval   无
 *****************************************************/
void partition_info_show(void)
{
    int j = 0;
    STORAGE_PART_INFO_T *part = NULL;
    char idbuf[8], namebuf[16], hxbuf[16], sizbuf[16], usedbuf[16];
    const char *sep = "--------------------------------------------------";

    printf("\n================ Flash Partition ================\n");

    /* ---- 内部 Flash:XIP 绝对地址; boot 为 stage0 固化区, 不占索引 ---- */
    printf("\n[Internal Flash] 1MB XIP\n");
    printf("%s\n", sep);
    printf("ID  NAME       BASE        SIZE       USED    HDR\n");
    printf("%s\n", sep);
    /* stage0 固化 boot 区固定一行展示 */
    snprintf(hxbuf, sizeof(hxbuf), "%08lX", (unsigned long)BOOT_START_ADDR);
    part_fmt_size(BOOT_FLASH_SIZE, sizbuf, sizeof(sizbuf));
    printf("%-4s%-11s%-12s%-11s%-8s%s\n", "-", "boot", hxbuf, sizbuf, "-", "-");
    for (j = 0; j < (int)g_stInternalFlashPart.byPartNum; j++) {
        part = &g_stInternalFlashPart.pPartInfo[j];
        snprintf(idbuf, sizeof(idbuf), "%d", j);
        part_disp_name(part->name, namebuf, sizeof(namebuf));
        snprintf(hxbuf, sizeof(hxbuf), "%08lX", (unsigned long)part->start_addr);
        part_fmt_size(part->size, sizbuf, sizeof(sizbuf));
        printf("%-4s%-11s%-12s%-11s%-8s%s\n",
               idbuf, namebuf, hxbuf, sizbuf, "-", "-");
    }
    printf("%s\n", sep);

    /* ---- SPI NOR:偏移 = 片内地址; hdr=CRC 表示数据前有分区管理头 ---- */
    printf("\n\n[SPI NOR Flash]\n");
    printf("%s\n", sep);
    printf("ID  NAME       OFFSET      SIZE       USED    HDR\n");
    printf("%s\n", sep);
    for (j = 0; j < (int)g_stSpiFlashPart.byPartNum; j++) {
        part = &g_stSpiFlashPart.pPartInfo[j];
        /* DTB 保留区在 loader 分区索引之外, 于 custom 前插占位行 */
        if (part->name && !strcmp(part->name, "custom")) {
            snprintf(hxbuf, sizeof(hxbuf), "%08lX", (unsigned long)PART_DTS_BASE);
            part_fmt_size(PART_DTS_SIZE, sizbuf, sizeof(sizbuf));
            printf("%-4s%-11s%-12s%-11s%-8s%s\n",
                   "-", "DTS", hxbuf, sizbuf, "-", "-");
        }
        snprintf(idbuf, sizeof(idbuf), "%d", j);
        part_disp_name(part->name, namebuf, sizeof(namebuf));
        snprintf(hxbuf, sizeof(hxbuf), "%08lX", (unsigned long)part->start_addr);
        part_fmt_size(part->size, sizbuf, sizeof(sizbuf));
        if (part->byManage && part->size_used) {
            part_fmt_size(part->size_used, usedbuf, sizeof(usedbuf));
        } else {
            strcpy(usedbuf, "-");
        }
        printf("%-4s%-11s%-12s%-11s%-8s%s\n",
               idbuf, namebuf, hxbuf, sizbuf, usedbuf,
               part->byManage ? "CRC" : "-");
    }
    printf("%s\n", sep);
    printf("\nTotal:\n");
    printf("  Internal Flash : 1MB\n");
    printf("  SPI NOR        : 16MB\n");
    printf("\n==================================================\n");
}

/* 简单延时函数（延时单位为毫秒）
 * 直接复用 SysTick_Handler 里维护的 1ms 节拍计数 current_time，
 * 不再手工折算 SysTick->VAL，避免和中断里的节拍计数各算一套、
 * 逻辑重复还容易在中断被抢占时产生误差 */
static void loader_poll_stay_byte(void)
{
    uint8_t c;

    if (!s_listen_stay || s_stay_loader) {
        return;
    }
    if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) {
        c = (uint8_t)USART_ReceiveData(USART1);
        if (c == 'U' || c == 'u' || c == 0x0D || c == 0x0A) {
            s_stay_loader = 1;
        }
    }
}

void DelayMs(uint32_t ms) {
    uint32_t start = current_time;
    while ((uint32_t)(current_time - start) < ms) {
        loader_poll_stay_byte();
    }
}


//  timeout_ms: 超时时间（毫秒），0 表示非阻塞
//  返回 0 成功，1 超时（勿用 -1，返回类型是 uint8_t）
//  注意：必须紧轮询（不能每次 DelayMs(1)）。115200 波特下 7 字节 "upgrade"
//  在 ~0.6ms 内全部到达，若每 1ms 才查一次 RXNE，两次轮询间多字节到达
//  会触发 USART overrun（单字节 DR 被覆盖），连续帧必然丢字节。
static void uart_dwt_cyccnt_enable(void)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk))
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    }
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
    {
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

static uint8_t UART_RecvByte(uint8_t *p, uint32_t timeout_ms)
{
    uint32_t start;
    uint32_t wait_cycles;

    uart_dwt_cyccnt_enable();
    start = DWT->CYCCNT;
    wait_cycles = timeout_ms * (SystemCoreClock / 1000u);

    while ((DWT->CYCCNT - start) < wait_cycles)
    {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET)
        {
            *p = (uint8_t)USART_ReceiveData(USART1);
            return 0;   // success
        }
    }

    return 1;  // timeout
}

/* 倒计时阶段直接收 upgrade 命令：主机不必再抢 U 窗口。
 * 用前缀匹配区分手动按 'u' 和主机发的 "upgrade"（首字节同为 u）。
 * 返回:
 *   1 = 完整 "upgrade" 已匹配 → 调用方进入 YMODEM 下载
 *   0 = 该字节是 "upgrade" 前缀 → 调用方继续收下一字节（内部保留前缀）
 *   2 = U/u/CR/LF 且不是 upgrade 前缀 → 调用方进入 CLI
 *   3 = 其它垃圾字符（已清缓冲）→ 调用方忽略继续倒计时 */
static char s_upg_buf[12];
static uint8_t s_upg_len;

static void cli_reset_upg_cmd(void)
{
    s_upg_len = 0;
    s_upg_buf[0] = '\0';
}

static int cli_poll_upgrade_cmd(uint8_t c)
{
    static const char s_upg_cmd[] = "upgrade";

    if (c == 0x0D || c == 0x0A) {
        s_upg_len = 0;
        s_upg_buf[0] = '\0';
        return 2;
    }
    if (s_upg_len >= sizeof(s_upg_buf) - 1) {
        s_upg_len = 0;
    }
    s_upg_buf[s_upg_len] = (char)c;
    s_upg_buf[s_upg_len + 1] = '\0';
    s_upg_len++;

    if (strcmp(s_upg_buf, s_upg_cmd) == 0) {
        cli_reset_upg_cmd();
        return 1;
    }
    if (strncmp(s_upg_buf, s_upg_cmd, s_upg_len) == 0) {
        return 0;   /* 仍是前缀 */
    }
    /* 不再可能是 upgrade */
    s_upg_len = 0;
    if (c == 'U' || c == 'u') {
        return 2;
    }
    return 3;
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
extern char __vector_base;

int main() 
{
    uint8_t boot_count = 5;
    uint8_t ch = 0;

    /* SystemInit 默认把 VTOR 指向 0x08000000；SRAM 变体(link_ram.ld)需指回 RAM 里的向量表，
     * 否则中断会取错向量。__vector_base 由链接脚本定义在 .isr_vector 起始处 */
    SCB->VTOR = (uint32_t)&__vector_base;

    SysTick_Config(SystemCoreClock / 1000);
    /* boot 跳转前 __disable_irq()；SRAM 变体必须在 SysTick 配好后再开中断，
     * 否则 DelayMs() 等 current_time 永远不动，卡在 SPI_FLASH_Init 之后。 */
    __enable_irq();

    TIM3_init();

    /* 初始化串口 */
    Debug_USART_Config();
    s_listen_stay = 1;

    /* RGB LED：OTA 时闪蓝，成功绿 / 失败红 */
    LED_GPIO_Config();
    LED_RGBOFF;

    /* 野火 KEY1=PA0 WK_UP：按下为高。按住再复位 → 留在 loader CLI */
    KEY_GPIO_Config();

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

    /* 打印 ROM(内部Flash) + SPI 完整分区布局(在进入 OTA 流程前) */
    partition_info_show();

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
        s_listen_stay = 0;
        mini_cli_loop();
        /* CLI 里 goto/reset 可能改地址；再试一次 */
        if (!app_image_valid(dwCurrentAppAddr)) {
            while (1) {
                DelayMs(1000);
            }
        }
    }

    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET) {
        s_stay_loader = 1;
        BOOTL_PRINT(BOOT_REPORT"KEY1 held, stay in loader CLI\n");
    }

    /* 倒计时改为由 UART_RecvByte 统一收字节（含 "upgrade" 前缀匹配）。
     * 必须关掉 s_listen_stay：否则 UART_RecvByte 里每次 DelayMs(1) 都会触发
     * loader_poll_stay_byte 抢读 RXNE，把 "upgrade" 字节拆散、'u' 还会被误置
     * s_stay_loader，导致主机发的 upgrade 永远匹配不上，只能进 CLI。 */
    s_listen_stay = 0;

    while(1)
    {
        if (s_stay_loader) {
            __os_printf("\nstay in bootloader, enter CLI...\n");
            break;
        }
        __os_printf("\rPress \"U\" or \"u\" to stay in bootloader %d...", boot_count);
        if(1 == boot_count)
        {
            __os_printf("\n");
        }

        ch = 0;
        if (0 == UART_RecvByte(&ch, 1000))
        {
            uint8_t first = ch;

            /* 倒计时直接收 upgrade：xfer 不用抢 U 窗口，也不留残留输入。
             * "upgrade" 首字节是 'u'，与手动按 U 冲突，用前缀 + 短超时区分：
             * 完整匹配 → 下载；前缀在途 → 等下一字节；孤立 'u' → 进 CLI。 */
            for (;;) {
                int r = cli_poll_upgrade_cmd(ch);

                if (r == 1) {
                    __os_printf("\nupgrade received in countdown, enter YMODEM download\n");
                    s_listen_stay = 0;
                    cmd_update(NULL);
                    goto cli_entry;
                }
                if (r == 2) {
                    __os_printf("\nstay in bootloader, enter CLI...\n");
                    goto cli_entry;
                }
                if (r == 3) {
                    break;   /* 垃圾字符：回倒计时 */
                }
                /* r == 0：前缀在途，等后续字节（给足一帧余量） */
                ch = 0;
                if (0 == UART_RecvByte(&ch, 500)) {
                    continue;
                }
                /* 前缀没凑成 upgrade：孤立 'u' 按手动进 CLI，其它忽略 */
                cli_reset_upg_cmd();
                if (first == 'u' || first == 'U') {
                    __os_printf("\nstay in bootloader, enter CLI...\n");
                    goto cli_entry;
                }
                break;
            }
            continue;
        }

        if (s_stay_loader) {
            __os_printf("\nstay in bootloader, enter CLI...\n");
            break;
        }

        /* 1s 超时：倒计时减一 */
        boot_count--;
        if (0 == boot_count)
        {
            JumpToApplication(dwCurrentAppAddr);
        }
    }

cli_entry:
    s_listen_stay = 0;

    /* mini command loop；goto/reset 会改 dwCurrentAppAddr 并退出循环 */
    mini_cli_loop();
    JumpToApplication(dwCurrentAppAddr);
    while(1)
    {
        DelayMs(1000);
    }

    return 0;
}



