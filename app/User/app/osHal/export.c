/***************************************************************
 * @file    :  export.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-12-15
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#include "export.h"

#define XPLAT_POLL_PERIOD_MAX                        (2592000000)    /* 30 days */

#if defined(__linux__)
#define STR_ENTER                                   "\n"
#else
#define STR_ENTER                                   "\r\n"
#endif

/* link.ld 中为 INIT_EXPORT / POLL_EXPORT 段定义的起止符号 */
extern xplat_export_t __xplat_export_start[];
extern xplat_export_t __xplat_export_end[];
extern xplat_export_t __expoll_start[];
extern xplat_export_t __expoll_end[];

/* private function prototype ----------------------------------------------- */
static void module_null_init(void);
static void _init_func_execute(int8_t level);
static void _get_init_export_table(void);
#if POLL_FUNC_ENABLE
static void _get_poll_export_table(void);
static void _poll_func_execute(void);
#endif

/* private variables -------------------------------------------------------- */
INIT_EXPORT(module_null_init, 0);
POLL_EXPORT(module_null_init, (1000 * 60 * 60));

static xplat_export_t *export_init_table = NULL;
static uint32_t count_export_init = 0;
#if POLL_FUNC_ENABLE
static xplat_export_t *export_poll_table = NULL;
static uint32_t count_export_poll = 0;
#endif
static int8_t export_level_max = EXPORT_LEVEL_MIN;


/**
  * @brief  null exporting function.
  * @retval None
  */
static void module_null_init(void)
{
    /* NULL */
}


/**
  * @brief  Get the init export table（依赖 link.ld 连续段 __xplat_export_*）。
  */
static void _get_init_export_table(void)
{
    xplat_export_t *start = __xplat_export_start;
    xplat_export_t *end   = __xplat_export_end;
    uint32_t i;

    if (end <= start) {
        export_init_table = NULL;
        count_export_init = 0;
        return;
    }

    export_init_table = start;
    count_export_init = (uint32_t)(end - start);
    export_level_max = EXPORT_LEVEL_MIN;

    for (i = 0; i < count_export_init; i++) {
        if (export_init_table[i].magic_head != EXPORT_ID_INIT ||
            export_init_table[i].magic_tail != EXPORT_ID_INIT) {
            /* 表被插缝或长度不对，截断到首个非法项 */
            count_export_init = i;
            break;
        }
        if (export_init_table[i].level > export_level_max) {
            export_level_max = export_init_table[i].level;
        }
    }
}


/**
  * @brief  init exporting function executing.
  * @retval None
  */
static void _init_func_execute(int8_t level)
{
    /* Execute the poll function in the specific level. */
    for (uint32_t i = 0; i < count_export_init; i++)
    {
        if (export_init_table[i].level == level)
        {
            if (!export_init_table[i].exit)  
            {
                if (level != EXPORT_UNIT_TEST)
                {
                    //printf("Export init %s." STR_ENTER, export_init_table[i].name);
                }
                ((void (*)(void))export_init_table[i].func)();
            }
        }
    }
}


#if POLL_FUNC_ENABLE
/**
  * @brief  Get the polling export table（依赖 link.ld 连续段 __expoll_*）。
  */
static void _get_poll_export_table(void)
{
    xplat_export_t *start = __expoll_start;
    xplat_export_t *end   = __expoll_end;
    uint32_t i;

    if (end <= start) {
        export_poll_table = NULL;
        count_export_poll = 0;
        return;
    }

    export_poll_table = start;
    count_export_poll = (uint32_t)(end - start);

    for (i = 0; i < count_export_poll; i++) {
        if (export_poll_table[i].magic_head != EXPORT_ID_POLL ||
            export_poll_table[i].magic_tail != EXPORT_ID_POLL) {
            count_export_poll = i;
            break;
        }
        xplat_export_poll_data_t *data =
            (xplat_export_poll_data_t *)export_poll_table[i].data;
        data->timeout_ms = sys_jiffies() + export_poll_table[i].period_ms;
    }
}


/**
  * @brief  eLab polling exporting function executing.
  * @retval None
  */
static void _poll_func_execute(void)
{
    xplat_export_poll_data_t *data;
    
    /* Execute the poll function in the specific level. */
    for (uint32_t i = 0; i < count_export_poll; i ++)
    {
        data = export_poll_table[i].data;

        while (1)
        {
            uint64_t _time = (uint64_t)xplat_time_ms();
            if (_time < (uint64_t)data->timeout_ms &&
                ((uint64_t)data->timeout_ms - _time) <=
                    (UINT32_MAX - XPLAT_POLL_PERIOD_MAX))
            {
                _time += (UINT32_MAX + 1);
            }

            if (_time >= (uint64_t)data->timeout_ms)
            {
                data->timeout_ms += export_poll_table[i].period_ms;
                ((void (*)(void))export_poll_table[i].func)();
            }
            else
            {
                break;
            }
        }
    }
}
#endif


/**
  * @brief  run
  * @retval None
  */
void xplat_run(void)
{
    uint8_t level = 0;
    /* get func in section */
    _get_init_export_table();
#if POLL_FUNC_ENABLE
    _get_poll_export_table();
#endif

    /* Initialize all module */
    for (level = 0; level <= export_level_max; level++)
    {
        _init_func_execute(level);
    }

    /* Start polling function */
#if POLL_FUNC_ENABLE
    while (1)
    {
        _poll_func_execute();
    }
#endif
}

