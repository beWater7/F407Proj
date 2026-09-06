#ifndef __INTERNAL_FLASH_H
#define	__INTERNAL_FLASH_H

#include "stm32f4xx.h"
#include <stdio.h>

//typedef unsigned int uint32_t;


/* Base address of the Flash sectors */
#ifndef ADDR_FLASH_SECTOR_0
#define ADDR_FLASH_SECTOR_0     ((uint32_t)0x08000000) /* Base address of Sector 0, 16 Kbytes   */
#endif
#ifndef ADDR_FLASH_SECTOR_1
#define ADDR_FLASH_SECTOR_1     ((uint32_t)0x08004000) /* Base address of Sector 1, 16 Kbytes   */
#endif
#ifndef ADDR_FLASH_SECTOR_2
#define ADDR_FLASH_SECTOR_2     ((uint32_t)0x08008000) /* Base address of Sector 2, 16 Kbytes   */
#endif
#ifndef ADDR_FLASH_SECTOR_3
#define ADDR_FLASH_SECTOR_3     ((uint32_t)0x0800C000) /* Base address of Sector 3, 16 Kbytes   */
#endif
#ifndef ADDR_FLASH_SECTOR_4
#define ADDR_FLASH_SECTOR_4     ((uint32_t)0x08010000) /* Base address of Sector 4, 64 Kbytes   */
#endif
#ifndef ADDR_FLASH_SECTOR_5
#define ADDR_FLASH_SECTOR_5     ((uint32_t)0x08020000) /* Base address of Sector 5, 128 Kbytes  */
#endif
#ifndef ADDR_FLASH_SECTOR_6
#define ADDR_FLASH_SECTOR_6     ((uint32_t)0x08040000) /* Base address of Sector 6, 128 Kbytes  */
#endif
#ifndef ADDR_FLASH_SECTOR_7
#define ADDR_FLASH_SECTOR_7     ((uint32_t)0x08060000) /* Base address of Sector 7, 128 Kbytes  */
#endif
#ifndef ADDR_FLASH_SECTOR_8
#define ADDR_FLASH_SECTOR_8     ((uint32_t)0x08080000) /* Base address of Sector 8, 128 Kbytes  */
#endif
#ifndef ADDR_FLASH_SECTOR_9
#define ADDR_FLASH_SECTOR_9     ((uint32_t)0x080A0000) /* Base address of Sector 9, 128 Kbytes  */
#endif
#ifndef ADDR_FLASH_SECTOR_10
#define ADDR_FLASH_SECTOR_10    ((uint32_t)0x080C0000) /* Base address of Sector 10, 128 Kbytes */
#endif
#ifndef ADDR_FLASH_SECTOR_11
#define ADDR_FLASH_SECTOR_11    ((uint32_t)0x080E0000) /* Base address of Sector 11, 128 Kbytes */
#endif
#ifndef ADDR_FLASH_SECTOR_12
#define ADDR_FLASH_SECTOR_12     ((uint32_t)0x08100000) /* Base address of Sector 12, 16 Kbytes  */
#endif


#ifndef FLASH_DEBUG
#define FLASH_DEBUG_ON 1
#define FLASH_DEBUG(format, ...) do{\
                                 if(FLASH_DEBUG_ON)\
                                    printf("[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);\
                                 }while(0)
#endif


#ifndef MAX
#define MAX(a, b) (a > b ? a : b)
#endif
#ifndef MIN
#define MIN(a, b) (a > b ? b : a)
#endif

int InternalFlash_Test(void);

uint32_t GetSector(uint32_t Address);

/* 下一扇区起始字节地址（内部 Flash 擦除按扇区推进） */
uint32_t GetNextSectorAddr(uint32_t addr);

void internal_flash_read(uint32_t addr, uint8_t *buf, uint32_t size);
void internal_flash_write(uint32_t addr, uint8_t *buf, uint32_t size);
void internal_flash_erase(uint32_t addr);
void internal_flash_erase_all(void);

#define INTERNAL_FLASH_DEBUG  0

#endif /* __INTERNAL_FLASH_H */
