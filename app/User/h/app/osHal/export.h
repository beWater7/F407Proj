/***************************************************************
 * @file    :  export.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-12-15
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __XPLAT_EXPORT_H__
#define __XPLAT_EXPORT_H__

#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif


#define XPLAT_SECTION(x)             __attribute__((section(x)))
#define XPLAT_USED                   __attribute__((used))
#define XPLAT_ALIGN(n)               __attribute__((aligned(n)))
#define XPLAT_WEAK                   __attribute__((weak))
#define xplat_inline                 static __inline
#define xplat_likely(x)              (x)
#define xplat_unlikely(x)            (x)


/* public define ------------------------------------------------------------ */
#define EXPORT_ID_INIT                  (0x97979797)
#define EXPORT_ID_POLL                  (0xabcdabcd)  
#define EXPORT_LEVEL_MIN                (-64)

typedef int32_t                         xplat_pointer_t;
/* public define ------------------------------------------------------------ */
enum xplat_export_level
{
    EXPORT_POLL                         = -2,
    EXPORT_UNIT_TEST                    = -1,
    EXPORT_LEVEL_BSP                    = 0,
    EXPORT_LEVEL_HW_INDEPNEDENT         = 0,
    EXPORT_DRIVER                       = 1,
    EXPORT_MIDWARE                      = 2,
    EXPORT_DEVICE                       = 3,
    EXPORT_APP                          = 4,
    EXPORT_USER                         = 5,
};

/* public typedef ----------------------------------------------------------- */
typedef struct xplat_export_poll_data
{
    uint32_t timeout_ms;
} xplat_export_poll_data_t;

typedef struct xplat_export
{
    uint32_t magic_head;
    const char *name;
    void *func;
    void *data;
    bool exit;
    int8_t level;
    uint8_t type;
    uint32_t period_ms;
    uint32_t magic_tail;
} xplat_export_t;

/* private function --------------------------------------------------------- */
void xplat_unit_test(void);
void xplat_run(void);

/* public export ------------------------------------------------------------ */
/**
  * @brief  Initialization function exporting macro.
  * @param  _func   The initialization function.
  * @param  _level  The export level, [0, 127].
  * @retval None.
  */  
/* 每个导出放入独立段 xplat_export.<func>，配合 link.ld 的 SORT_BY_NAME，
 * 保证同 level 内按函数名字母序执行（避免同 .o 内声明顺序/反序导致依赖颠倒）。 */
#define INIT_EXPORT(_func, _level)                                             \
    XPLAT_USED const xplat_export_t init_##_func                               \
        XPLAT_SECTION("xplat_export." #_func) =                                \
    {                                                                          \
        .name = #_func,                                                        \
        .func = (void *)&_func,                                                \
        .level = _level,                                                       \
        .exit = false,                                                         \
        .magic_head = EXPORT_ID_INIT,                                          \
        .magic_tail = EXPORT_ID_INIT,                                          \
    }

/**
  * @brief  Exiting function exporting macro.
  * @param  _func   The polling function.
  * @param  _level  The export level, [0, 127].
  * @retval None.
  */  
#define EXIT_EXPORT(_func, _level)                                             \
    XPLAT_USED const xplat_export_t exit_##_func                               \
        XPLAT_SECTION("xplat_export." #_func) =                                \
    {                                                                          \
        .name = #_func,                                                        \
        .func = (void *)&_func,                                                \
        .level = _level,                                                       \
        .exit = true,                                                          \
        .magic_head = EXPORT_ID_INIT,                                          \
        .magic_tail = EXPORT_ID_INIT,                                          \
    }

/**
  * @brief  Unit test function exporting macro.
  * @param  _func   The unit test function.
  * @retval None.
  */
#define UNIT_TEST_EXPORT(_func)             INIT_EXPORT(_func, EXPORT_UNIT_TEST)

/**
  * @brief  Poll function exporting macro.
  * @param  _func       The polling function.
  * @param  _period_ms  The polling period in ms. 
  *                     The max value is 30 days (2592000000ms).
  * @retval None.
  */
#define POLL_EXPORT(_func, _period_ms)                                         \
    static xplat_export_poll_data_t poll_##_func##_data =                       \
    {                                                                          \
        .timeout_ms = 0,                                                       \
    };                                                                         \
    XPLAT_USED const xplat_export_t poll_##_func XPLAT_SECTION("expoll") =        \
    {                                                                          \
        .name = "poll",                                                        \
        .func = &_func,                                                        \
        .data = (void *)&poll_##_func##_data,                                  \
        .level = EXPORT_POLL,                                                  \
        .period_ms = (uint32_t)(_period_ms),                                   \
        .magic_head = EXPORT_ID_POLL,                                          \
        .magic_tail = EXPORT_ID_POLL,                                          \
    }


#define POLL_FUNC_ENABLE     0

#ifdef __cplusplus
}
#endif

#endif /* __XPLAT_EXPORT_H__ */

/* ----------------------------- end of file -------------------------------- */

