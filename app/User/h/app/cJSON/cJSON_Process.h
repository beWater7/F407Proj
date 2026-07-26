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

#ifndef __CJSON_PROCESS_H__
#define __CJSON_PROCESS_H__

#include "typedef.h"
//#include "lwip/apps/fs.h"
#include "cJSON.h"

#define NAME              "board"
#define DEFAULT_NAME      "stm32F407"
#define TEMP_NUM          "temperature"
#define DEFAULT_TEMP_NUM  35.0
#define HUM_NUM           "humidity"
#define DEFAULT_HUM_NUM   50.0

#define   UPDATE_SUCCESS       1 
#define   UPDATE_FAIL          0

cJSON* cJSON_Data_Init(void);
uint8_t cJSON_Update(cJSON * const object,const char * const string,void *d);
void Proscess(void* data);


#define PRINT_DEBUG printf


#endif /* __CJSON_PROCESS_H__ */
