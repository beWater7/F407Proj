#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "stm32f4xx.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/
//#define  sFLASH_ID                       0xEF3015     //W25X16
//#define  sFLASH_ID                       0xEF4015	    //W25Q16
//#define  sFLASH_ID                        0XEF4017     //W25Q64
#define  sFLASH_ID                       0XEF4018     //W25Q128


//#define SPI_FLASH_PageSize            4096
#define SPI_FLASH_PageSize              256
#define SPI_FLASH_PerWritePageSize      256

/* Private define ------------------------------------------------------------*/
/*√¸¡Ó∂®“Â-ø™Õ∑*******************************/
#define W25X_WriteEnable		      0x06 
#define W25X_WriteDisable		      0x04 
#define W25X_ReadStatusReg		    0x05 
#define W25X_WriteStatusReg		  0x01 
#define W25X_ReadData			        0x03 
#define W25X_FastReadData		      0x0B 
#define W25X_FastReadDual		      0x3B 
#define W25X_PageProgram		      0x02 
#define W25X_BlockErase			      0xD8 
#define W25X_SectorErase		      0x20 
#define W25X_ChipErase			      0xC7 
#define W25X_PowerDown			      0xB9 
#define W25X_ReleasePowerDown	  0xAB 
#define W25X_DeviceID			        0xAB 
#define W25X_ManufactDeviceID   	0x90 
#define W25X_JedecDeviceID		    0x9F 

#define WIP_Flag                  0x01  /* Write In Progress (WIP) flag */
#define Dummy_Byte                0xFF
/*√¸¡Ó∂®“Â-Ω·Œ≤*******************************/


/*SPIΩ”ø⁄∂®“Â-ø™Õ∑****************************/
#define FLASH_SPI                           SPI1
#define FLASH_SPI_CLK                       RCC_APB2Periph_SPI1
#define FLASH_SPI_CLK_INIT                  RCC_APB2PeriphClockCmd

#define FLASH_SPI_SCK_PIN                   GPIO_Pin_3                  
#define FLASH_SPI_SCK_GPIO_PORT             GPIOB                       
#define FLASH_SPI_SCK_GPIO_CLK              RCC_AHB1Periph_GPIOB
#define FLASH_SPI_SCK_PINSOURCE             GPIO_PinSource3
#define FLASH_SPI_SCK_AF                    GPIO_AF_SPI1

#define FLASH_SPI_MISO_PIN                  GPIO_Pin_4                
#define FLASH_SPI_MISO_GPIO_PORT            GPIOB                   
#define FLASH_SPI_MISO_GPIO_CLK             RCC_AHB1Periph_GPIOB
#define FLASH_SPI_MISO_PINSOURCE            GPIO_PinSource4
#define FLASH_SPI_MISO_AF                   GPIO_AF_SPI1

#define FLASH_SPI_MOSI_PIN                  GPIO_Pin_5                
#define FLASH_SPI_MOSI_GPIO_PORT            GPIOB                     
#define FLASH_SPI_MOSI_GPIO_CLK             RCC_AHB1Periph_GPIOB
#define FLASH_SPI_MOSI_PINSOURCE            GPIO_PinSource5
#define FLASH_SPI_MOSI_AF                   GPIO_AF_SPI1

#define FLASH_CS_PIN                        GPIO_Pin_6               
#define FLASH_CS_GPIO_PORT                  GPIOG                     
#define FLASH_CS_GPIO_CLK                   RCC_AHB1Periph_GPIOG

#define SPI_FLASH_CS_LOW()      {FLASH_CS_GPIO_PORT->BSRRH=FLASH_CS_PIN;}
#define SPI_FLASH_CS_HIGH()     {FLASH_CS_GPIO_PORT->BSRRL=FLASH_CS_PIN;}

/* SPI1 DMA: RX=DMA2 Stream0 Ch3, TX=DMA2 Stream3 Ch3 */
#define SPI_FLASH_DMA_RX_STREAM             DMA2_Stream0
#define SPI_FLASH_DMA_TX_STREAM             DMA2_Stream3
#define SPI_FLASH_DMA_CHANNEL               DMA_Channel_3
#define SPI_FLASH_DMA_RX_FLAGS              (DMA_FLAG_FEIF0 | DMA_FLAG_DMEIF0 | DMA_FLAG_TEIF0 | DMA_FLAG_HTIF0 | DMA_FLAG_TCIF0)
#define SPI_FLASH_DMA_TX_FLAGS              (DMA_FLAG_FEIF3 | DMA_FLAG_DMEIF3 | DMA_FLAG_TEIF3 | DMA_FLAG_HTIF3 | DMA_FLAG_TCIF3)
#define SPI_FLASH_DMA_RX_TCFLAG             DMA_FLAG_TCIF0
#define SPI_FLASH_DMA_CHUNK                 16384U
#define SPI_FLASH_DMA_MIN_LEN               32U
/*SPIΩ”ø⁄∂®“Â-Ω·Œ≤****************************/

/*µ»¥˝≥¨ ± ±º‰*/
#define SPIT_FLAG_TIMEOUT         ((uint32_t)0x1000)
#define SPIT_LONG_TIMEOUT         ((uint32_t)(10 * SPIT_FLAG_TIMEOUT))

/*–≈œ¢ ‰≥ˆ*/
#define FLASH_DEBUG_ON         1

#define FLASH_INFO(fmt,arg...)           printf("<<-FLASH-INFO->> "fmt"\n",##arg)
#define FLASH_ERROR(fmt,arg...)          printf("<<-FLASH-ERROR->> "fmt"\n",##arg)
#define FLASH_DEBUG(fmt,arg...)          do{\
                                          if(FLASH_DEBUG_ON)\
                                          printf("<<-FLASH-DEBUG->> [%d]"fmt"\n",__LINE__, ##arg);\
                                          }while(0)


#define FW_WRITTEN_FLAG   ((uint32_t)0x00000017)


void SPI_FLASH_Init(void);
void SPI_FLASH_SectorErase(u32 SectorAddr);
void SPI_FLASH_BulkErase(void);
void SPI_FLASH_PageWrite(u8* pBuffer, u32 WriteAddr, u32 NumByteToWrite);

u32 SPI_FLASH_ReadID(void);
u32 SPI_FLASH_ReadDeviceID(void);
void SPI_FLASH_StartReadSequence(u32 ReadAddr);
void SPI_Flash_PowerDown(void);
void SPI_Flash_WAKEUP(void);

u8 SPI_FLASH_ReadByte(void);
u8 SPI_FLASH_SendByte(u8 byte);
u16 SPI_FLASH_SendHalfWord(u16 HalfWord);
void SPI_FLASH_WriteEnable(void);
void SPI_FLASH_WaitForWriteEnd(void);


/* === Â§?È?®È©±Â?®Ê?•Âè£Ôº?È??Ë?™Â∑±Êèê‰æ?Ôº? === */
//bool spi_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
//bool spi_flash_read(uint32_t addr, uint8_t *data, uint32_t len);

void SPI_FLASH_BufferRead(u32 ReadAddr, u8* pBuffer, u32 NumByteToRead);
void SPI_FLASH_BufferWrite(u32 WriteAddr, u8* pBuffer, u32 NumByteToWrite);

//#define FLASH_DEBUG_PRINT 1


#endif /* __SPI_FLASH_H */



