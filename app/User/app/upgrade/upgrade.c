/**
  ******************************************************************************
  * @file    upgrade.c
  * @author  ldy
  * @version V1.0
  * @date    2024-12-1
  * @brief   实现全量/差分升级
  ******************************************************************************
  * @attention
  *
  * 实验平台:野火  STM32 F407 开发板  
  * 论坛    :http://www.firebbs.cn
  * 淘宝    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */
#include <string.h>
#include "stm32f4xx.h"
#include "./internalFlash/bsp_internalFlash.h"
#include "os_debug.h"
#include "bsp_spi_flash.h"
#if defined(CONFIG_APP_FATFS)
#include "ff.h"
#endif
#include "flash_manage.h"
#include "upgrade.h"

//extern FRESULT res_flash;                  /* 文件操作结果 */

#define FLASH_FW_START_ADDR  ADDR_FLASH_SECTOR_7
#define FLASH_FW_END_ADDR    ADDR_FLASH_SECTOR_12
/*扇区大小4K*/
#define  SPIFLASH_FW_START_ADDR     (1536+1536) //一个扇区大小为4K，3072个扇区即为偏移12MB
#define FLAG_ADDRESS ADDR_FLASH_SECTOR_11 + 124*1024              //固件写入完成后在该地址写入标志位
//#define FLAG_ADDRESS ADDR_FLASH_SECTOR_0 + 26*1024 


extern struct http_state* gs_hs[2];  //保存需要的http连接、web和固件升级请求

extern char *gs_byUserFile;

extern unsigned int crc32_checksum(const unsigned char *ptr, unsigned int len);


/**
  * @brief  InternalFlash_Test,对内部FLASH进行读写测试
  * @param  None
  * @retval None
  */
uint32_t getFlashSector(uint32 len, uint32_t dwStartSector)
{
    uint8_t bySectorCount = 0; //记录需要清空的扇区个数
    uint32_t bySectorSize = 0; //记录需要清空的扇区内存大小
    uint32_t uwEndSector = 0;

    /* 计算需要使用的扇区个数 */
    while(bySectorSize < len)
    {
        if(dwStartSector + bySectorCount <= 4)
        {
            bySectorSize += 16*1024*bySectorCount;
        }
        else if(5 == dwStartSector + bySectorCount)
        {
            bySectorSize += 64*1024*bySectorCount;
        }
        else if(dwStartSector + bySectorCount < 12)
        {
            bySectorSize += 128*1024*bySectorCount;
        }
        else
        {
            FLASH_DEBUG("fw too large! len:%lu\n", (unsigned long)len);
        }
        
        if(bySectorSize >= len)
            break;
        bySectorCount++;
    }

    FLASH_DEBUG("bySectorSize:%lu\n", (unsigned long)bySectorSize);
    bySectorCount += dwStartSector;

    /* 匹配到扇区对应的sector */
    switch (bySectorCount)
    {
        case 0:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_0);
            break;
        case 1:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_1);
            break;
        case 2:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_2);
            break;
        case 3:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_3);
            break;
        case 4:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_4);
            break;
        case 5:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_5);
            break;
        case 6:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_6);
            break;
        case 7:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_7);
            break;
        case 8:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_8);
            break;
        case 9:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_9);
            break;
        case 10:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_10);
            break;
        case 11:
            uwEndSector = GetSector(ADDR_FLASH_SECTOR_11);
            break;
        case 12:
            uwEndSector = GetSector(FLASH_FW_END_ADDR);
            break;
    }

    return uwEndSector;
}


// 打印进度条
void PrintProgressBar(uint32_t size, uint32_t total_size)
{
    int filled_length = 0;
    char bar[PROGRESS_BAR_LENGTH + 1] = {0};
    static uint8_t preProgress = 0;
    uint8_t progress = 0;
    uint8_t i = 0;

    progress = ((double)size/total_size*100.0 + 0.5);

    //printf("progress %d\n", progress);
    if(progress < 0 || progress > 100)
    {
        return;
    }

    /* 重复的直接return */
    if(preProgress == progress)
    {
        return;
    }
    preProgress = progress;

    /* 每增长5个点打印一次进度表 */
    if(0 != (progress%5))
    {
        return;
    }

    filled_length = (progress * PROGRESS_BAR_LENGTH) / 100;
    memset(bar, ' ', PROGRESS_BAR_LENGTH);
    bar[PROGRESS_BAR_LENGTH] = '\0'; // 确保字符串以'\0'结尾
    /* 循环写入 */
    for (i = 0; i < filled_length; i++)
    {
        bar[i] = '#';
    }

    // 使用\r回到行首，打印进度条和百分比
    ota_printf( "\r[%s] %d%%", bar, progress);
    if(100 == progress)
    {
        ota_printf("\n\n");
    }
    //ota_printf( "[%s] %d%%\n", bar, progress);
}


/*****************************************************
 * @fn       upgrade_write_fw
 * @brief    升级包写入内部flash
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int upgrade_write_fw(uint8 *fw, uint32 len)
{
    uint32_t dwFirstSector = 0;
    (void)dwFirstSector;
    uint32_t dwLastSector = 0;
    (void)dwLastSector;
    uint8_t  byOtaRegion = 0;
    (void)byOtaRegion;
    ota_flag_t stOtaFlag = {0};

    CUSTOM_ASSERT(NULL == fw, return -1);
    CUSTOM_ASSERT(0 == len, return -1);

#if (OTA_MODE == MAIN_APP)
    /* 读取 */
    INTERNAL_FLASH_READ(PART_RES, 0, (uint8 *)&stOtaFlag, sizeof(ota_flag_t));

    /* active_app==1 表示当前工作在APP2, 此时应擦写fw1进行升级 */
    byOtaRegion = (1 == stOtaFlag.active_app)?PART_FW1:PART_FW2;
    (void)byOtaRegion;
    //byOtaRegion = (1 == stOtaFlag.active_app)?PART_FW1:PART_FW2;
    stOtaFlag.len = len;
    stOtaFlag.crc32 = crc32_checksum(fw, len);
    stOtaFlag.target_app = byOtaRegion;
    /* 升级标志位 */
    stOtaFlag.upgrade_flag = 1;
    /* 当前运行地址 */
    stOtaFlag.active_app = byOtaRegion;
    stOtaFlag.state = UPDATE_DOWNLOAD_OK;

    os_printf("stOtaFlag.len:%d stOtaFlag.crc32:0x%08x stOtaFlag.active_app:%d\n", stOtaFlag.len, stOtaFlag.crc32, stOtaFlag.active_app);
#if 0
    dwFirstSector = GetSector(internal_flash_table[byOtaRegion].start_addr);
    (void)dwFirstSector;
    /* 固件区独占几个完整扇区大小 */
    dwLastSector = GetSector(internal_flash_table[byOtaRegion].start_addr + internal_flash_table[byOtaRegion].size);
    (void)dwLastSector;

    FLASH_Unlock();

    /* 清除各种FLASH的标志位 */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                 FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR| FLASH_FLAG_PGSERR); 

    /* 擦除固件数据 */             
    while(dwFirstSector < dwLastSector)
    {
        /* VoltageRange_3 以“字(32位)”的大小进行擦除，清除整个扇区的空间 */ 
        if (FLASH_EraseSector(dwFirstSector, VoltageRange_3) != FLASH_COMPLETE)
        {
            os_debug("FLASH_EraseSector err!\n");
            /*擦除出错，返回，实际应用中可加入处理 */
            return -1;
        }
        os_printf("[%s:%d] ota erase sector:0x%08x\n", __FUNCTION__,__LINE__,dwFirstSector);
        dwFirstSector += 8;
    }

    /* 擦除升级控制分区 */
    dwFirstSector = GetSector(internal_flash_table[PART_RES].start_addr);
    (void)dwFirstSector;
    /* VoltageRange_3 以“字(32位)”的大小进行擦除，清除整个扇区的空间 */ 
    if (FLASH_EraseSector(dwFirstSector, VoltageRange_3) != FLASH_COMPLETE)
    {
        os_debug("FLASH_EraseSector err!\n");
        /*擦除出错，返回，实际应用中可加入处理 */
        return -1;
    }

    FLASH_Lock();
#endif

    /* 分区已占用大小清0 */
    internal_flash_table[byOtaRegion].size_used = 0;
    internal_flash_table[PART_RES].size_used = 0;

    //flash_data_print((uint8_t * )(0x080a0000), 256);
    //flash_data_print(fw+92800, 256);

    /* 写入固件正文 */
    INTERNAL_FLASH_WRITE(byOtaRegion, fw, len);

    //flash_data_print((uint8_t *)0x08076A80, 256);
#endif

#if (OTA_MODE == MAIN_APP)
    /* TODO */
#endif
    /* 写入固件信息 */
    INTERNAL_FLASH_WRITE(PART_RES, (uint8 *)&stOtaFlag, sizeof(ota_flag_t));

    //flash_data_print((uint8_t * )(0x080a0000), 256);

    return 0;
}


#ifdef __GNUC__
__attribute__((naked)) uint32_t get_pc(void)
{
    __asm volatile (
        "mov r0, pc \n"
        "bx lr      \n"
    );
}
#else
__asm uint32_t get_pc(void)
{
    MOV r0, pc
    BX lr
}
#endif


/*****************************************************
 * @fn       ota_get_active_slot
 * @brief    当前运行槽：0=APP1, 1=APP2
 *****************************************************/
uint8_t ota_get_active_slot(void)
{
    ota_flag_t flag = {0};
    uint32_t pc;

    SPI_FLASH_READ(PART_OTA, 0, (uint8 *)&flag, sizeof(flag));
    if (flag.magic == OTA_FLAG_MAGIC) {
        return (flag.active_app == 1U) ? 1U : 0U;
    }

    /* 首次升级：标志未写，用 PC 判断当前链接基址 */
    pc = get_pc();
    if (pc >= APP2_ADDRESS && pc < (APP2_ADDRESS + APP_FLASH_SIZE)) {
        return 1U;
    }
    return 0U;
}

/*****************************************************
 * @fn       upgrade_write_fw_v2
 * @brief    固件写入 SPI Flash，供 boot 搬运
 * @note     DUAL_APP：调用方已选好「空闲槽」对应镜像；此处保留 active_app=当前槽
 *****************************************************/
int upgrade_write_fw_v2(uint8 *fw, uint32 len)
{
    ota_flag_t stOtaFlag = {0};
    uint8_t cur_slot = 0;

    CUSTOM_ASSERT(NULL == fw, return -1);
    CUSTOM_ASSERT(0 == len, return -1);

    /*
     * 固件落到 SPI PART_APP1；bootloader fw_upgrade_v2() 用 target_app 作为
     * SPI 分区号去读。
     */
    if (SPI_FLASH_WRITE_VERIFY(PART_APP1, 0, fw, len)) {
        os_debug("upgrade_write_fw_v2: write PART_APP1 failed\r\n");
        return -1;
    }

    memset(&stOtaFlag, 0, sizeof(stOtaFlag));
    stOtaFlag.len = len;
    stOtaFlag.crc32 = crc32_checksum(fw, len);
    stOtaFlag.target_app = PART_APP1;
#if (OTA_MODE == DUAL_APP)
    cur_slot = ota_get_active_slot();
    stOtaFlag.active_app = cur_slot; /* boot 据此写 inactive bank */
#else
    stOtaFlag.active_app = 0;
    (void)cur_slot;
#endif
    stOtaFlag.upgrade_flag = OTA_FLAG_UPGRADE_PENDING;
    stOtaFlag.state = UPDATE_DOWNLOAD_OK;
    stOtaFlag.magic = OTA_FLAG_MAGIC;

    if (SPI_FLASH_WRITE(PART_OTA, 0, (uint8 *)&stOtaFlag, sizeof(ota_flag_t))) {
        os_debug("upgrade_write_fw_v2: write PART_OTA failed\r\n");
        return -1;
    }

    os_printf("OTA ready: len=%lu crc=0x%08lx spi=APP1(%u) active_slot=%u "
              "next=APP%u size_used[app1]=%lu\r\n",
              (unsigned long)stOtaFlag.len,
              (unsigned long)stOtaFlag.crc32,
              (unsigned)PART_APP1,
              (unsigned)stOtaFlag.active_app,
              (unsigned)((stOtaFlag.active_app == 1U) ? 1U : 2U),
              (unsigned long)spi_flash_table[PART_APP1].size_used);
    return 0;
}

int upgrade_commit_ota_flag(uint32 len, uint32 crc32)
{
    ota_flag_t stOtaFlag = {0};
    uint8_t cur_slot = 0;

    CUSTOM_ASSERT(0 == len, return -1);

    memset(&stOtaFlag, 0, sizeof(stOtaFlag));
    stOtaFlag.len = len;
    stOtaFlag.crc32 = crc32;
    stOtaFlag.target_app = PART_APP1;
#if (OTA_MODE == DUAL_APP)
    cur_slot = ota_get_active_slot();
    stOtaFlag.active_app = cur_slot;
#else
    stOtaFlag.active_app = 0;
    (void)cur_slot;
#endif
    stOtaFlag.upgrade_flag = OTA_FLAG_UPGRADE_PENDING;
    stOtaFlag.state = UPDATE_DOWNLOAD_OK;
    stOtaFlag.magic = OTA_FLAG_MAGIC;

    if (SPI_FLASH_WRITE(PART_OTA, 0, (uint8 *)&stOtaFlag, sizeof(ota_flag_t))) {
        os_debug("upgrade_commit_ota_flag: write PART_OTA failed\r\n");
        return -1;
    }

    spi_flash_table[PART_APP1].size_used = len;
    os_printf("OTA stream commit: len=%lu crc=0x%08lx active_slot=%u\r\n",
              (unsigned long)len, (unsigned long)crc32,
              (unsigned)stOtaFlag.active_app);
    return 0;
}


#define MAX_WRITE_SIZE 65535

void WriteLargeDataToFlash(uint8_t* pData, uint32_t WriteAddr, uint32_t DataSize) {
    while (DataSize > 0) {
        uint16_t WriteSize = (DataSize > SPI_FLASH_PageSize) ? SPI_FLASH_PageSize : DataSize;

        SPI_FLASH_BufferWrite(WriteAddr, pData, WriteSize);

        WriteAddr += WriteSize;
        pData += WriteSize;
        DataSize -= WriteSize;
    }
}


uint8_t SPI_FLASH_ReadStatusRegister(void) {
    SPI_FLASH_CS_LOW();
    SPI_FLASH_SendByte(0x05); // 读取状态寄存器命令
    uint8_t status = SPI_FLASH_SendByte(0xFF);
    SPI_FLASH_CS_HIGH();
    return status;
}

#define SPIx ((SPI_TypeDef *)SPI1_BASE)

void Check_SPI_Registers(void) {
    printf("CR1: 0x%04X\n", SPIx->CR1);
    printf("CR2: 0x%04X\n", SPIx->CR2);
    printf("SR:  0x%04X\n", SPIx->SR);
    printf("DR:  0x%04X\n", SPIx->DR);
    printf("CRCPR: 0x%04X\n", SPIx->CRCPR);
    printf("RXCRCR: 0x%04X\n", SPIx->RXCRCR);
    printf("TXCRCR: 0x%04X\n", SPIx->TXCRCR);
    printf("I2SCFGR: 0x%04X\n", SPIx->I2SCFGR);
    printf("I2SPR: 0x%04X\n", SPIx->I2SPR);
}



