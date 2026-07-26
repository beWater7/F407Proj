/***************************************************************
 * @file    :  storage_manage.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __FLASH_MANAGE_H__
#define __FLASH_MANAGE_H__

#include <stdint.h>
#include "typedef.h"
#include "os_mutex.h"
#include "dev_manage.h"
#include "os_debug.h"

/* === 分区对象 === */
typedef struct storageCtrl STORAGE_CTRL_T, *STORAGE_CTRL_PTR;

#define PARTITION_MAGIC       0x55AA55AA    //魔幻数
#define SECTOR_SIZE           4096          /* 按你实际的 Flash 扇区大小定义 */
#define PARTITION_HEADER_SIZE SECTOR_SIZE   /* flash管理头必须单独占一个扇区, 不能存其他数据, 因为会擦除 */

/* 定义flash底层函数返回值 */
#define RETVAL void

/* SPI分区 */
typedef enum {
    PART_APP1,    
    PART_APP2,      
    PART_OTA,      
    PART_LOG,       
    PART_WEB,       
    PART_CONFIG,    
    PART_CUSTOM,
    SPI_FLASH_PART_MAX
}SPI_PART_ID_EN;


/* 内部flash分区 */
typedef enum {
    PART_FW1,
    PART_FW2,
    PART_RES,
    INTERNAL_FLASH_PART_MAX
}INTERNAL_PART_ID_EN;


typedef struct {
    const char *name;
    uint32_t start_addr;
    uint32_t size;
    uint32_t flags;      /* PART_CRCCHECK_EN 等 */
    uint32_t size_used;
} STORAGE_PART_INFO_T, *STORAGE_PART_INFO_PTR;

#define PART_CRCCHECK_EN   (1u << 0)


/* 各种flash全局管理 */
typedef struct storage_manage {

    /* 存储介质名字 */
    const char *name;
    /* 存储介质大小 */
    uint32 dwSize;
    /* 是否启用 */
    uint8 byUsed;
    /* 分区数量 */ 
    uint8 byPartNum;

    /* 储存分区信息 */
    STORAGE_CTRL_PTR pStorageCtrl;

    /* 预留对齐 */
    uint8 byRes[2]; 

} STORAGE_MANAGE_T, *STORAGE_MANAGE_PTR;


typedef struct {
    /* === 抽象底层接口 === */
    RETVAL (*hw_read)(uint32_t addr, uint8_t *buf, uint32_t len);
    RETVAL (*hw_write)(uint32_t addr, uint8_t *buf, uint32_t len);
    RETVAL (*hw_erase)(uint32_t addr);
} STORAGE_HW_OPS_T, *STORAGE_HW_OPS_PTR;


typedef struct {    
    STORAGE_HW_OPS_PTR hw_ops;

    /* 方法指针, 内部实现 */
    int (*partition_write)(STORAGE_CTRL_T *self, uint8_t index, uint32_t offset, uint8_t *data, uint32_t len);
    int (*partition_read)(STORAGE_CTRL_T *self, uint8_t index, uint32_t offset, uint8_t *data, uint32_t len);
    int (*write_verify)(STORAGE_CTRL_T *self,  uint8_t index, uint32_t offset, uint8_t *data, uint32_t len);
    int (*partition_erase)(STORAGE_CTRL_T *self,  uint8_t index, uint32_t start_addr, uint32_t end_addr);
} STORAGE_PART_OPS_T, *STORAGE_PART_OPS_PTR;


typedef struct storageCtrl {
    /* 设备管理 */
    dev_obj dev;

    /* 指向part info table address */
    STORAGE_PART_INFO_PTR pPartInfo;

    /* 划分的分区数 */
    uint8_t byPartNum;

    /* 使用管理头管理 */
    uint8_t byManage;

    uint8_t byRes[2];

    /* 实现线程安全需要锁 */
    os_mutex_t lock;

    STORAGE_PART_OPS_T stPartOps;
} STORAGE_CTRL_T, *STORAGE_CTRL_PTR;


typedef struct
{
    uint32_t magic;      /* 魔数 */
    uint32_t used_size;  /* 已用容量 */
    uint32_t crc;        /* CRC32 校验（不含自身） */
} PartitionHeader;


#define HW_READ(a, b, c)    (self->stPartOps.hw_ops->hw_read(a, b, c))
#define HW_WRITE(a, b, c)   (self->stPartOps.hw_ops->hw_write(a, b, c))
#define HW_ERASE(a)         (self->stPartOps.hw_ops->hw_erase(a))
//#define HW_ERASE(a)         (self->stPartOps.hw_ops->hw_erase?self->stPartOps.hw_ops->hw_erase(a):(void)a)


void flash_data_print(uint8_t *addr, uint16_t len);
void hex_dump(uint8_t part, uint32_t offset, uint32_t len, uint8_t dev_id);

/* === 实例化函数 === */
void FlashPartition_Init(STORAGE_CTRL_PTR self, STORAGE_PART_INFO_PTR pStPartInfo, uint8_t byPartNum, STORAGE_HW_OPS_PTR pPartHwOps);

/* 自定义区 */
#define SPI_FLASH_WRITE_VERIFY(part, offset, buf, size) \
    (g_stSpiFlashPart.stPartOps.write_verify(&g_stSpiFlashPart, part, offset, buf, size))

#define SPI_FLASH_WRITE(part, offset, buf, size) \
    (g_stSpiFlashPart.stPartOps.partition_write(&g_stSpiFlashPart, part, offset, buf, size))

#define SPI_FLASH_READ(part, offset, buf, size) \
    (g_stSpiFlashPart.stPartOps.partition_read(&g_stSpiFlashPart, part, offset, buf, size))

/* 兼容旧接口名（id 参数忽略，当前仅 SPI） */
#define PartitionRead(part, offset, buf, size, id) \
    SPI_FLASH_READ((part), (offset), (buf), (size))

#define SPI_FLASH_ERASE(part, start, end) \
    (g_stSpiFlashPart.stPartOps.partition_erase(&g_stSpiFlashPart, part, start, end))

#define SPI_FLASH_ERASE_ALL(part) \
    (g_stSpiFlashPart.stPartOps.partition_erase(&g_stSpiFlashPart, part, spi_flash_table[part].start_addr, \
                                               spi_flash_table[part].start_addr+spi_flash_table[part].size))

extern STORAGE_PART_INFO_T spi_flash_table[];

extern STORAGE_CTRL_T g_stSpiFlashPart;


/* 自定义区 */
#define INTERNAL_FLASH_WRITE_VERIFY(part, buf, size) \
    (g_stInternalFlashPart.stPartOps.write_verify(&g_stInternalFlashPart, part, 0, buf, size))

#define INTERNAL_FLASH_WRITE(part, buf, size) \
    (g_stInternalFlashPart.stPartOps.partition_write(&g_stInternalFlashPart, part, 0, buf, size))

#define INTERNAL_FLASH_READ(part, offset, buf, size) \
    (g_stInternalFlashPart.stPartOps.partition_read(&g_stInternalFlashPart, part, offset, buf, size))

#define INTERNAL_FLASH_ERASE(part, start, end) \
    (g_stSpiFlashPart.stPartOps.partition_erase(&g_stInternalFlashPart, part, start, end))

#define INTERNAL_FLASH_ERASE_ALL(part) \
    (g_stSpiFlashPart.stPartOps.partition_erase(&g_stInternalFlashPart, part, internal_flash_table[part].start_addr, \
                                               internal_flash_table[part].start_addr+internal_flash_table[part].size))

extern STORAGE_PART_INFO_T internal_flash_table[];

extern STORAGE_CTRL_T g_stInternalFlashPart;

#define STOR_PREFIX  "[STOR] "
#define STOR_ALERT   KERN_ALERT STOR_PREFIX
#define STOR_ERROR   KERN_ERROR STOR_PREFIX
#define STOR_WARN    KERN_WARN STOR_PREFIX
#define STOR_REPORT  KERN_REPORT STOR_PREFIX
#define STOR_INFO    KERN_INFO STOR_PREFIX
#define STOR_TRACE   KERN_TRACE STOR_PREFIX


#define STORAGE_MNG_DEBUG  0

#if STORAGE_MNG_DEBUG
#define STOR_DEBUG(format, ...) os_printf_api(format, __func__,__LINE__,##__VA_ARGS__)
#define STOR_INFO(format, ...) os_printf_api(STOR_PREFIX format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define STOR_DEBUG(format, ...)
#define STOR_INFO(format, ...)
#endif


/* FLASH 数据打印---小端 */
#define FLASH_DATA_LITTLE_END 1

#endif /* __FLASH_MANAGE_H__ */



