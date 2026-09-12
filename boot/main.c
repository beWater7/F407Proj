/***************************************************************
 * @file    : main.c (boot)
 * @author  : LDY
 * @version : 1.0
 * @date    : 2026-08-25
 * @brief   : 固化 boot——多介质加载 loader 到 SRAM 并跳转
 *
 * 加载顺序（每个源都做 CRC32 + 尺寸校验）：
 *   1. SPI NOR  PART_LOADER      （固定主源，SPI 正常时永远用它）
 *   2. 内部Flash LOADER_BACKUP_ADDR（SWD 直烧的 loader 存这里，SPI 损坏时兜底）
 *   3. UART XMODEM-CRC           （兜底：接收 [loader_header_t][loader.bin]）
 *   4. fail-safe：直接跳内部 Flash 合法 APP1/APP2
 *   5. 全部失败：死循环等待 XMODEM
 *
 * boot 自身在内部 Flash XIP，不做任何 OTA，固化后只烧一次。
 *
 * @note   与 app/loader 侧的 flash_manage.h / loader_meta.h 保持一致。
 * @copyright Copyright (c) [2026] [LDY/STM32F407]
 ***************************************************************/
#include "stm32f4xx.h"
#include <string.h>
#include <stdio.h>
#include "bsp_debug_usart.h"
#include "bsp_spi_flash.h"
#include "bsp_led.h"
#include "crc.h"
#include "loader_meta.h"

/* 与 flash_manage.h 的 PartitionHeader 一致 */
#define PARTITION_MAGIC     0x55AA55AAu
#define PART_HEADER_SIZE    0x1000u

#define APP1_ADDRESS        0x08008000u
#define APP2_ADDRESS        0x08060000u
#define APP_FLASH_SIZE      0x40000u

typedef struct {
    uint32_t magic;
    uint32_t used_size;
    uint32_t crc;
} stage_part_hdr_t;

typedef void (*jump_cb)(void);

static volatile uint32_t g_tick = 0;

void SysTick_Handler(void)
{
    g_tick++;
}

/* reset_handler.s 无条件调用 FSMC_SRAM_Init；stage0 不使用外部 PSRAM，空实现 */
void FSMC_SRAM_Init(void) {}

static void DelayMs(uint32_t ms)
{
    uint32_t start = g_tick;
    while ((uint32_t)(g_tick - start) < ms);
}

/* ==================== 串口 ==================== */
static void uart_putc(uint8_t c)
{
    USART_SendData(DEBUG_USART, c);
    while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);
}

/* 返回 0 收到，1 超时。必须空转轮询 RXNE：DelayMs(1) 会在 115200 下丢包。 */
static uint8_t uart_getc(uint8_t *p, uint32_t timeout_ms)
{
    uint32_t start = g_tick;

    if (timeout_ms == 0u) {
        timeout_ms = 1u;
    }
    while ((uint32_t)(g_tick - start) < timeout_ms) {
        uint32_t sr = DEBUG_USART->SR;
        if (sr & USART_FLAG_ORE) {
            (void)DEBUG_USART->DR;
        }
        if (sr & USART_FLAG_RXNE) {
            *p = (uint8_t)USART_ReceiveData(DEBUG_USART);
            return 0;
        }
    }
    if (DEBUG_USART->SR & USART_FLAG_RXNE) {
        *p = (uint8_t)USART_ReceiveData(DEBUG_USART);
        return 0;
    }
    return 1;
}

/* ==================== 跳转 ==================== */
static int app_image_valid(uint32_t app_addr)
{
    uint32_t msp;
    uint32_t reset;

    if ((app_addr != APP1_ADDRESS) && (app_addr != APP2_ADDRESS)) {
        return 0;
    }
    msp = *(volatile uint32_t *)app_addr;
    reset = *(volatile uint32_t *)(app_addr + 4u);

    if (msp < 0x20000000u || msp > 0x20020000u) {
        return 0;
    }
    if ((reset & 1u) == 0u) {
        return 0;
    }
    reset &= ~1u;
    if (reset < app_addr || reset >= (app_addr + APP_FLASH_SIZE)) {
        return 0;
    }
    return 1;
}

static void jump_to_loader(void)
{
    uint32_t msp = *(volatile uint32_t *)LOADER_RAM_BASE;
    uint32_t pc  = *(volatile uint32_t *)(LOADER_RAM_BASE + 4u);

    printf("[boot] jump loader: MSP=0x%08lx PC=0x%08lx VTOR=0x%08lx\n",
           (unsigned long)msp, (unsigned long)pc, (unsigned long)LOADER_RAM_BASE);

    if (msp < 0x20000000u || msp > 0x20020000u) {
        printf("[boot] loader MSP invalid\n");
        return;
    }
    if ((pc & 1u) == 0u) {
        printf("[boot] loader PC not Thumb\n");
        return;
    }

    __disable_irq();
    SCB->VTOR = LOADER_RAM_BASE;
    __DSB();
    __ISB();
    __set_MSP(msp);
    ((jump_cb)pc)();
    while (1) {}
}

static void jump_app(uint32_t app_addr)
{
    uint32_t msp = *(volatile uint32_t *)app_addr;
    uint32_t pc  = *(volatile uint32_t *)(app_addr + 4u);

    printf("[boot] fail-safe jump APP @0x%08lx\n", (unsigned long)app_addr);
    __disable_irq();
    SCB->VTOR = app_addr;
    __DSB();
    __ISB();
    __set_MSP(msp);
    ((jump_cb)pc)();
    while (1) {}
}

/* ==================== loader 加载 ==================== */
/* 从 SPI 分区读 loader：base 为分区基址，分区头占 0x1000 */
static int loader_load_from_spi(uint32_t spi_base)
{
    stage_part_hdr_t ph;
    loader_header_t lh;
    uint32_t crc;

    SPI_FLASH_BufferRead(spi_base, (uint8_t *)&ph, sizeof(ph));
    if (ph.magic != PARTITION_MAGIC ||
        crc32_checksum((const uint8_t *)&ph, sizeof(ph) - sizeof(ph.crc)) != ph.crc) {
        printf("[boot] SPI 0x%06lx: part hdr bad magic=0x%08lx exp=0x%08lx "
               "crc=0x%08lx\n",
               (unsigned long)spi_base,
               (unsigned long)ph.magic, (unsigned long)PARTITION_MAGIC,
               (unsigned long)ph.crc);
        return -1;
    }
    if (ph.used_size < sizeof(lh) ||
        ph.used_size > PART_HEADER_SIZE + LOADER_MAX_SIZE) {
        printf("[boot] SPI 0x%06lx: used_size %lu bad (need %lu~%lu)\n",
               (unsigned long)spi_base, (unsigned long)ph.used_size,
               (unsigned long)sizeof(lh),
               (unsigned long)(PART_HEADER_SIZE + LOADER_MAX_SIZE));
        return -1;
    }

    SPI_FLASH_BufferRead(spi_base + PART_HEADER_SIZE, (uint8_t *)&lh, sizeof(lh));
    if (lh.magic != LOADER_HDR_MAGIC || lh.size == 0u || lh.size > LOADER_MAX_SIZE) {
        printf("[boot] SPI 0x%06lx: loader hdr bad magic=0x%08lx exp=0x%08lx "
               "size=%lu\n",
               (unsigned long)spi_base,
               (unsigned long)lh.magic, (unsigned long)LOADER_HDR_MAGIC,
               (unsigned long)lh.size);
        return -1;
    }

    /* 直接读到 SRAM 运行基址 */
    SPI_FLASH_BufferRead(spi_base + PART_HEADER_SIZE + sizeof(lh),
                         (uint8_t *)LOADER_RAM_BASE, lh.size);
    crc = crc32_checksum((const uint8_t *)LOADER_RAM_BASE, lh.size);
    if (crc != lh.crc32) {
        printf("[boot] SPI 0x%06lx: crc fail got=0x%08lx exp=0x%08lx\n",
               (unsigned long)spi_base, (unsigned long)crc, (unsigned long)lh.crc32);
        return -1;
    }
    printf("[boot] SPI 0x%06lx: loader ver=0x%08lx size=%lu used=%lu crc ok\n",
           (unsigned long)spi_base, (unsigned long)lh.version,
           (unsigned long)lh.size, (unsigned long)ph.used_size);
    return 0;
}

/* 从内部 Flash 备份区读 loader（loader_header_t 直接位于 addr） */
static int loader_load_from_flash(uint32_t addr)
{
    loader_header_t lh;
    uint32_t crc;

    memcpy(&lh, (const void *)addr, sizeof(lh));
    if (lh.magic != LOADER_HDR_MAGIC || lh.size == 0u || lh.size > LOADER_MAX_SIZE) {
        printf("[boot] internal 0x%08lx: loader hdr bad magic=0x%08lx "
               "exp=0x%08lx size=%lu\n",
               (unsigned long)addr,
               (unsigned long)lh.magic, (unsigned long)LOADER_HDR_MAGIC,
               (unsigned long)lh.size);
        return -1;
    }
    memcpy((void *)LOADER_RAM_BASE, (const void *)(addr + sizeof(lh)), lh.size);
    crc = crc32_checksum((const uint8_t *)LOADER_RAM_BASE, lh.size);
    if (crc != lh.crc32) {
        printf("[boot] internal 0x%08lx: crc fail\n", (unsigned long)addr);
        return -1;
    }
    printf("[boot] internal 0x%08lx: loader ver=0x%08lx size=%lu crc ok\n",
           (unsigned long)addr, (unsigned long)lh.version, (unsigned long)lh.size);
    return 0;
}

/* ==================== XMODEM-CRC 兜底 ==================== */
#define X_SOH 0x01u
#define X_EOT 0x04u
#define X_ACK 0x06u
#define X_NAK 0x15u
#define X_CAN 0x18u

/* XMODEM-CRC16：poly 0x1021, init 0x0000, MSB-first */
static uint16_t xmodem_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= (uint16_t)((*data++) << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/*
 * 接收 [loader_header_t][loader.bin] 到 dst（=LOADER_RAM_BASE）。
 * 首个 128B 块含 loader 头 + body 前 112B，其余块依次追加。
 * 成功返回 0，*out_size = loader.bin 字节数。
 */
static int xmodem_receive_loader(uint8_t *dst, uint32_t *out_size)
{
    loader_header_t hdr = {0};
    uint8_t buf[128];
    uint32_t body_len = 0;
    uint32_t blk = 0;
    uint8_t c;
    int i;
    int have_soh;

    printf("[boot] XMODEM-CRC: 等待发送 [16B loader头 + loader.bin]...\n");

    /* 握手：连发 'C' 请求 CRC 模式。首个 SOH 已读到，必须留给下面的块解析，
     * 否则会把 seq 当成下一包的起始字节，整次传输错位。 */
    have_soh = 0;
    for (i = 0; i < 40; i++) {
        uart_putc('C');
        if (uart_getc(&c, 2000) == 0 && c == X_SOH) {
            have_soh = 1;
            break;
        }
    }
    if (i >= 40) {
        printf("[boot] XMODEM 无响应\n");
        return -1;
    }

    while (1) {
        uint8_t bnum, bnum2, hi, lo;
        int data_ok;

        if (have_soh) {
            have_soh = 0;
            c = X_SOH;
        } else if (uart_getc(&c, 3000) != 0) {
            uart_putc(X_NAK);
            continue;
        }
        if (c == X_EOT) {
            uart_putc(X_ACK);
            break;
        }
        if (c == X_CAN) {
            return -1;
        }
        if (c != X_SOH) {
            continue;
        }

        if (uart_getc(&bnum, 1000) != 0 || uart_getc(&bnum2, 1000) != 0) {
            uart_putc(X_NAK);
            continue;
        }
        if (bnum != (uint8_t)(blk + 1u) || bnum2 != (uint8_t)~bnum) {
            uart_putc(X_NAK);
            continue;
        }

        data_ok = 1;
        for (i = 0; i < 128; i++) {
            if (uart_getc(&c, 1000) != 0) {
                data_ok = 0;
                break;
            }
            buf[i] = c;
        }
        if (!data_ok) {
            uart_putc(X_NAK);
            continue;
        }
        if (uart_getc(&hi, 1000) != 0 || uart_getc(&lo, 1000) != 0) {
            uart_putc(X_NAK);
            continue;
        }
        if (xmodem_crc16(buf, 128) != (uint16_t)((hi << 8) | lo)) {
            uart_putc(X_NAK);
            continue;
        }
        uart_putc(X_ACK);

        if (blk == 0u) {
            memcpy(&hdr, buf, sizeof(hdr));
            if (hdr.magic != LOADER_HDR_MAGIC || hdr.size == 0u ||
                hdr.size > LOADER_MAX_SIZE) {
                printf("[boot] XMODEM loader 头非法 magic=0x%08lx size=%lu\n",
                       (unsigned long)hdr.magic, (unsigned long)hdr.size);
                return -1;
            }
            memcpy(dst, buf + sizeof(hdr), 128u - sizeof(hdr));
            body_len = 128u - sizeof(hdr);
        } else {
            memcpy(dst + body_len, buf, 128);
            body_len += 128u;
        }
        blk++;
    }

    if (blk == 0u) {
        printf("[boot] XMODEM 未收到数据块\n");
        return -1;
    }
    if (body_len < hdr.size) {
        printf("[boot] XMODEM 长度不足 %lu < %lu\n",
               (unsigned long)body_len, (unsigned long)hdr.size);
        return -1;
    }
    if (crc32_checksum(dst, hdr.size) != hdr.crc32) {
        printf("[boot] XMODEM CRC32 校验失败\n");
        return -1;
    }
    *out_size = hdr.size;
    printf("[boot] XMODEM 接收 ok: ver=0x%08lx size=%lu\n",
           (unsigned long)hdr.version, (unsigned long)hdr.size);
    return 0;
}

/* ==================== reset reason ==================== */
/* boot is the first code that runs after ANY reset, so reading RCC_CSR here
 * tells us who reset the board last time. Read+clear so the next reset is
 * not confused with this one.
 * FLASH_OPTCR bit0 = IWDG mode: 1 = software (only app arms it),
 *                              0 = hardware (armed at power-on, cannot be
 *                                  disabled -> boot/loader hang also resets). */
static void report_reset_reason(void)
{
    uint32_t csr = RCC->CSR;
    uint32_t iwdg_sw = FLASH->OPTCR & 0x1u;

    printf("[boot] RST:");
    if (csr & RCC_CSR_LPWRRSTF) { printf(" LPWR"); }
    if (csr & RCC_CSR_WWDGRSTF) { printf(" WWDG"); }
    if (csr & RCC_CSR_WDGRSTF)  { printf(" IWDG"); }
    if (csr & RCC_CSR_SFTRSTF)  { printf(" SW");   }
    if (csr & RCC_CSR_PORRSTF)  { printf(" POR");  }
    if (csr & RCC_CSR_PADRSTF)  { printf(" NRST"); }
    if (csr & RCC_CSR_BORRSTF)  { printf(" BOR");  }
    if ((csr & 0xFE000000u) == 0u) { printf(" none"); }
    printf(" | IWDG=%s\n", iwdg_sw ? "SW" : "HW");

    RCC->CSR |= RCC_CSR_RMVF;
}

/* ==================== main ==================== */
int main(void)
{
    uint32_t loader_size = 0;
    int i;

    SysTick_Config(SystemCoreClock / 1000);
    Debug_USART_Config();
    LED_GPIO_Config();
    LED_RGBOFF;

    printf("\n===== boot %s %s =====\n", __DATE__, __TIME__);
    report_reset_reason();

    SPI_FLASH_Init();
    DelayMs(200);

    printf("[boot] spi flash id=0x%06lx\n", (unsigned long)SPI_FLASH_ReadID());
    printf("[boot] ROM: app1@0x%08lx app2@0x%08lx\n",
           (unsigned long)APP1_ADDRESS, (unsigned long)APP2_ADDRESS);
    printf("[boot] loader: spi blob@0x%06lx | flash bk@0x%08lx | xmodem\n",
           (unsigned long)LOADER_SPI_ACTIVE_BASE, (unsigned long)LOADER_BACKUP_ADDR);

    /* 1) SPI 主 loader：固定加载，失败重试 3 次（坏 CRC 可能是读到一半） */
    printf("[boot] 1) SPI PART_LOADER\n");
    for (i = 0; i < 3; i++) {
        if (loader_load_from_spi(LOADER_SPI_ACTIVE_BASE) == 0) {
            LED_GREEN;
            jump_to_loader();
        }
        DelayMs(50);
    }
    /* 2) 内部 Flash 备份：SWD 直烧的 loader 存这里，SPI 损坏时兜底 */
    printf("[boot] 2) internal flash backup\n");
    if (loader_load_from_flash(LOADER_BACKUP_ADDR) == 0) {
        LED_GREEN;
        jump_to_loader();
    }

    /* 3) UART XMODEM 兜底 */
    printf("[boot] 3) UART XMODEM\n");
    if (xmodem_receive_loader((uint8_t *)LOADER_RAM_BASE, &loader_size) == 0) {
        LED_GREEN;
        jump_to_loader();
    }

    /* 4) fail-safe：直接跳合法 APP */
    if (app_image_valid(APP1_ADDRESS)) {
        LED_BLUE;
        jump_app(APP1_ADDRESS);
    }
    if (app_image_valid(APP2_ADDRESS)) {
        LED_BLUE;
        jump_app(APP2_ADDRESS);
    }

    /* 5) 无 loader 也无 APP：循环等待 XMODEM 恢复 */
    printf("[boot] 无可用 loader/APP，进入恢复模式（XMODEM）\n");
    while (1) {
        LED_RED;
        DelayMs(500);
        LED_RGBOFF;
        DelayMs(500);
        if (xmodem_receive_loader((uint8_t *)LOADER_RAM_BASE, &loader_size) == 0) {
            LED_GREEN;
            jump_to_loader();
        }
    }

    return 0;
}
