 /**
  ******************************************************************************
  * @file    bsp_spi_flash.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   spi flash ??????????bsp 
  ******************************************************************************
  * @attention
  *
  * ?????:???STM32 F407 ??????
  * ???    :http://www.firebbs.cn
  * ???    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */
  
#include "bsp_spi_flash.h"
#include "os_debug.h"

/**
???? = ????????	???? 4KB/32KB/64KB/????????
? = ??????	???? 256 Bytes?????????????
????????????	??????????????
????????????????????
**/

#define SECTOR_SIZE 4*1024

static __IO uint32_t  SPITimeout = SPIT_LONG_TIMEOUT;
static uint8_t s_spi_dma_dummy = Dummy_Byte;
/* DMA 先落到内部 SRAM，再 memcpy 到 PSRAM；直接 DMA 写 FSMC 往往更慢 */
static uint8_t s_spi_dma_bounce[4096];

static uint16_t SPI_TIMEOUT_UserCallback(uint8_t errorCode);
static void SPI_FLASH_DMA_Init(void);
static void SPI_FLASH_BufferRead_Poll(u32 ReadAddr, u8* pBuffer, u32 NumByteToRead);
static int SPI_FLASH_DMA_ReadChunk(u8 *pBuffer, u32 len);

 /**
  * @brief  SPI_FLASH?????
  * @param  ??
  * @retval ??
  */
void SPI_FLASH_Init(void)
{
    SPI_InitTypeDef  SPI_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    /* ??? FLASH_SPI ??GPIO ??? */
    /*!< SPI_FLASH_SPI_CS_GPIO, SPI_FLASH_SPI_MOSI_GPIO, 
       SPI_FLASH_SPI_MISO_GPIO,SPI_FLASH_SPI_SCK_GPIO ?????? */
    RCC_AHB1PeriphClockCmd (FLASH_SPI_SCK_GPIO_CLK | FLASH_SPI_MISO_GPIO_CLK|FLASH_SPI_MOSI_GPIO_CLK|FLASH_CS_GPIO_CLK, ENABLE);

    /*!< SPI_FLASH_SPI ?????? */
    FLASH_SPI_CLK_INIT(FLASH_SPI_CLK, ENABLE);

    //???????????
    GPIO_PinAFConfig(FLASH_SPI_SCK_GPIO_PORT,FLASH_SPI_SCK_PINSOURCE,FLASH_SPI_SCK_AF);
    GPIO_PinAFConfig(FLASH_SPI_MISO_GPIO_PORT,FLASH_SPI_MISO_PINSOURCE,FLASH_SPI_MISO_AF);
    GPIO_PinAFConfig(FLASH_SPI_MOSI_GPIO_PORT,FLASH_SPI_MOSI_PINSOURCE,FLASH_SPI_MOSI_AF);

    /*!< ???? SPI_FLASH_SPI ????: SCK */
    GPIO_InitStructure.GPIO_Pin = FLASH_SPI_SCK_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;  

    GPIO_Init(FLASH_SPI_SCK_GPIO_PORT, &GPIO_InitStructure);

    /*!< ???? SPI_FLASH_SPI ????: MISO */
    GPIO_InitStructure.GPIO_Pin = FLASH_SPI_MISO_PIN;
    GPIO_Init(FLASH_SPI_MISO_GPIO_PORT, &GPIO_InitStructure);

    /*!< ???? SPI_FLASH_SPI ????: MOSI */
    GPIO_InitStructure.GPIO_Pin = FLASH_SPI_MOSI_PIN;
    GPIO_Init(FLASH_SPI_MOSI_GPIO_PORT, &GPIO_InitStructure);  

    /*!< ???? SPI_FLASH_SPI ????: CS */
    GPIO_InitStructure.GPIO_Pin = FLASH_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(FLASH_CS_GPIO_PORT, &GPIO_InitStructure);

    /* ????? FLASH: CS???????*/
    SPI_FLASH_CS_HIGH();

    /* FLASH_SPI ?????? */
    // FLASH??? ???SPI??0????3?????????CPOL CPHA
    /* SPI??????????????????????? */

    /* ????SPI????????????? */
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    /*????SPI?????*/
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    /*SPI???????????*/
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    /*????SPI??????CPOL= 1 CPHA= 1 ?????3????????????????? */
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    /* ??????SPI??????SPI?????????????SPI */
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    /* ??????????????????fpclk 2????????SCK???????? */
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    /*SPI??????	?????????????MSB????????LSB???? */
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    /*SPI	CRC?????????*/
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(FLASH_SPI, &SPI_InitStructure);

    /* ??? FLASH_SPI  */
    SPI_Cmd(FLASH_SPI, ENABLE);

    SPI_FLASH_DMA_Init();
}

static void SPI_FLASH_DMA_Init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    DMA_Cmd(SPI_FLASH_DMA_RX_STREAM, DISABLE);
    DMA_Cmd(SPI_FLASH_DMA_TX_STREAM, DISABLE);
    DMA_DeInit(SPI_FLASH_DMA_RX_STREAM);
    DMA_DeInit(SPI_FLASH_DMA_TX_STREAM);
}

static void SPI_FLASH_DMA_StreamsStop(void)
{
    SPI_I2S_DMACmd(FLASH_SPI, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
    DMA_Cmd(SPI_FLASH_DMA_RX_STREAM, DISABLE);
    DMA_Cmd(SPI_FLASH_DMA_TX_STREAM, DISABLE);
    while (DMA_GetCmdStatus(SPI_FLASH_DMA_RX_STREAM) != DISABLE) { }
    while (DMA_GetCmdStatus(SPI_FLASH_DMA_TX_STREAM) != DISABLE) { }
    DMA_ClearFlag(SPI_FLASH_DMA_RX_STREAM, SPI_FLASH_DMA_RX_FLAGS);
    DMA_ClearFlag(SPI_FLASH_DMA_TX_STREAM, SPI_FLASH_DMA_TX_FLAGS);
}

/**
 * @brief  SPI1 ???? DMA??TX ?? dummy??RX ???????len<=65535??
 * @retval 0 ok, -1 timeout
 */
static int SPI_FLASH_DMA_ReadChunk(u8 *pBuffer, u32 len)
{
    DMA_InitTypeDef DMA_InitStructure;
    uint32_t timeout;

    if (0 == len || NULL == pBuffer) {
        return -1;
    }

    SPI_FLASH_DMA_StreamsStop();

    /* ????? RX?????? DMA ?????????????? */
    while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_RXNE) != RESET) {
        (void)SPI_I2S_ReceiveData(FLASH_SPI);
    }
    (void)FLASH_SPI->SR;

    DMA_InitStructure.DMA_Channel            = SPI_FLASH_DMA_CHANNEL;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(FLASH_SPI->DR);
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)pBuffer;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize         = len;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold      = DMA_FIFOThreshold_1QuarterFull;
    DMA_InitStructure.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(SPI_FLASH_DMA_RX_STREAM, &DMA_InitStructure);

    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)&s_spi_dma_dummy;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Disable;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_Init(SPI_FLASH_DMA_TX_STREAM, &DMA_InitStructure);

    /* ??? RX????? TX????????? */
    SPI_I2S_DMACmd(FLASH_SPI, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(FLASH_SPI, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(SPI_FLASH_DMA_RX_STREAM, ENABLE);
    DMA_Cmd(SPI_FLASH_DMA_TX_STREAM, ENABLE);

    /* ~42MHz SPI??????? 20 ????????????????? */
    timeout = len * 64U + 200000U;
    while (DMA_GetFlagStatus(SPI_FLASH_DMA_RX_STREAM, SPI_FLASH_DMA_RX_TCFLAG) == RESET) {
        if (DMA_GetFlagStatus(SPI_FLASH_DMA_RX_STREAM, DMA_FLAG_TEIF0) != RESET) {
            SPI_FLASH_DMA_StreamsStop();
            FLASH_ERROR("SPI DMA RX TE");
            return -1;
        }
        if ((timeout--) == 0U) {
            SPI_FLASH_DMA_StreamsStop();
            FLASH_ERROR("SPI DMA RX timeout len=%lu", (unsigned long)len);
            return -1;
        }
    }

    timeout = 100000U;
    while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_BSY) == SET) {
        if ((timeout--) == 0U) {
            break;
        }
    }

    SPI_FLASH_DMA_StreamsStop();
    return 0;
}

static void SPI_FLASH_BufferRead_Poll(u32 ReadAddr, u8* pBuffer, u32 NumByteToRead)
{
    SPI_FLASH_CS_LOW();
    SPI_FLASH_SendByte(W25X_ReadData);
    SPI_FLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
    SPI_FLASH_SendByte((ReadAddr & 0xFF00) >> 8);
    SPI_FLASH_SendByte(ReadAddr & 0xFF);

    while (NumByteToRead--) {
        *pBuffer++ = SPI_FLASH_SendByte(Dummy_Byte);
    }
    SPI_FLASH_CS_HIGH();
}


void SPI_Flash_Erase(uint32_t addr, uint32_t len)
{
    uint32_t sector_addr = addr & ~(SECTOR_SIZE - 1); // ???????
    uint32_t end_addr = addr + len;

    while (sector_addr < end_addr)
    {
        SPI_FLASH_SectorErase(sector_addr); // ??????? sector
        sector_addr += SECTOR_SIZE;
    }
}


 /**
  * @brief  ????FLASH????
  * @param  SectorAddr????????????????
  * @retval ??
  */
void SPI_FLASH_SectorErase(u32 SectorAddr)
{
    /* ????FLASH????????? */
    SPI_FLASH_WriteEnable();

    SPI_FLASH_WaitForWriteEnd();
    /* ???????? */
    /* ???FLASH: CS???? */
    SPI_FLASH_CS_LOW();
    /* ???????????????*/
    SPI_FLASH_SendByte(W25X_SectorErase);

    /*???????????????????*/
    SPI_FLASH_SendByte((SectorAddr & 0xFF0000) >> 16);
    /* ???????????????????? */
    SPI_FLASH_SendByte((SectorAddr & 0xFF00) >> 8);
    /* ??????????????????? */
    SPI_FLASH_SendByte(SectorAddr & 0xFF);
    /* ????? FLASH: CS ???? */
    SPI_FLASH_CS_HIGH();
    /* ??????????*/
    SPI_FLASH_WaitForWriteEnd();
}


 /**
  * @brief  ????FLASH?????????????
  * @param  ??
  * @retval ??
  */
void SPI_FLASH_BulkErase(void)
{
  /* ????FLASH????????? */
  SPI_FLASH_WriteEnable();

  /* ???? Erase */
  /* ???FLASH: CS???? */
  SPI_FLASH_CS_LOW();
  /* ??????????????*/
  SPI_FLASH_SendByte(W25X_ChipErase);
  /* ????? FLASH: CS ???? */
  SPI_FLASH_CS_HIGH();

  /* ??????????*/
  SPI_FLASH_WaitForWriteEnd();
}



 /**
  * @brief  ??FLASH??????????????????????????????????????????
  * @param	pBuffer???????????????
  * @param WriteAddr????????
  * @param  NumByteToWrite?????????????????????????SPI_FLASH_PerWritePageSize
  * @retval ??
  */
void SPI_FLASH_PageWrite(u8* pBuffer, u32 WriteAddr, u32 NumByteToWrite)
{
  /* ????FLASH????????? */
  SPI_FLASH_WriteEnable();

  /* ???FLASH: CS???? */
  SPI_FLASH_CS_LOW();
  /* ????????*/
  SPI_FLASH_SendByte(W25X_PageProgram);
  /*??????????????*/
  SPI_FLASH_SendByte((WriteAddr & 0xFF0000) >> 16);
  /*???????????????*/
  SPI_FLASH_SendByte((WriteAddr & 0xFF00) >> 8);
  /*??????????????*/
  SPI_FLASH_SendByte(WriteAddr & 0xFF);

  if(NumByteToWrite > SPI_FLASH_PerWritePageSize)
  {
     NumByteToWrite = SPI_FLASH_PerWritePageSize;
     FLASH_ERROR("SPI_FLASH_PageWrite too large!");
  }

  /* ????????*/
  while (NumByteToWrite--)
  {
    /* ??????????????????? */
    SPI_FLASH_SendByte(*pBuffer);
    /* ????????????? */
    pBuffer++;
  }

  /* ????? FLASH: CS ???? */
  SPI_FLASH_CS_HIGH();

  /* ??????????*/
  SPI_FLASH_WaitForWriteEnd();
}


 /**
  * @brief  ??FLASH???????????????????????????????????????
  * @param	pBuffer???????????????
  * @param  WriteAddr????????
  * @param  NumByteToWrite?????????????
  * @retval ??
  */
void SPI_FLASH_BufferWrite(u32 WriteAddr, u8* pBuffer, u32 NumByteToWrite)
{
  u8 NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;
	
	/*mod??????????writeAddr??SPI_FLASH_PageSize??????????????Addr??0*/
  Addr = WriteAddr % SPI_FLASH_PageSize;
	
	/*??count??????????????????????*/
  count = SPI_FLASH_PageSize - Addr;	
	/*?????????????????*/
  NumOfPage =  NumByteToWrite / SPI_FLASH_PageSize;
	/*mod???????????????????????????*/
  NumOfSingle = NumByteToWrite % SPI_FLASH_PageSize;

	 /* Addr=0,??WriteAddr ????????? aligned  */
  if (Addr == 0) 
  {
		/* NumByteToWrite < SPI_FLASH_PageSize */
    if (NumOfPage == 0) 
    {
      SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumByteToWrite);
    }
    else /* NumByteToWrite > SPI_FLASH_PageSize */
    {
			/*??????????????*/
      while (NumOfPage--)
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, SPI_FLASH_PageSize);
        WriteAddr +=  SPI_FLASH_PageSize;
        pBuffer += SPI_FLASH_PageSize;
      }
			
			/*?????????????????????????????*/
      SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumOfSingle);
    }
  }
	/* ??????? SPI_FLASH_PageSize ??????  */
  else 
  {
		/* NumByteToWrite < SPI_FLASH_PageSize */
    if (NumOfPage == 0) 
    {
			/*????????count???????NumOfSingle??????????*/
      if (NumOfSingle > count) 
      {
        temp = NumOfSingle - count;
				
		/*??????????*/
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, count);
        WriteAddr +=  count;
        pBuffer += count;
				
		/*????????????*/
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, temp);
      }
      else /*????????count????????????NumOfSingle??????*/
      {				
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumByteToWrite);
      }
    }
    else /* NumByteToWrite > SPI_FLASH_PageSize */
    {
			/*?????????????count??????????????????????*/
      NumByteToWrite -= count;
      NumOfPage =  NumByteToWrite / SPI_FLASH_PageSize;
      NumOfSingle = NumByteToWrite % SPI_FLASH_PageSize;

      SPI_FLASH_PageWrite(pBuffer, WriteAddr, count);
      WriteAddr +=  count;
      pBuffer += count;

    /*?????????????*/
      while (NumOfPage--)
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, SPI_FLASH_PageSize);
        WriteAddr +=  SPI_FLASH_PageSize;
        pBuffer += SPI_FLASH_PageSize;
      }
			/*?????????????????????????????*/
      if (NumOfSingle != 0)
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumOfSingle);
      }
    }
  }
}

 /**
  * @brief  Read FLASH via SPI1 DMA (cmd/addr polled, payload DMA)
  */
void SPI_FLASH_BufferRead(u32 ReadAddr, u8* pBuffer, u32 NumByteToRead)
{
    u32 chunk;

    if (NULL == pBuffer || 0 == NumByteToRead) {
        return;
    }

    /* Short reads: polling is cheaper than DMA setup */
    if (NumByteToRead < SPI_FLASH_DMA_MIN_LEN) {
        SPI_FLASH_BufferRead_Poll(ReadAddr, pBuffer, NumByteToRead);
        return;
    }

    /* CCM RAM is not DMA-accessible */
    {
        uint32_t addr = (uint32_t)pBuffer;
        if (addr >= 0x10000000U && addr < 0x10010000U) {
            SPI_FLASH_BufferRead_Poll(ReadAddr, pBuffer, NumByteToRead);
            return;
        }
    }

    SPI_FLASH_CS_LOW();
    SPI_FLASH_SendByte(W25X_ReadData);
    SPI_FLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
    SPI_FLASH_SendByte((ReadAddr & 0xFF00) >> 8);
    SPI_FLASH_SendByte(ReadAddr & 0xFF);

    while (NumByteToRead > 0U) {
        chunk = NumByteToRead;
        if (chunk > SPI_FLASH_DMA_CHUNK) {
            chunk = SPI_FLASH_DMA_CHUNK;
        }
        if (SPI_FLASH_DMA_ReadChunk(pBuffer, chunk) != 0) {
            /* DMA ???????????????????? CS???????????? */
            SPI_FLASH_CS_HIGH();
            SPI_FLASH_BufferRead_Poll(ReadAddr, pBuffer, NumByteToRead);
            return;
        }
        pBuffer += chunk;
        ReadAddr += chunk;
        NumByteToRead -= chunk;
    }

    SPI_FLASH_CS_HIGH();
}


u32 SPI_FLASH_ReadID(void)
{
  u32 Temp = 0, Temp0 = 0, Temp1 = 0, Temp2 = 0;

  /* ???????CS???? */
  SPI_FLASH_CS_LOW();

  /* ????JEDEC??????ID */
  SPI_FLASH_SendByte(W25X_JedecDeviceID);

  /* ????????????? */
  Temp0 = SPI_FLASH_SendByte(Dummy_Byte);

  /* ????????????? */
  Temp1 = SPI_FLASH_SendByte(Dummy_Byte);

  /* ????????????? */
  Temp2 = SPI_FLASH_SendByte(Dummy_Byte);

  /* ??????CS???? */
  SPI_FLASH_CS_HIGH();

	/*????????????????????????????*/
  Temp = (Temp0 << 16) | (Temp1 << 8) | Temp2;

  return Temp;
}

 /**
  * @brief  ???FLASH Device ID
  * @param 	??
  * @retval FLASH Device ID
  */
u32 SPI_FLASH_ReadDeviceID(void)
{
  u32 Temp = 0;

  /* Select the FLASH: Chip Select low */
  SPI_FLASH_CS_LOW();

  /* Send "RDID " instruction */
  SPI_FLASH_SendByte(W25X_DeviceID);
  SPI_FLASH_SendByte(Dummy_Byte);
  SPI_FLASH_SendByte(Dummy_Byte);
  SPI_FLASH_SendByte(Dummy_Byte);
  
  /* Read a byte from the FLASH */
  Temp = SPI_FLASH_SendByte(Dummy_Byte);

  /* Deselect the FLASH: Chip Select high */
  SPI_FLASH_CS_HIGH();

  return Temp;
}


/*******************************************************************************
* Function Name  : SPI_FLASH_StartReadSequence
* Description    : Initiates a read data byte (READ) sequence from the Flash.
*                  This is done by driving the /CS line low to select the device,
*                  then the READ instruction is transmitted followed by 3 bytes
*                  address. This function exit and keep the /CS line low, so the
*                  Flash still being selected. With this technique the whole
*                  content of the Flash is read with a single READ instruction.
* Input          : - ReadAddr : FLASH's internal address to read from.
* Output         : None
* Return         : None
*******************************************************************************/
void SPI_FLASH_StartReadSequence(u32 ReadAddr)
{
  /* Select the FLASH: Chip Select low */
  SPI_FLASH_CS_LOW();

  /* Send "Read from Memory " instruction */
  SPI_FLASH_SendByte(W25X_ReadData);

  /* Send the 24-bit address of the address to read from -----------------------*/
  /* Send ReadAddr high nibble address byte */
  SPI_FLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
  /* Send ReadAddr medium nibble address byte */
  SPI_FLASH_SendByte((ReadAddr& 0xFF00) >> 8);
  /* Send ReadAddr low nibble address byte */
  SPI_FLASH_SendByte(ReadAddr & 0xFF);
}


 /**
  * @brief  ???SPI??????????????
  * @param  ??
  * @retval ??????????????
  */
u8 SPI_FLASH_ReadByte(void)
{
  return (SPI_FLASH_SendByte(Dummy_Byte));
}


 /**
  * @brief  ???SPI???????????????
  * @param  byte????????????
  * @retval ??????????????
  */
u8 SPI_FLASH_SendByte(u8 byte)
{
  SPITimeout = SPIT_FLAG_TIMEOUT;

  /* ????????????????TXE??? */
  while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_TXE) == RESET)
   {
    if((SPITimeout--) == 0) return SPI_TIMEOUT_UserCallback(0);
   }

  /* ????????????????????????????????????? */
  SPI_I2S_SendData(FLASH_SPI, byte);

  SPITimeout = SPIT_FLAG_TIMEOUT;

  /* ????????????????RXNE??? */
  while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_RXNE) == RESET)
   {
    if((SPITimeout--) == 0) return SPI_TIMEOUT_UserCallback(1);
   }

  /* ????????????????????????????? */
  return SPI_I2S_ReceiveData(FLASH_SPI);
}

/*******************************************************************************
* Function Name  : SPI_FLASH_SendHalfWord
* Description    : Sends a Half Word through the SPI interface and return the
*                  Half Word received from the SPI bus.
* Input          : Half Word : Half Word to send.
* Output         : None
* Return         : The value of the received Half Word.
*******************************************************************************/
u16 SPI_FLASH_SendHalfWord(u16 HalfWord)
{
  
  SPITimeout = SPIT_FLAG_TIMEOUT;

  /* Loop while DR register in not emplty */
  while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_TXE) == RESET)
  {
    if((SPITimeout--) == 0) return SPI_TIMEOUT_UserCallback(2);
   }

  /* Send Half Word through the FLASH_SPI peripheral */
  SPI_I2S_SendData(FLASH_SPI, HalfWord);

  SPITimeout = SPIT_FLAG_TIMEOUT;

  /* Wait to receive a Half Word */
  while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_RXNE) == RESET)
   {
    if((SPITimeout--) == 0) return SPI_TIMEOUT_UserCallback(3);
   }
  /* Return the Half Word read from the SPI bus */
  return SPI_I2S_ReceiveData(FLASH_SPI);
}


 /**
  * @brief  ??FLASH???? ????? ????
  * @param  none
  * @retval none
  */
void SPI_FLASH_WriteEnable(void)
{
  /* ???????CS?? */
  SPI_FLASH_CS_LOW();

  /* ?????????????*/
  SPI_FLASH_SendByte(W25X_WriteEnable);

  /*????????CS?? */
  SPI_FLASH_CS_HIGH();
}

 /**
  * @brief  ???WIP(BUSY)???????0?????????FLASH??????????????
  * @param  none
  * @retval none
  */
void SPI_FLASH_WaitForWriteEnd(void)
{
  u8 FLASH_Status = 0;

  /* ??? FLASH: CS ?? */
  SPI_FLASH_CS_LOW();

  /* ???? ????????? ???? */
  SPI_FLASH_SendByte(W25X_ReadStatusReg);

  SPITimeout = SPIT_FLAG_TIMEOUT;
  /* ??FLASH???????? */
  do
  {
    /* ???FLASH???????????? */
    FLASH_Status = SPI_FLASH_SendByte(Dummy_Byte);	 

    {
      if((SPITimeout--) == 0) 
      {
        SPI_TIMEOUT_UserCallback(4);
        return;
      }
    } 
  }
  while ((FLASH_Status & WIP_Flag) == SET); /* ?????????? */

  /* ?????  FLASH: CS ?? */
  SPI_FLASH_CS_HIGH();
}


//?????????
void SPI_Flash_PowerDown(void)   
{ 
  /* ??? FLASH: CS ?? */
  SPI_FLASH_CS_LOW();

  /* ???? ???? ???? */
  SPI_FLASH_SendByte(W25X_PowerDown);

  /* ?????  FLASH: CS ?? */
  SPI_FLASH_CS_HIGH();
}   

//????
void SPI_Flash_WAKEUP(void)   
{
  /*??? FLASH: CS ?? */
  SPI_FLASH_CS_LOW();

  /* ???? ??? ???? */
  SPI_FLASH_SendByte(W25X_ReleasePowerDown);

  /* ????? FLASH: CS ?? */
  SPI_FLASH_CS_HIGH();                   //???TRES1
}


/**
  * @brief  ?????????????
  * @param  None.
  * @retval None.
  */
static  uint16_t SPI_TIMEOUT_UserCallback(uint8_t errorCode)
{
  /* ????????????,?????????? */
  FLASH_ERROR("SPI ??????!errorCode = %d",errorCode);
  return 0;
}


/*********************************************END OF FILE**********************/
