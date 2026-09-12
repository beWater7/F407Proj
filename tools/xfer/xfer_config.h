/*
 * xfer_config.h — xfer 烧录工具的集中配置(用户可调项)
 *
 * 用法: 默认即可; 需要覆盖时在 make 命令行传宏, 例如:
 *   make -C tools/xfer EXTRA_CFLAGS=-DXFER_PROGRESS_COLOR=3
 *
 * 约定:
 *   - 本文件只放"用户/维护者想调"的参数(进度条样式、默认波特率等)。
 *   - 协议常量(SOH/STX/EOT、PKT_128/1K、upg/loader 魔数)是对端约定值,
 *     必须与 boot/loader 严格一致, 留在 flash_upg.c, 不属可调配置。
 *   - 进度条色值可整体覆盖成任意 ANSI 色:
 *       PROG_BAR_COLOR 填充段前景色(如 "\x1b[35m" 品红)
 *       PROG_BAR_RESET  色段结束复位(通常 "\x1b[0m")
 */
#ifndef XFER_CONFIG_H
#define XFER_CONFIG_H

/* ============ 升级进度条 ============ */
/* XFER_PROGRESS_BAR: 1 终端画 [####----] 条(默认); 0 退回纯文本百分比 */
#ifndef XFER_PROGRESS_BAR
#define XFER_PROGRESS_BAR 1
#endif

/* XFER_PROGRESS_COLOR: 0 无色; 1 绿(默认); 2 青; 3 黄 */
#ifndef XFER_PROGRESS_COLOR
#define XFER_PROGRESS_COLOR 1
#endif

#ifndef PROG_BAR_COLOR
#if XFER_PROGRESS_COLOR == 2
#define PROG_BAR_COLOR  "\x1b[36m"        /* 青 */
#elif XFER_PROGRESS_COLOR == 3
#define PROG_BAR_COLOR  "\x1b[33m"        /* 黄 */
#elif XFER_PROGRESS_COLOR >= 1
#define PROG_BAR_COLOR  "\x1b[32m"        /* 绿(默认) */
#else
#define PROG_BAR_COLOR  ""                /* 0: 无色 */
#endif
#endif

#ifndef PROG_BAR_RESET
#if XFER_PROGRESS_COLOR >= 1
#define PROG_BAR_RESET  "\x1b[0m"
#else
#define PROG_BAR_RESET  ""
#endif
#endif

/* ============ 默认运行参数 ============ */
/* 未加 -b/--baud、--retries 时使用的默认值 */
#ifndef XFER_DEFAULT_BAUD
#define XFER_DEFAULT_BAUD   115200
#endif

#ifndef XFER_DEFAULT_RETRIES
#define XFER_DEFAULT_RETRIES 3
#endif

/* ============ 串口占用抢占 ============ */
/* 端口被占用时, 最多列出/尝试抢占多少个进程(+1 之外的只提示不处理) */
#ifndef XFER_PREEMPT_MAX_HOLDERS
#define XFER_PREEMPT_MAX_HOLDERS 16
#endif

/* 发 SIGTERM 后等待端口释放的时长(ms)。占用方(如 serialTerm)响应信号 +
 * 关闭串口需要时间, 给足余量; 超时仍未释放则升级 SIGKILL。 */
#ifndef XFER_PREEMPT_TERM_WAIT_MS
#define XFER_PREEMPT_TERM_WAIT_MS 1500
#endif

/* 发 SIGKILL 后等待端口释放的时长(ms)。SIGKILL 无法被忽略, 通常远快于上值。 */
#ifndef XFER_PREEMPT_KILL_WAIT_MS
#define XFER_PREEMPT_KILL_WAIT_MS 1000
#endif

#endif /* XFER_CONFIG_H */
