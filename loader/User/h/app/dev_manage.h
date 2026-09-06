/***************************************************************
 * @file    :  dev_manage.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __DEV_MANAGE_H__
#define __DEV_MANAGE_H__

#include <stdint.h>
#include "typedef.h"

typedef struct dev_obj dev_obj;
typedef void ops_func_t;


typedef struct dev_obj{
    uint8_t dev_id;
    uint8_t byRes[3];
    dev_obj *next;
    /* 指向ops */
    ops_func_t *ops;
}dev_obj;



/* 设备devID */
#define SPI_FLASH_DEV_ID            0
#define INTERNAL_FLASH_DEV_ID       1 



void dev_init();
void* dev_get(uint8_t dev_id);
int dev_register(uint8_t dev_id, dev_obj *dev);
int dev_unregister(uint8_t dev_id);


#endif /* __DEV_MANAGE_H__ */


