/***************************************************************
 * @file    : os_log.h
 * @brief   : 统一模块化打印层。
 *
 *   在 os_printf_api(等级/颜色/tick/telnet/UART 直写)之上增加
 *   "模块"维度：
 *     - 每条日志自动带 [模块名] 前缀，来源为模块注册表；
 *     - 每个模块有独立运行时开关(g_log_mods[].enabled)，
 *       shell `dbg` 命令可开可关，off 后该模块彻底静默；
 *     - 等级语义与 os_debug.h 的 DLEVEL_* 完全一致，仍受全局
 *       debugLevel(__printf_level__) 过滤。
 *
 *   用法示例：
 *       LOGE(LOG_MOD_ESP8266, "join AP timeout\n");
 *       LOGI(LOG_MOD_DEVCFG,  "restored OK\n");
 *
 * @note : 替换旧式"每模块一套 XXX_DEBUG_ENABLE/XXX_ERROR"样板，
 *         模块只需注册一行表项即可获得 前缀+等级+运行时开关。
 ***************************************************************/
#ifndef __OS_LOG_H__
#define __OS_LOG_H__

#include "os_debug.h"   /* DLEVEL_*、__printf_level__、os_print_buf_flush */

/* 模块注册表索引。新增模块时：
 *   1. 在此枚举追加一项(LOG_MOD_MAX 之前)；
 *   2. 在 os_log.c 的 g_log_mods[] 中追加对应 {name, prefix, enabled}。 */
typedef enum {
    LOG_MOD_SYS = 0,     /* 未分类 / 系统日志      "[SYS] "     */
    LOG_MOD_DEVCFG,      /* 参数/分区配置          "[DEVCFG] "  */
    LOG_MOD_ESP8266,     /* ESP8266 驱动           "[ESP8266] " */
    LOG_MOD_NET,         /* 以太网/DHCP            "[NET] "     */
    LOG_MOD_HTTP,        /* HTTP/Web 资源          "[HTTP] "    */
    LOG_MOD_MAX
} log_mod_t;

typedef struct {
    const char *name;    /* shell 匹配短名，小写，如 "esp8266"     */
    const char *prefix;  /* 日志显示前缀，含 [] 与空格            */
    uint8_t     enabled; /* 运行时开关：0=该模块整段静默(含错误)  */
} log_mod_tab_t;

extern log_mod_tab_t g_log_mods[LOG_MOD_MAX];

/* 模块化日志主入口。
 * @param mod   模块索引(log_mod_t)
 * @param level 打印等级(DLEVEL_ALERT..DLEVEL_TRACE)
 * @param fmt   printf 风格格式串(无需再带 [模块] 前缀)
 *
 * 过滤链(两级 AND)：
 *   1. 模块闸  g_log_mods[mod].enabled==0        -> 直接丢弃
 *   2. 等级闸  level > __printf_level__          -> 丢弃
 * 通过后渲染 颜色 + [tick] + [模块前缀] + body 输出。 */
void os_log(uint8_t mod, uint8_t level, const char *fmt, ...);

/* 等级便捷宏：一套宏服务所有模块，模块仍是参数 */
#define LOG(mod, lvl, ...)  os_log(mod, lvl, __VA_ARGS__)
#define LOGE(mod, ...) os_log(mod, DLEVEL_ERROR,  __VA_ARGS__)
#define LOGW(mod, ...) os_log(mod, DLEVEL_WARN,   __VA_ARGS__)
#define LOGR(mod, ...) os_log(mod, DLEVEL_REPORT, __VA_ARGS__) /* 默认阈值下可见 */
#define LOGI(mod, ...) os_log(mod, DLEVEL_INFO,   __VA_ARGS__) /* 默认阈值下隐藏 */
#define LOGT(mod, ...) os_log(mod, DLEVEL_TRACE,  __VA_ARGS__) /* 需 debugLevel 5 */

#endif /* __OS_LOG_H__ */
