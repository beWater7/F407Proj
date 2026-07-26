/***************************************************************
 * @file    :  storage_manage.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/
#ifndef __DEV_CONFIG_H__
#define __DEV_CONFIG_H__

#include "flash_manage.h"

/***
 * ADDR:0xe00000   SIZE: 1MB
 * 
 * FATE1  FATE1  CFG1   CFG2
 * |  4K  |  4K  |  4K  | 4K |
 *
 ***/

#define CFG_MODULE_VERSION   0x10000000

#define DEV_MNG_MAGIC  0x46415445  // 'FATE' 的 ASCII 表示

#define MAGIC_INTERVAL 0x1000

#define DEVINFO_MAGIC  0x1000
#define NETWORK_MAGIC  0x2000


#define CFGPARA_SIZE  0x1000      // 4k      


/* FATE表顺序 */
typedef enum{
    DEV_FATE_ID = 0,

    FATE_ID_MAX,

}FATE_ID_EN;


typedef struct {
    os_mutex lock;
    uint8_t bySave;
    uint8_t byRes[3];
}DEVPARAM_MNG_T, *DEVPARAM_MNG_PTR;


typedef struct {
    uint32_t dwMagic;     // 魔幻数
    uint32_t version;     // 版本  
    uint32_t fateNum;     // 节点数
    uint32_t dwLen;       // 配置长度
    uint32_t dwCrc32;     // 整个nodes[]的CRC（保护完整性）
}CFG_MNG_T, *CFG_MNG_PTR;


typedef struct {
    uint32_t dwMagic;       // 魔幻数
    uint32_t type;          // 类型：0=APP，1=BOOT，2=OTA，3=CFG
    uint32_t fateOffset;    // 偏移地址
    uint32_t entryOffset;
    uint32_t entryNum;
    uint32_t entryLen;      // 数据长度
}FATE_NODE_T, *FATE_NODE_PTR;


//typedef struct {
//    uint32_t         
//    uint32_t 
//    uint8_t 
//}PARAM_NODE_T, *PARAM_NODE_PTR; 


typedef struct {
    uint8_t byDebugLevel;
    uint8_t byRes[3];
}DEVINFO_PARAM_T, *DEVINFO_PARAM_PTR;


typedef struct {
    DEVINFO_PARAM_T stDevParam;
    /**/
}DEV_PARAM_T, *DEV_PARAM_PTR;

/* 网络参数（shell dhcp_config 使用；完整实现可再扩展） */
typedef struct {
    uint8_t byDhcpEnabled;
    uint8_t byRes[3];
} NETWORK_PARAM_T, *NETWORK_PARAM_PTR;

int setNetworkParam(NETWORK_PARAM_T *p);
void setDhcpEnabled(uint8_t en);
uint8_t getDhcpEnabled(void);
#define setDevParam  setDevInfoParam


#define DEVPARAM_CRC_ENABLE  0


#define DEVCFG_PREFIX  "[DEVCFG] "
#define DEVCFG_ALERT   KERN_ALERT DEVCFG_PREFIX
#define DEVCFG_ERROR   KERN_ERROR DEVCFG_PREFIX
#define DEVCFG_WARN    KERN_WARN DEVCFG_PREFIX
#define DEVCFG_REPORT  KERN_REPORT DEVCFG_PREFIX
#define DEVCFG_INFO    KERN_INFO DEVCFG_PREFIX
#define DEVCFG_TRACE   KERN_TRACE DEVCFG_PREFIX


#define DEVCFG_DEBUG_ENABLE  1


#if DEVCFG_DEBUG_ENABLE
#define DEVCFG_DEBUG(format, ...) os_printf_api(format, ##__VA_ARGS__)
#else
#define DEVCFG_DEBUG(format, ...)
#endif

int devCfg_init();
int devPara_init();
int devCfgRestore();

void devParamSave();
void devParamSaveNow();

int getDevInfoParam(DEVINFO_PARAM_PTR pStDevParam);
int setDevInfoParam(DEVINFO_PARAM_PTR pStDevParam);


#endif /* __DEV_CONFIG_H__ */


