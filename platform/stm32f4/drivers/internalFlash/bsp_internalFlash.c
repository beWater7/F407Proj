/**
  ******************************************************************************
  * @file    bsp_internalFlash.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   ???FLASH???????????
  ******************************************************************************
  * @attention
  *
  * ?????:???  STM32 F407 ??????  
  * ???    :http://www.firebbs.cn
  * ???    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */

#include "bsp_internalFlash.h"
#include "flash_manage.h"
#include "os_debug.h"
#include <stdio.h>



/*????????????????*/
#define DATA_32                 ((uint32_t)0x00000000)
#define OS_1KB  1024

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* ????????FLASH???????? */
#define FLASH_USER_START_ADDR   ADDR_FLASH_SECTOR_8   
/* ????????FLASH???????? */
#define FLASH_USER_END_ADDR     ADDR_FLASH_SECTOR_12  

uint8_t g_bySectorMap = 0;

#define SECTOR_7_ERASED  (1)
#define SECTOR_8_ERASED  (1 << 1)
#define SECTOR_9_ERASED  (1 << 2)
#define SECTOR_10_ERASED (1 << 3)
#define SECTOR_11_ERASED (1 << 4)
#define SECTOR_12_ERASED (1 << 5)




typedef struct{
    uint8_t bySector;       //????????
    uint32_t dwSectorAddr;  //???????
    uint8_t byHeaderSize;
    uint32_t dwUsedSize; //???????????? 
}INTERNAL_FLASH_INFO;


INTERNAL_FLASH_INFO g_stInterFlashInfo[2] = {
    {7,    sizeof(PartitionHeader)},
    {9,    sizeof(PartitionHeader)},
};



/**
  * @brief  InternalFlash_Test,?????FLASH????????????
  * @param  None
  * @retval None
  */
int InternalFlash_Test(void)
{
	/*??????????????(????)??????????(??????)????8-12?????????8??9??10??11????*/
	uint32_t uwStartSector = 0;
	uint32_t uwEndSector = 0;
	
	uint32_t uwAddress = 0;
	uint32_t uwSectorCounter = 0;

	__IO uint32_t uwData32 = 0;
	__IO uint32_t uwMemoryProgramStatus = 0;
	
  /* FLASH ???? ********************************/
  /* ??????FLASH???????? */
  FLASH_Unlock();
    
  /* ??????????? (????????????????????????????????)**/
  /* ???????FLASH?????? */  
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                  FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 


	uwStartSector = GetSector(FLASH_USER_START_ADDR);
	uwEndSector = GetSector(FLASH_USER_END_ADDR);

  /* ??????????? */
  uwSectorCounter = uwStartSector;
  while (uwSectorCounter <= uwEndSector) 
  {
    /* VoltageRange_3 ??????????????????? */ 
    if (FLASH_EraseSector(uwSectorCounter, VoltageRange_3) != FLASH_COMPLETE)
    { 
      /*?????????????????????????????? */
			return -1;
    }
    /* ?????????????????? */
    if (uwSectorCounter == FLASH_Sector_11)
    {
      uwSectorCounter += 40;
    } 
    else 
    {
      uwSectorCounter += 8;
    }
  }

  /* ??????????????????????? ********************************/
  uwAddress = FLASH_USER_START_ADDR;

  while (uwAddress < FLASH_USER_END_ADDR)
  {
    if (FLASH_ProgramWord(uwAddress, DATA_32) == FLASH_COMPLETE)
    {
      uwAddress = uwAddress + 4;
    }
    else
    { 
      /*?????????????????????????????? */
			return -1;
    }
  }
	

  /* ??FLASH?????????????????*/
  FLASH_Lock(); 


  /* ??FLASH??????????????????***************************************/
  /*  MemoryProgramStatus = 0: ?????????????
      MemoryProgramStatus != 0: ?????????????????????????? */
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
  /* ???????????? */
  if(uwMemoryProgramStatus)
  {    
		return -1;
  }
  else /*???????????*/
  { 
		return 0;   
  }
}



/**
  * @brief  ??????????????????????sector
  *					?????
						uwStartSector = GetSector(FLASH_USER_START_ADDR);
						uwEndSector = GetSector(FLASH_USER_END_ADDR);	
  * @param  Address?????
  * @retval ????????sector
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

uint32_t GetNextSectorAddr(uint32_t addr)
{
    static const uint32_t map[] = {
        ADDR_FLASH_SECTOR_0, ADDR_FLASH_SECTOR_1, ADDR_FLASH_SECTOR_2,
        ADDR_FLASH_SECTOR_3, ADDR_FLASH_SECTOR_4, ADDR_FLASH_SECTOR_5,
        ADDR_FLASH_SECTOR_6, ADDR_FLASH_SECTOR_7, ADDR_FLASH_SECTOR_8,
        ADDR_FLASH_SECTOR_9, ADDR_FLASH_SECTOR_10, ADDR_FLASH_SECTOR_11,
        ADDR_FLASH_SECTOR_12
    };
    unsigned i;

    for (i = 0; i < (sizeof(map) / sizeof(map[0]) - 1u); i++) {
        if (addr < map[i + 1]) {
            return map[i + 1];
        }
    }
    return map[sizeof(map) / sizeof(map[0]) - 1u];
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
    (void)i;
    uint32_t word = 0;
    uint8_t *orig_buf = buf;    // ????? buf ???
    (void)orig_buf;
    uint32_t orig_size = size;
    (void)orig_size;

    // ???? flash end ??????? [start, end]????????? >=
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
    // ?? word ??
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

    // ?????? 4 ???
    while (size--)
    {
        *buf++ = *(__IO uint8_t *)addr++;
    }


#if INTERNAL_FLASH_DEBUG       
    // ??????? orig_buf ?? orig_size
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
    (void)i;
    uint32_t dwFirstSector = 0;
    uint32_t dwLastSector = 0;
    (void)dwLastSector;
    uint8_t byFlag = 0;
    (void)byFlag;
    uint32_t start_align_offset = addr % 4;
    uint32_t first_word_addr = addr - start_align_offset;
    (void)first_word_addr;

    /* ???????????????? */
    CUSTOM_ASSERT(end_addr > ADDR_FLASH_SECTOR_12, return);
    CUSTOM_ASSERT(size < 1, return);

    /* Get the 1st sector to erase */
    dwFirstSector = GetSector(addr);
    byFlag = GetSectorFlag(dwFirstSector);
    (void)byFlag;

#if INTERNAL_FLASH_DEBUG
    os_printf(" internal_flash_table[i].size_used:%d \n", internal_flash_table[i].size_used);
#endif
    FLASH_Unlock();

    /* ???????FLASH?????? */  
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 
    #if 0
    /* Get the 1st sector to erase */
    dwFirstSector = GetSector(addr);
    byFlag = GetSectorFlag(dwFirstSector);
    (void)byFlag;
    g_bySectorMap &= ~byFlag;

    dwLastSector = GetSector(end_addr);
    (void)dwLastSector;
    byFlag = GetSectorFlag(dwLastSector);
    (void)byFlag;
    g_bySectorMap &= ~byFlag;

    /* ?????????????????? */
    while(dwFirstSector < dwLastSector)
    {
        dwFirstSector += 8;
        byFlag = GetSectorFlag(dwFirstSector);
        (void)byFlag;
        g_bySectorMap &= ~byFlag;
    }
    #endif
#if INTERNAL_FLASH_DEBUG
    os_printf(" internal_flash_write addr:0x%08x size:%d\n", addr, size);
#endif
    /* ?????????????????flash??????????16????32??????????????????????????????????????? */
    while (size > 0)
    {
        uint32_t aligned_addr = addr & ~0x3;  // 4??????
        uint32_t word = *(__IO uint32_t*)aligned_addr;

        uint8_t offset = addr & 0x3; // ????????Word???????
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
    (void)dwLastSector;
    uint8_t byFlag = 0;
    (void)byFlag;
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
            /* ?????????*/
            break;
        }
    }

    /* ?????????sector */
    dwLastSector = GetSector(internal_flash_table[i].start_addr + internal_flash_table[i].size - 1);
    (void)dwLastSector;

#if INTERNAL_FLASH_DEBUG
    printf("internal_flash_erase addr:0x%08x dwFirstSector:0x%04x dwLastSector:0x%04x\n", addr, dwFirstSector, dwLastSector);
#endif

    /* ?????????????????????, ?????????????? */
    if(addr == internal_flash_table[i].start_addr)
    {
        /* Unlock the Flash to enable the flash control register access */
        FLASH_Unlock();

        /* ???????FLASH?????? */  
        FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR); 

        if(dwLastSector >= dwFirstSector)
        {
            /* VoltageRange_3 ?????(32??)????????????????????????????????? */ 
            if (FLASH_EraseSector(dwFirstSector, VoltageRange_3) != FLASH_COMPLETE)
            {
                os_debug("FLASH_EraseSector err!\n");
                /*?????????????????????????????? */
                return;
            }
            dwFirstSector += 8;
#if INTERNAL_FLASH_DEBUG
            os_printf("flash(rom) addr:0x%08x erase  success!\n", dwFirstSector);
#endif
        }

        /* ???????????????? */
        //g_bySectorMap |= byFlag;

        FLASH_Lock();
    }
#endif
    /* internal flash??????????, ???????????????????? */
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
    /* app 传入 FLASH_Sector_x；loader 传入字节地址。>= Flash 基址则先换算扇区号 */
    uint32_t sector = (addr >= ADDR_FLASH_SECTOR_0) ? GetSector(addr) : addr;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR  | FLASH_FLAG_WRPERR |
                 FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    if (FLASH_EraseSector(sector, VoltageRange_3) != FLASH_COMPLETE)
    {
        os_debug("FLASH_EraseSector err!\n");
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

    /* ???????FLASH?????? */  
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






