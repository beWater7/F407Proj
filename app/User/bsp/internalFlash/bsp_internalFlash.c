/**
  ******************************************************************************
  * @file    bsp_internalFlash.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   内部FLASH读写测试范例
  ******************************************************************************
  * @attention
  *
  * 实验平台:野火  STM32 F407 开发板  
  * 论坛    :http://www.firebbs.cn
  * 淘宝    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */

#include "bsp_internalFlash.h"
#include "flash_manage.h"
#include "os_debug.h"
#include <stdio.h>



/*准备写入的测试数据*/
#define DATA_32                 ((uint32_t)0x00000000)
#define OS_1KB  1024

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* 要擦除内部FLASH的起始地址 */
#define FLASH_USER_START_ADDR   ADDR_FLASH_SECTOR_8   
/* 要擦除内部FLASH的结束地址 */
#define FLASH_USER_END_ADDR     ADDR_FLASH_SECTOR_12  

uint8_t g_bySectorMap = 0;

#define SECTOR_7_ERASED  (1)
#define SECTOR_8_ERASED  (1 << 1)
#define SECTOR_9_ERASED  (1 << 2)
#define SECTOR_10_ERASED (1 << 3)
#define SECTOR_11_ERASED (1 << 4)
#define SECTOR_12_ERASED (1 << 5)


/* FLASH读写测试结果 */
#define  TEST_ERROR    -1   /* 错误（擦除、写入错误） */
#define  TEST_SUCCESS  0    /* 成功 */
#define  TEST_FAILED   1    /* 失败 */


#define BufferSize 6

uint16_t usFlashWriteBuf[BufferSize] = {0x0101,0x0202,0x0303,0x0404,0x0505,0x0606};
uint16_t usFlashReadBuf[BufferSize] = {0};


typedef struct{
    uint8_t bySector;       //扇区标号
    uint32_t dwSectorAddr;  //扇区地址
    uint8_t byHeaderSize;
    uint32_t dwUsedSize; //已使用区域大小 
}INTERNAL_FLASH_INFO;


INTERNAL_FLASH_INFO g_stInterFlashInfo[2] = {
    {7,    sizeof(PartitionHeader)},
    {9,    sizeof(PartitionHeader)},
};



/**
  * @brief  InternalFlash_Test,对内部FLASH进行读写测试
  * @param  None
  * @retval None
  */
int InternalFlash_Test(void)
{
	/*要擦除的起始扇区(包含)及结束扇区(不包含)，如8-12，表示擦除8、9、10、11扇区*/
	uint32_t uwStartSector = 0;
	uint32_t uwEndSector = 0;
	
	uint32_t uwAddress = 0;
	uint32_t uwSectorCounter = 0;

	__IO uint32_t uwData32 = 0;
	__IO uint32_t uwMemoryProgramStatus = 0;
	
  /* FLASH 解锁 ********************************/
  /* 使能访问FLASH控制寄存器 */
  FLASH_Unlock();
    
  /* 擦除用户区域 (用户区域指程序本身没有使用的空间，可以自定义)**/
  /* 清除各种FLASH的标志位 */  
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                  FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 


	uwStartSector = GetSector(FLASH_USER_START_ADDR);
	uwEndSector = GetSector(FLASH_USER_END_ADDR);

  /* 开始擦除操作 */
  uwSectorCounter = uwStartSector;
  while (uwSectorCounter <= uwEndSector) 
  {
    /* VoltageRange_3 以“字”的大小进行操作 */ 
    if (FLASH_EraseSector(uwSectorCounter, VoltageRange_3) != FLASH_COMPLETE)
    { 
      /*擦除出错，返回，实际应用中可加入处理 */
			return -1;
    }
    /* 计数器指向下一个扇区 */
    if (uwSectorCounter == FLASH_Sector_11)
    {
      uwSectorCounter += 40;
    } 
    else 
    {
      uwSectorCounter += 8;
    }
  }

  /* 以“字”的大小为单位写入数据 ********************************/
  uwAddress = FLASH_USER_START_ADDR;

  while (uwAddress < FLASH_USER_END_ADDR)
  {
    if (FLASH_ProgramWord(uwAddress, DATA_32) == FLASH_COMPLETE)
    {
      uwAddress = uwAddress + 4;
    }
    else
    { 
      /*写入出错，返回，实际应用中可加入处理 */
			return -1;
    }
  }
	

  /* 给FLASH上锁，防止内容被篡改*/
  FLASH_Lock(); 


  /* 从FLASH中读取出数据进行校验***************************************/
  /*  MemoryProgramStatus = 0: 写入的数据正确
      MemoryProgramStatus != 0: 写入的数据错误，其值为错误的个数 */
  uwAddress = FLASH_USER_START_ADDR;
  uwMemoryProgramStatus = 0;
  
  while (uwAddress < FLASH_USER_END_ADDR)
  {
    uwData32 = *(__IO uint32_t*)uwAddress;

    if (uwData32 != DATA_32)
    {
      uwMemoryProgramStatus++;  
    }

    uwAddress = uwAddress + 4;
  }  
  /* 数据校验不正确 */
  if(uwMemoryProgramStatus)
  {    
		return -1;
  }
  else /*数据校验正确*/
  { 
		return 0;   
  }
}



/*******************************************************************************************************
** 函数: FlashReadWriteTest, 内部Flash读写测试函数
**------------------------------------------------------------------------------------------------------
** 参数: void
** 返回: TEST_ERROR：错误（擦除、写入错误）  TEST_SUCCESS：成功   TEST_FAILED：失败
** 说明: 无
********************************************************************************************************/
int FlashReadWriteTest(void)
{
    uint32_t ucStartAddr;
    
    /* 解锁 */
    FLASH_Unlock(); 
    
    /* 擦除操作 */
    ucStartAddr = 0x080C1000;
//    if (FLASH_COMPLETE != FLASH_EraseSector(FLASH_Sector_10, VoltageRange_3))
//    {
//        printf("Erase Error!\n");
//        return TEST_ERROR;
//    }
//    else
//    {
//        ucStartAddr = ADDR_FLASH_PAGE_255;
//        printf("擦除成功，此时FLASH中值为：\n");
//        for (int i = 0; i < BufferSize; i++)
//        {
//            usFlashReadBuf[i] = *(uint32_t*)ucStartAddr;
//            printf("ucFlashReadBuf[%d] = 0x%.4x\n", i, usFlashReadBuf[i]);
//            ucStartAddr += 2;
//        }
//    }
    /* 写入操作 */
    //ucStartAddr = 0x080E1000;
    printf("\n往FLASH中写入的数据为：\n");
    for (int i = 0; i < BufferSize; i++)
    {
        if (FLASH_COMPLETE != FLASH_ProgramHalfWord(ucStartAddr, usFlashWriteBuf[i]))
        {
            printf("Write Error!\n");
            return TEST_ERROR;
        }
        printf("ucFlashWriteBuf[%d] = 0x%.4x\n", i, usFlashWriteBuf[i]);
        ucStartAddr += 2;
    }
    
    /* 上锁 */
    FLASH_Lock();
    ucStartAddr = 0x080C1000;
    printf("\n从FLASH中读出的数据为：\n");
    for (int i = 0; i < BufferSize; i++)
    {
        usFlashReadBuf[i] = *(__IO uint16_t*)ucStartAddr;
        printf("ucFlashReadBuf[%d] = 0x%.4x\n", i, usFlashReadBuf[i]);
        ucStartAddr += 2;
    }

    
    /* 读出的数据与写入的数据做比较 */
    for (int i = 0; i < BufferSize; i++)
    {
        if (usFlashReadBuf[i] != usFlashWriteBuf[i])
        {
            return TEST_FAILED;
        }
    }
    
    return TEST_SUCCESS;
}



/**
  * @brief  根据输入的地址给出它所在的sector
  *					例如：
						uwStartSector = GetSector(FLASH_USER_START_ADDR);
						uwEndSector = GetSector(FLASH_USER_END_ADDR);	
  * @param  Address：地址
  * @retval 地址所在的sector
  */
uint32_t GetSector(uint32_t Address)
{
  uint32_t sector = 0;
  
  if((Address < ADDR_FLASH_SECTOR_1) && (Address >= ADDR_FLASH_SECTOR_0))
  {
    sector = FLASH_Sector_0;  
  }
  else if((Address < ADDR_FLASH_SECTOR_2) && (Address >= ADDR_FLASH_SECTOR_1))
  {
    sector = FLASH_Sector_1;  
  }
  else if((Address < ADDR_FLASH_SECTOR_3) && (Address >= ADDR_FLASH_SECTOR_2))
  {
    sector = FLASH_Sector_2;  
  }
  else if((Address < ADDR_FLASH_SECTOR_4) && (Address >= ADDR_FLASH_SECTOR_3))
  {
    sector = FLASH_Sector_3;  
  }
  else if((Address < ADDR_FLASH_SECTOR_5) && (Address >= ADDR_FLASH_SECTOR_4))
  {
    sector = FLASH_Sector_4;  
  }
  else if((Address < ADDR_FLASH_SECTOR_6) && (Address >= ADDR_FLASH_SECTOR_5))
  {
    sector = FLASH_Sector_5;  
  }
  else if((Address < ADDR_FLASH_SECTOR_7) && (Address >= ADDR_FLASH_SECTOR_6))
  {
    sector = FLASH_Sector_6;  
  }
  else if((Address < ADDR_FLASH_SECTOR_8) && (Address >= ADDR_FLASH_SECTOR_7))
  {
    sector = FLASH_Sector_7;  
  }
  else if((Address < ADDR_FLASH_SECTOR_9) && (Address >= ADDR_FLASH_SECTOR_8))
  {
    sector = FLASH_Sector_8;  
  }
  else if((Address < ADDR_FLASH_SECTOR_10) && (Address >= ADDR_FLASH_SECTOR_9))
  {
    sector = FLASH_Sector_9;  
  }
  else if((Address < ADDR_FLASH_SECTOR_11) && (Address >= ADDR_FLASH_SECTOR_10))
  {
    sector = FLASH_Sector_10;  
  }
  else /*((Address < FLASH_END_ADDR) && (Address >= ADDR_FLASH_SECTOR_11))*/
  {
    sector = FLASH_Sector_11;  
  }


  return sector;
}


uint32_t GetSectorFlag(uint32_t dwSector)
{
    uint8_t sectorFlag = 0;
    if(FLASH_Sector_7 == dwSector)
    {
        sectorFlag = SECTOR_7_ERASED;
    }
    if(FLASH_Sector_8 == dwSector)
    {
        sectorFlag = SECTOR_8_ERASED; 
    }
    else if(FLASH_Sector_9 == dwSector)
    {
        sectorFlag = SECTOR_9_ERASED;
    }
    else if(FLASH_Sector_10 == dwSector)
    {
        sectorFlag = SECTOR_10_ERASED;
    }
    else if(FLASH_Sector_11 == dwSector)
    {
        sectorFlag = SECTOR_11_ERASED;
    }
    else if(FLASH_Sector_12 == dwSector)
    {
        sectorFlag = SECTOR_12_ERASED;
    }

    return sectorFlag;
}


/*****************************************************
 * @fn       internal_flash_read
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void internal_flash_read(uint32_t addr, uint8_t *buf, uint32_t size)
{
    uint32_t i = 0;
    uint32_t word = 0;
    uint8_t *orig_buf = buf;    // 保存原 buf 起始
    uint32_t orig_size = size;

    // 如果 flash end 是闭区间 [start, end]，这里要用 >=
    CUSTOM_ASSERT(NULL == buf, return);
    CUSTOM_ASSERT(size <= 0, return);

    if ((addr < ADDR_FLASH_SECTOR_7) || (addr + size > ADDR_FLASH_SECTOR_12))
    {
        os_debug("read outrange flash! addr=0x%08x, size=%lu\n", addr, size);
        return;
    }
#if INTERNAL_FLASH_DEBUG
    printf("internal_flash_read addr:%08x size:%d\n", addr, size);
#endif
    // 按 word 读
    while (size >= 4)
    {
        word = *(__IO uint32_t *)addr;
    
        buf[0] = (uint8_t)(word & 0xFF);
        buf[1] = (uint8_t)((word >> 8) & 0xFF);
        buf[2] = (uint8_t)((word >> 16) & 0xFF);
        buf[3] = (uint8_t)((word >> 24) & 0xFF);
    
        buf += 4;
        addr += 4;
        size -= 4;
    }

    // 剩下不足 4 字节
    while (size--)
    {
        *buf++ = *(__IO uint8_t *)addr++;
    }


#if INTERNAL_FLASH_DEBUG       
    // 用保存的 orig_buf 和 orig_size
    for (i = 0; i < orig_size; i++)
    {
        printf("%02X ", orig_buf[i]);
    }
    printf("\n");
#endif

    return;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void internal_flash_write(uint32_t addr, uint8_t *buf, uint32_t size)
{
    uint32_t end_addr = addr + size;
    uint32_t i = 0;
    uint32_t dwFirstSector = 0;
    uint32_t dwLastSector = 0;
    uint8_t byFlag = 0;
    uint32_t start_align_offset = addr % 4;
    uint32_t first_word_addr = addr - start_align_offset;

    /* 写入地址和长度校验 */
    CUSTOM_ASSERT(end_addr > ADDR_FLASH_SECTOR_12, return);
    CUSTOM_ASSERT(size < 1, return);

    /* Get the 1st sector to erase */
    dwFirstSector = GetSector(addr);
    byFlag = GetSectorFlag(dwFirstSector);

#if INTERNAL_FLASH_DEBUG
    os_printf(" internal_flash_table[i].size_used:%d \n", internal_flash_table[i].size_used);
#endif
    FLASH_Unlock();

    /* 清除各种FLASH的标志位 */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 
    #if 0
    /* Get the 1st sector to erase */
    dwFirstSector = GetSector(addr);
    byFlag = GetSectorFlag(dwFirstSector);
    g_bySectorMap &= ~byFlag;

    dwLastSector = GetSector(end_addr);
    byFlag = GetSectorFlag(dwLastSector);
    g_bySectorMap &= ~byFlag;

    /* 标记中间的扇区已写入 */
    while(dwFirstSector < dwLastSector)
    {
        dwFirstSector += 8;
        byFlag = GetSectorFlag(dwFirstSector);
        g_bySectorMap &= ~byFlag;
    }
    #endif
#if INTERNAL_FLASH_DEBUG
    os_printf(" internal_flash_write addr:0x%08x size:%d\n", addr, size);
#endif
    /* 按字节写入数据到内部flash，其实可以按16位或32位写入，但是代码会更复杂写，需要考虑未对齐的情况 */
    while (size > 0)
    {
        uint32_t aligned_addr = addr & ~0x3;  // 4字节对齐
        uint32_t word = *(__IO uint32_t*)aligned_addr;

        uint8_t offset = addr & 0x3; // 当前地址在Word里的偏移
        while (offset < 4 && size > 0)
        {
            ((uint8_t*)&word)[offset] = *buf;
            buf++;
            addr++;
            size--;
            offset++;
        }

        if (FLASH_ProgramWord(aligned_addr, word) != FLASH_COMPLETE)
        {
            os_debug("ProgramWord fail!\n");
            break;
        }
    }

    FLASH_Lock();

    return;
}

#if 0
/*****************************************************
 * @fn       internal_flash_erase
 * @brief    erase by 4k to adapter flash manage
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void internal_flash_erase(uint32_t addr)
{
    uint32_t dwFirstSector = 0;
    uint32_t dwLastSector = 0;
    uint8_t byFlag = 0;
    uint8_t i = 0;

#if 1
    CUSTOM_ASSERT(addr < ADDR_FLASH_SECTOR_0, return);
    CUSTOM_ASSERT(addr > ADDR_FLASH_SECTOR_12, return);


    /* Get the 1st sector to erase */
    dwFirstSector = GetSector(addr);

//    byFlag = GetSectorFlag(dwSector);

    for(i = 0; i < 2; i++)
    {
        if( addr == internal_flash_table[i].start_addr)
        {
            /* 找到对应地址*/
            break;
        }
    }

    /* 分区的最后sector */
    dwLastSector = GetSector(internal_flash_table[i].start_addr + internal_flash_table[i].size - 1);

#if INTERNAL_FLASH_DEBUG
    printf("internal_flash_erase addr:0x%08x dwFirstSector:0x%04x dwLastSector:0x%04x\n", addr, dwFirstSector, dwLastSector);
#endif

    /* 仅当擦除地址为管理头地址时, 支持擦除整个分区 */
    if(addr == internal_flash_table[i].start_addr)
    {
        /* Unlock the Flash to enable the flash control register access */
        FLASH_Unlock();

        /* 清除各种FLASH的标志位 */  
        FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 

        if(dwLastSector >= dwFirstSector)
        {
            /* VoltageRange_3 以“字(32位)”的大小进行擦除，清除整个扇区的空间 */ 
            if (FLASH_EraseSector(dwFirstSector, VoltageRange_3) != FLASH_COMPLETE)
            {
                os_debug("FLASH_EraseSector err!\n");
                /*擦除出错，返回，实际应用中可加入处理 */
                return;
            }
            dwFirstSector += 8;
#if INTERNAL_FLASH_DEBUG
            os_printf("flash(rom) addr:0x%08x erase  success!\n", dwFirstSector);
#endif
        }

        /* 标记某个扇区已擦除 */
        //g_bySectorMap |= byFlag;

        FLASH_Lock();
    }
#endif
    /* internal flash仅支持顺序写, 不考虑覆盖写的擦除问题 */
    return;
}
#endif

/*****************************************************
 * @fn       internal_flash_erase
 * @brief    erase by 4k to adapter flash manage
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void internal_flash_erase(uint32_t addr)
{
    /* Unlock the Flash to enable the flash control register access */
    FLASH_Unlock();
    
    /* 清除各种FLASH的标志位 */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR  | FLASH_FLAG_WRPERR | 
                 FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* VoltageRange_3 以“字(32位)”的大小进行擦除，清除整个扇区的空间 */ 
    if (FLASH_EraseSector(addr, VoltageRange_3) != FLASH_COMPLETE)
    {
        os_debug("FLASH_EraseSector err!\n");
        /*擦除出错，返回，实际应用中可加入处理 */
        return;
    }

    FLASH_Lock();
    return;
}


/*****************************************************
 * @fn       internal_flash_erase_all
 * @brief    erase by 4k to adapter flash manage
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void internal_flash_erase_all(void)
{
    /* Unlock the Flash to enable the flash control register access */
    FLASH_Unlock();

    /* 清除各种FLASH的标志位 */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR  | FLASH_FLAG_WRPERR | 
                 FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR); 

    CUSTOM_ASSERT(FLASH_EraseSector(FLASH_Sector_7, VoltageRange_3) != FLASH_COMPLETE, return);
    CUSTOM_ASSERT(FLASH_EraseSector(FLASH_Sector_8, VoltageRange_3) != FLASH_COMPLETE, return);
    CUSTOM_ASSERT(FLASH_EraseSector(FLASH_Sector_9, VoltageRange_3) != FLASH_COMPLETE, return);
    CUSTOM_ASSERT(FLASH_EraseSector(FLASH_Sector_10, VoltageRange_3) != FLASH_COMPLETE, return);
    CUSTOM_ASSERT(FLASH_EraseSector(FLASH_Sector_11, VoltageRange_3) != FLASH_COMPLETE, return);

    FLASH_Lock();

    os_printf("flash(rom) full erase success!\n");
    return;
}		






