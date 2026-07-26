/***************************************************************
 * @file    :  devConfig.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/
#include <stdlib.h>
#include <string.h>
#include "devConfig.h"
#include "safe_utils.h"
#include "lwip/sys.h"

//DEVINFO_PARAM_T g_stDevParam = {0};
static DEV_PARAM_PTR g_pstDevParam = NULL;
static DEVPARAM_MNG_T g_stDevParamMng = {0};
static BOOL waitDevCfgModuleInit = FALSE;

//uint8_t byTmp[64] = {0};
//SPI_FLASH_READ(PART_CONFIG, 0, byTmp, 64);
//flash_data_print(byTmp, 64);
static int writeDevParam();



int cfgSizeArray[FATE_ID_MAX] = {
    sizeof(DEVINFO_PARAM_T),
};

/*****************************************************
 * @fn       devRestore
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int cfgSizeArraySum()
{
    uint8_t i = 0;
    uint32_t dwSum = 0;

    for(i = 0; i < FATE_ID_MAX; i++)
    {
        dwSum += cfgSizeArray[i];
    }
    return dwSum;
}


/*****************************************************
 * @fn       waitModuleInit
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void waitModuleInit()
{
    if(waitDevCfgModuleInit)
    {
        return;
    }

    FOREVER
    {
        os_debug("wait Module init\n");
        if(waitDevCfgModuleInit)
        {
            break;
        }
        os_sleep_ms(500);
    }
    return;
}


/*****************************************************
 * @fn       devRestore
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int rebuildCfg(uint32_t dwCrcChkSum)
{
    CFG_MNG_T stCfgMng = {0};

    stCfgMng.dwMagic = DEV_MNG_MAGIC;
    stCfgMng.version = CFG_MODULE_VERSION;
    stCfgMng.fateNum = FATE_ID_MAX;
    stCfgMng.dwLen   = cfgSizeArraySum();
    stCfgMng.dwCrc32 = dwCrcChkSum;

    if (SPI_FLASH_WRITE(PART_CONFIG, 0, (uint8 *)&stCfgMng, sizeof(CFG_MNG_T)) < 0) {
        DEVCFG_DEBUG(DEVCFG_ERROR"rebuildCfg: write CFG_MNG failed\n");
        return RET_ERR;
    }

    if (writeDevParam() < 0) {
        DEVCFG_DEBUG(DEVCFG_ERROR"rebuildCfg: writeDevParam failed\n");
        return RET_ERR;
    }

    return RET_OK;
}


/*****************************************************
 * @fn       devRestore
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
static int writeDevParam()
{
    uint8_t i = 0;
    FATE_NODE_T stFateTable[FATE_ID_MAX] = {0};
    uint32_t dwOffset = 0;

    CUSTOM_ASSERT(!g_pstDevParam, return RET_ERR);
    
    dwOffset = sizeof(CFG_MNG_T) + FATE_ID_MAX*sizeof(FATE_NODE_T);
    /* 写入fate表 */
    for(i = 0; i < FATE_ID_MAX; i++)
    {
        stFateTable[i].dwMagic = (i+1)*MAGIC_INTERVAL;
        stFateTable[i].fateOffset = sizeof(CFG_MNG_T) + i*sizeof(FATE_NODE_T);
        stFateTable[i].entryNum = 1;
        stFateTable[i].entryLen = cfgSizeArray[i];
        if(i > 0)
        {
            dwOffset += stFateTable[i-1].entryNum*cfgSizeArray[i-1];
        }    
        stFateTable[i].entryOffset = dwOffset;
        stFateTable[i].type = PART_CONFIG;

        /* 写入配置 */
        if(DEVINFO_MAGIC == stFateTable[i].dwMagic)
        {
            if (SPI_FLASH_WRITE(PART_CONFIG, dwOffset, (uint8 *)&(g_pstDevParam->stDevParam), sizeof(DEVINFO_PARAM_T)) < 0) {
                return RET_ERR;
            }
        }
        /* TODO, 写入其他配置 */
    }

    dwOffset = sizeof(CFG_MNG_T);

    if (SPI_FLASH_WRITE(PART_CONFIG, dwOffset, (uint8 *)stFateTable, FATE_ID_MAX*sizeof(FATE_NODE_T)) < 0) {
        return RET_ERR;
    }

    return RET_OK;
}


/*****************************************************
 * @fn       getDevCfg
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int get_fate(uint32_t dwMagic, FATE_NODE_PTR pstFate)
{
    uint8_t i = 0;
    uint32_t dwOffset = 0;
    CFG_MNG_T stCfgMng = {0};
    FATE_NODE_T stFate = {0};

    CUSTOM_ASSERT(!pstFate, return RET_ERR);

    SPI_FLASH_READ(PART_CONFIG, 0, (uint8_t *)&stCfgMng, sizeof(CFG_MNG_T));

    CUSTOM_ASSERT(DEV_MNG_MAGIC != stCfgMng.dwMagic, return RET_ERR);

    dwOffset = sizeof(CFG_MNG_T);

    for(i = 0; i < stCfgMng.fateNum; i++)
    {
        dwOffset += i?sizeof(FATE_NODE_T):0;

        SPI_FLASH_READ(PART_CONFIG, dwOffset, (uint8_t *)&stFate, sizeof(FATE_NODE_T));

        if(dwMagic == stFate.dwMagic)
        {
            memcpy(pstFate, &stFate, sizeof(FATE_NODE_T));
            break;
        }
    }
    return RET_OK;
}


/*****************************************************
 * @fn       getDevCfg
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int get_Param(uint32_t dwMagic, uint8_t *param)
{
    uint8 byIndex = 0;
    FATE_NODE_T stFate = {0};

    CUSTOM_ASSERT(!param, return RET_ERR);

    if(get_fate(dwMagic, &stFate))
    {
        DEVCFG_DEBUG("get fate err!\n");
        return RET_ERR;
    }

    byIndex = (stFate.dwMagic >> 12) - 1; // >> 12 ==>  /0x1000

    SPI_FLASH_READ(PART_CONFIG, stFate.entryOffset, param, cfgSizeArray[byIndex]);

    return RET_OK;
}


/*****************************************************
 * @fn       getDevCfg
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int readDevCfg(DEV_PARAM_PTR pstDevParam)
{
    uint32_t dwOffset = 0;
    CFG_MNG_T stCfgMng = {0};

    CUSTOM_ASSERT(!pstDevParam, return RET_ERR);
    SPI_FLASH_READ(PART_CONFIG, dwOffset, (uint8_t *)&stCfgMng, sizeof(CFG_MNG_T));
    /* 首次上电/损坏时 magic 非法是预期路径，走 restore，不要当致命 ASSERT 刷屏 */
    if (DEV_MNG_MAGIC != stCfgMng.dwMagic) {
        DEVCFG_DEBUG(DEVCFG_WARN"cfg magic invalid: 0x%08x (expect 0x%08x)\n",
                     stCfgMng.dwMagic, DEV_MNG_MAGIC);
        return RET_ERR;
    }
    
    dwOffset = sizeof(CFG_MNG_T) + FATE_ID_MAX*sizeof(FATE_NODE_T);
    if(SPI_FLASH_READ(PART_CONFIG, dwOffset, (uint8_t*)pstDevParam, sizeof(DEV_PARAM_T)) < 0)
    {
        DEVCFG_DEBUG(DEVCFG_ERROR"[%s:%d]SPI_FLASH_READ err!\n",__FUNCTION__,__LINE__);
        return RET_ERR;
    }
    return RET_OK;
}


/*****************************************************
 * @fn       devRestore
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void devInfoParamRestore()
{

    /* TODO 加锁 */

    /* 恢复默认参数 */
    g_pstDevParam->stDevParam.byDebugLevel = DLEVEL_REPORT;



    return;
}


/*****************************************************
 * @fn       devRestore
 * @brief    恢复默认参数
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int devCfgRestore()
{
    uint32_t dwCrc = 0;
    DEV_PARAM_T stDevParam = {0}; 

    CUSTOM_ASSERT(!g_pstDevParam, return RET_ERR);

    devInfoParamRestore();

    /* TODO, 恢复其他参数 */

    /* 写入新的配置 */
    rebuildCfg(0);

#if DEVPARAM_CRC_ENABLE
    /* 读取新的配置以生成crc */
    readDevCfg(&stDevParam);

    dwCrc = crc32_checksum((uint8_t *)&stDevParam, sizeof(DEV_PARAM_T));
    /* 主要是为了更新CRC */
    rebuildCfg(dwCrc);
#endif
    return RET_OK;
}



static void devParam_Mng_task(void *arg)
{
    uint32_t dwCrc32 = 0;
    DEVINFO_PARAM_T stDevParam = {0};

    UNUSED_ARG(arg);

    FOREVER
    {
        /* 配置参数变化时重新写入flash */
        if(g_stDevParamMng.bySave)
        {
            os_printf("11111111111111111111\n");
            /* 写入新的配置 */
            rebuildCfg(0);
            DEVCFG_DEBUG(DEVCFG_WARN"dev param update to flash!\n");
#if DEVPARAM_CRC_ENABLE
            /* 读取新的配置以生成crc */
            readDevCfg(&stDevParam);

            dwCrc32 = crc32_checksum((uint8_t *)&stDevParam, sizeof(DEV_PARAM_T));
            /* 主要是为了更新CRC */
            rebuildCfg(dwCrc32);            
#endif
            os_printf("11111111111111111111\n");
            g_stDevParamMng.bySave = 0;
        }

        os_sleep_ms(500);
    }
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int devCfg_init()
{
    if (g_pstDevParam) {
        return RET_OK;
    }

    g_pstDevParam = (DEV_PARAM_PTR)malloc(sizeof(DEV_PARAM_T));
    CUSTOM_ASSERT(!g_pstDevParam, return RET_ERR);
    memset(g_pstDevParam, 0, sizeof(DEV_PARAM_T));

    os_mutex_init(g_stDevParamMng.lock);

    /* 启动shell cmd 任务 */
    sys_thread_new("devParamMng_task", devParam_Mng_task, NULL, 128, 3);

    return RET_OK;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int devPara_init()
{
    /* 依赖 devCfg_init 已分配 g_pstDevParam；若顺序异常则自愈 */
    if (!g_pstDevParam) {
        if (devCfg_init() < 0) {
            return RET_ERR;
        }
    }

    /* 从flash中读取配置参数, 如果设备第一次初始化或参数损坏
     * 则恢复默认配置并写入到flash 
     */
    if(readDevCfg(g_pstDevParam) < 0)
    {
        DEVCFG_DEBUG(DEVCFG_WARN"readDevCfg fail, restore defaults...\n");
        if (devCfgRestore() < 0) {
            DEVCFG_DEBUG(DEVCFG_ERROR"devCfgRestore failed\n");
            return RET_ERR;
        }
        if (readDevCfg(g_pstDevParam) < 0) {
            DEVCFG_DEBUG(DEVCFG_ERROR"readDevCfg still fail after restore\n");
            return RET_ERR;
        }
        DEVCFG_DEBUG(DEVCFG_REPORT"dev cfg restored OK\n");
    }

    waitDevCfgModuleInit = TRUE;

    setDebugLevel(g_pstDevParam->stDevParam.byDebugLevel);

    /* TODO 其他参数生效 */

    return RET_OK;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int readDevParam(DEV_PARAM_PTR pStDevParam)
{
    DEVINFO_PARAM_T stDevParam = {0};
    FATE_NODE_T stFate = {0};
    uint32_t dwOffset = 0;
    //uint32_t dwCrc32 = 0;

    CUSTOM_ASSERT(NULL == pStDevParam, return RET_ERR);

    //SPI_FLASH_READ(PART_CONFIG, 0, (uint8_t*)&stFateMng, sizeof(FATE_MNG_T));
    
    //dwOffset = DEV_FATE_ID*sizeof(FATE_NODE_T);
    dwOffset = sizeof(CFG_MNG_T) + DEV_FATE_ID*sizeof(FATE_NODE_T);
    SPI_FLASH_READ(PART_CONFIG, dwOffset, (uint8_t*)&stFate, sizeof(FATE_NODE_T));

    DEVCFG_DEBUG(
        DEVCFG_PREFIX "stFate.type:%d, dwMagic:0x%x, fateOffset:0x%x, entryOffset:0x%x, entryLen:%d, entryNum:%d\n",
        stFate.type,
        stFate.dwMagic,
        stFate.fateOffset,
        stFate.entryOffset,
        stFate.entryLen,
        stFate.entryNum
    );

    CUSTOM_ASSERT(DEVINFO_MAGIC != stFate.dwMagic, return RET_ERR);
    CUSTOM_ASSERT(4*1024 < stFate.entryOffset, return RET_ERR);

    SPI_FLASH_READ(PART_CONFIG, stFate.entryOffset, (uint8_t*)&stDevParam, sizeof(DEVINFO_PARAM_T));
    //dwCrc32 = crc32_checksum((uint8_t *)&stDevParam,  sizeof(DEVINFO_PARAM_T));

    memcpy(pStDevParam, &stDevParam, sizeof(DEVINFO_PARAM_T));
    return RET_OK;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int getDevInfoParam(DEVINFO_PARAM_PTR pStDevInfoParam)
{
    CUSTOM_ASSERT(!pStDevInfoParam, return RET_ERR);
    waitModuleInit();

    os_mutex_lock(g_stDevParamMng.lock);
    memcpy(pStDevInfoParam, &(g_pstDevParam->stDevParam), sizeof(DEVINFO_PARAM_T));
    os_mutex_unlock(g_stDevParamMng.lock);

    return RET_OK;
}

#if 0
/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int writeDevParam(DEV_PARAM_PTR pStDevParam)
{
    DEVINFO_PARAM_T stDevParam = {0};
    //FATE_MNG_T  stFateMng = {0};
    FATE_NODE_T stFate = {0};
    uint32_t dwOffset = 0;

    CUSTOM_ASSERT(NULL == pStDevParam, return RET_ERR);

    /* 配置文件偏移 = FATE表大小(总) + 之前的配置文件大小 */
    dwOffset = sizeof(CFG_MNG_T)+(FATE_ID_MAX)*sizeof(FATE_NODE_T);
    stFate.dwMagic = DEVINFO_MAGIC;
    stFate.type = PART_CONFIG;
    stFate.offset = dwOffset;
    stFate.len = sizeof(DEVINFO_PARAM_T);
    stFate.crc32 = crc32_checksum((uint8_t *)(pStDevParam), sizeof(DEVINFO_PARAM_T));

    /* 写入新的配置 */
    SPI_FLASH_WRITE(PART_CONFIG, dwOffset, (uint8_t*)pStDevParam, sizeof(DEVINFO_PARAM_T));
#if 0
    SPI_FLASH_READ(PART_CONFIG, dwOffset, (uint8_t*)&stFate, sizeof(FATE_NODE_T));
    DEVCFG_DEBUG(DEVCFG_PREFIX"stFate.type:%d, dwMagic:0x%x, offset:0x%x, len:%d, crc32:%d\n", \
                                    stFate.type,                                                 \
                                    stFate.dwMagic,                                              \ 
                                    stFate.offset,                                               \ 
                                    stFate.len,                                                  \
                                    stFate.crc32 );

#endif

    /* FATE表偏移 */
    dwOffset = DEV_FATE_ID*CFGPARA_SIZE;
    /* FATE表信息更新 */
    SPI_FLASH_WRITE(PART_CONFIG, dwOffset, (uint8_t*)&stFate, sizeof(FATE_NODE_T));

    return RET_OK;
}
#endif

/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int setDevInfoParam(DEVINFO_PARAM_PTR pStDevInfoParam)
{
    CUSTOM_ASSERT(!pStDevInfoParam, return RET_ERR);
    waitModuleInit();
    
    os_mutex_lock(g_stDevParamMng.lock);
    memcpy(&(g_pstDevParam->stDevParam), pStDevInfoParam, sizeof(DEVINFO_PARAM_T));    
    os_mutex_unlock(g_stDevParamMng.lock);

    return RET_OK;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void devParamSave()
{
    waitModuleInit();
    os_mutex_lock(g_stDevParamMng.lock);
    g_stDevParamMng.bySave = 1;
    os_mutex_unlock(g_stDevParamMng.lock);
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void devParamSaveNow()
{
    uint32_t dwCrc = 0;
    DEV_PARAM_T stDevParam = {0};

    waitModuleInit();
    if(g_stDevParamMng.bySave)
    {
        /* TODO, 恢复其他参数 */
        rebuildCfg(0);
#if DEVPARAM_CRC_ENABLE
        readDevCfg(&stDevParam);

        dwCrc = crc32_checksum((uint8_t *)&stDevParam, sizeof(DEV_PARAM_T));

        rebuildCfg(dwCrc);
#endif        
    }

    os_mutex_lock(g_stDevParamMng.lock);   
    g_stDevParamMng.bySave = 0;
    os_mutex_unlock(g_stDevParamMng.lock);
}



