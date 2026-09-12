/*
 * flash_upg.c — Linux 串口烧录 upg.bin
 *
 * 首次（SPI 上还没有 loader）：boot 走 XMODEM-CRC 收 loader blob，再进 SRAM loader。
 * 之后：DTR 复位 → 按 U 留在 loader CLI → `upgrade` → YMODEM 整包 upg.bin。
 *
 * 用法:
 *   ./tools/xfer/xfer /dev/ttyUSB0 dist/upg.bin
 *   ./tools/xfer/xfer --no-reset /dev/ttyUSB0 dist/upg.bin
 */
#define _DEFAULT_SOURCE
#include "xfer.h"
#include "xfer_config.h"   /* 进度条样式/默认波特率等可调项 */

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SOH   0x01
#define STX   0x02
#define EOT   0x04
#define ACK   0x06
#define NAK   0x15
#define CA    0x18
#define CRC_C 0x43
/* 高速确认字节: 新 loader 打印 BAUD 后会在 115200 等该字节(≤1s),
 * 收到才切高速; 不发则 loader 保持 115200 —— 双向确认, 单端无法强留 115200 */
#define BAUD_CONFIRM 'B'

#define PKT_128 128
#define PKT_1K  1024

#define UPG_HDR_MAGIC_LDR 0x55475023u
#define LOADER_HDR_MAGIC  0x52444C32u

enum phase {
    PH_BOOT_XMODEM = 1,
    PH_LOADER_WAIT_U,
    PH_LOADER_CLI,
    PH_LOADER_YMODEM,
    PH_TIMEOUT
};

static int now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

/* 板子串口原始输出回显。默认关闭：只显示烧录进度/phase/错误，
 * 想看板子日志时加 -v/--verbose 开启。 */
static int g_verbose = 0;
static int g_no_fast = 0;   /* --no-fast: 禁用高速 YMODEM 协商, 全程基线波特率 */

static void echo_rx(uint8_t c)
{
    if (!g_verbose) {
        return;
    }
    if (c == '\r') {
        fputc('\n', stderr);
    } else if (c == '\n' || (c >= 32 && c < 127)) {
        fputc((int)c, stderr);
    }
}

/* 回显段结束的换行：仅 verbose 时补，避免默认模式出现空行 */
static void echo_nl(void)
{
    if (g_verbose) {
        fputc('\n', stderr);
    }
}

/* ==================== 升级三步计时 & 等待动画 ====================
 * 步1 复位&引导识别(复位脉冲+嗅探), 步2 进入升级模式(upgrade/CLI+握手),
 * 步3 YMODEM/XMODEM 传输(真实字节进度, 见下方 progress_render2)。
 *
 * 步1/2 是"等远端事件", 没有真实总量, 用 label + 实时秒数 + 旋转符
 * 表示进行中 —— 诚实不伪造百分比; verbose 或非 tty 时不做行内动画,
 * 退化为逐行文本。
 *
 * 步3 字节条在 ACK 等待期(loader 擦/写 Flash)百分比不变, 但时间仍在走,
 * 因此 wait_ack 空窗里会周期重绘(速率/耗时/ETA 持续刷新), 不显得卡死。 */
#define STEP_MAX 3

struct step_rec {
    const char *label;   /* 步骤名 */
    long        t0;      /* 开始时刻(ms) */
    long        dur_ms;  /* 结束时算出 */
    uint64_t    bytes;   /* 该步传输的字节数(0=非传输步) */
    int         used;
};

static struct step_rec s_steps[STEP_MAX];
static int   s_step_cur = -1;   /* 当前进行中的步骤 */
static int   s_anim_on;         /* 当前步是否允许行内动画 */
static int   s_anim_painted;    /* 是否已输出动画帧(全行 printf 前须先换行) */
static long  s_anim_last;
static long  s_attempt_t0;
static const char s_spin[] = "|/-\\";

/* 关闭行内动画帧: 任何"全行"printf 之前先调, 否则会接在半行帧后面 */
static void anim_clear(void)
{
    if (s_anim_painted) {
        fputc('\n', stderr);
        s_anim_painted = 0;
    }
}

/* 等待循环周期调用: 步1/2 的进行中动画(label + 秒数 + 旋转符) */
static void step_tick(void)
{
    long now;

    if (!s_anim_on || s_step_cur < 0) {
        return;
    }
    now = now_ms();
    if (now - s_anim_last < 200) {
        return;
    }
    s_anim_last = now;
    s_anim_painted = 1;
    fprintf(stderr, "\r%-16s %5.1fs %c", s_steps[s_step_cur].label,
            (now - s_steps[s_step_cur].t0) / 1000.0,
            s_spin[(now / 200) % 4]);
    fflush(stderr);
}

static void step_begin(int idx, const char *label, int animate)
{
    s_steps[idx].label = label;
    s_steps[idx].t0 = now_ms();
    s_steps[idx].dur_ms = 0;
    s_steps[idx].bytes = 0;
    s_steps[idx].used = 1;
    s_step_cur = idx;
    /* 只有"纯等待"的步(复位识别/进升级)才画旋转动画;
     * 带字节条的传输步(XMODEM/YMODEM)由 progress_render2 负责, 不再画动画 */
    s_anim_on = animate && !g_verbose && isatty(fileno(stderr));
    s_anim_painted = 0;
    s_anim_last = 0;
    if (!s_anim_on) {
        fprintf(stderr, "%s ...\n", label);
    }
}

static void step_end(int idx, int show_done)
{
    s_steps[idx].dur_ms = now_ms() - s_steps[idx].t0;
    anim_clear();
    s_anim_on = 0;
    s_step_cur = -1;
    if (show_done && s_steps[idx].used) {
        fprintf(stderr, "%s done in %.1fs\n", s_steps[idx].label,
                s_steps[idx].dur_ms / 1000.0);
    }
}

static void steps_reset(void)
{
    s_attempt_t0 = now_ms();
    memset(s_steps, 0, sizeof(s_steps));
    s_step_cur = -1;
    s_anim_on = 0;
    s_anim_painted = 0;
}

/* YMODEM done 之后的升级耗时汇总 */
static void steps_summary(void)
{
    int i;
    int n = 0;
    double total = (now_ms() - s_attempt_t0) / 1000.0;

    for (i = 0; i < STEP_MAX; i++) {
        if (s_steps[i].used) {
            n++;
        }
    }
    fprintf(stderr, "upgrade done in %.1fs (%d steps)\n", total, n);
    return;
}

/* ==================== 传输字节条: 百分比 + 速率 + 耗时 + ETA ==================== */

struct pstate {
    int  pct;       /* 上次绘制百分比, 初 -1 */
    long t0;        /* 传输开始时刻(ms) */
    long last_t;    /* 上次绘制时刻 */
};

static int s_pb_drawn;   /* tty 下字节条处于半行 */

static void pstate_init(struct pstate *ps)
{
    ps->pct = -1;
    ps->t0 = now_ms();
    ps->last_t = ps->t0;
}

/* 传输进度条, 同一行 \r 覆写, 类似 apt/dnf 装包:
 *   tty     : "YMODEM   [########----------]  45%  160K/356K  12.1 KB/s  2.3s  ETA 4.1s"
 *   非 tty  : 每行 "YMODEM: 45% (...)", 避免 \r 污染重定向日志
 *   百分比未变时 tty 仍最多 2 帧/秒刷新(耗时/速率继续走, 覆盖 loader 擦除空窗)。 */
static void progress_render2(const char *label, uint64_t cur, uint64_t total,
                             struct pstate *ps)
{
    const int BAR_W = 24;
    int pct;
    int filled;
    int i;
    long now;
    double kbs;
    double el;

    if (total == 0u) {
        return;
    }
    now = now_ms();
    pct = (int)((cur * 100u) / total);

    /* 已画过且百分比没变: tty 节流 500ms 重绘一次, 非 tty 不再打行 */
    if (ps->pct == pct && ps->pct >= 0) {
        if (!isatty(fileno(stderr)) || now - ps->last_t < 500) {
            return;
        }
    }
    ps->pct = pct;
    ps->last_t = now;

    el = (now - ps->t0) / 1000.0;
    kbs = (el > 0.0) ? ((double)cur / 1024.0) / el : 0.0;

    if (!isatty(fileno(stderr))) {
        fprintf(stderr, "%s: %3d%% (%llu/%llu) %5.1f KB/s %4.1fs\n", label, pct,
                (unsigned long long)cur, (unsigned long long)total, kbs, el);
        return;
    }
    s_pb_drawn = 1;
#if XFER_PROGRESS_BAR == 0
    fprintf(stderr, "\r%-8s %3d%% (%llu/%llu) %5.1f KB/s %4.1fs", label, pct,
            (unsigned long long)cur, (unsigned long long)total, kbs, el);
#else
    filled = (BAR_W * pct) / 100;
    fprintf(stderr, "\r%-8s %s[", label, PROG_BAR_COLOR);
    for (i = 0; i < BAR_W; i++) {
        fputc(i < filled ? '#' : '-', stderr);
    }
    fprintf(stderr, "]%s %3d%%  %llu/%llu  %5.1f KB/s  %4.1fs  ETA %4.1fs",
            PROG_BAR_RESET, pct,
            (unsigned long long)cur, (unsigned long long)total, kbs, el,
            (kbs > 0.5) ? ((double)(total - cur) / 1024.0) / kbs : 0.0);
#endif
    fflush(stderr);
}

/* ============ 传输回调绑定: 供 wait_ack 空窗周期重绘 ============ */
static const char    *s_pb_label;
static uint64_t       s_pb_cur;
static uint64_t       s_pb_total;
static struct pstate *s_pb_ps;

static void pbar_bind(const char *label, uint64_t cur, uint64_t total,
                      struct pstate *ps)
{
    s_pb_label = label;
    s_pb_cur = cur;
    s_pb_total = total;
    s_pb_ps = ps;
}

static void pbar_unbind(void)
{
    s_pb_ps = NULL;
}

/* wait_ack 等读到空闲时调用: 传输百分比不动, 但刷新耗时/速率/ETA */
static void pbar_idle_repaint(void)
{
    if (s_pb_ps != NULL && s_pb_total != 0u) {
        progress_render2(s_pb_label, s_pb_cur, s_pb_total, s_pb_ps);
    }
}

/* 字节条 100% 后手动补了换行, 复位半行标记 */
static void pbar_line_done(void)
{
    s_pb_drawn = 0;
}

/* 出错前调用: 字节条还停在半行时先补换行, 再打错误行 */
static void pbar_clear(void)
{
    if (s_pb_drawn) {
        fputc('\n', stderr);
        s_pb_drawn = 0;
    }
}

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t size)
{
    uint16_t crc = 0;
    uint32_t i;
    int j;

    for (i = 0; i < size; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int xfer_putc(const xfer_ops_t *ops, void *ctx, uint8_t c)
{
    return ops->write(ctx, &c, 1);
}

static int wait_ack(const xfer_ops_t *ops, void *ctx, int timeout_ms)
{
    uint8_t c;
    int t0 = now_ms();

    while (now_ms() - t0 < timeout_ms) {
        int n = ops->read(ctx, &c, 1, 50);
        if (n < 0) {
            return -1;
        }
        if (n == 1) {
            if (c == ACK) {
                return 0;
            }
            if (c == NAK || c == CRC_C) {
                return 1;
            }
            if (c == CA) {
                return -2;
            }
            echo_rx(c);
        }
        if (n == 0) {
            step_tick();          /* 步1/2 等待动画 */
            pbar_idle_repaint();  /* 步3 传输空窗(loader 擦/写 Flash)持续刷新 */
        }
    }
    return -4;
}

static int wait_byte(const xfer_ops_t *ops, void *ctx, uint8_t want, int timeout_ms)
{
    uint8_t c;
    int t0 = now_ms();

    while (now_ms() - t0 < timeout_ms) {
        int n = ops->read(ctx, &c, 1, 50);
        if (n < 0) {
            return -1;
        }
        if (n == 1) {
            if (c == want) {
                return 0;
            }
            if (c == CA) {
                return -2;
            }
            echo_rx(c);
        }
        if (n == 0) {
            step_tick();
        }
    }
    return -4;
}

static int window_has(const char *win, int wlen, const char *token)
{
    size_t tlen = strlen(token);

    if ((size_t)wlen < tlen) {
        return 0;
    }
    return memcmp(win + wlen - (int)tlen, token, tlen) == 0;
}

static void window_push(char *win, int *wlen, int cap, uint8_t c)
{
    if (*wlen >= cap - 1) {
        memmove(win, win + 1, (size_t)(cap - 2));
        *wlen = cap - 2;
    }
    win[(*wlen)++] = (char)c;
    win[*wlen] = '\0';
}

/* YMODEM 等待态判定：loader 进入 SerialDownload 后持续发 'C' 等首包。
 * 若窗口尾部几乎全是 'C'（且没匹配到其它 token），视为已在 YMODEM。 */
static int window_ymodem_c(const char *win, int wlen)
{
    int i, n = 0, c = 0;
    int start = wlen - 16;

    if (start < 0) {
        start = 0;
    }
    for (i = start; i < wlen; i++) {
        n++;
        if (win[i] == (char)CRC_C) {
            c++;
        }
    }
    return n >= 8 && c >= (n * 3) / 4;
}

static enum phase sniff_phase(const xfer_ops_t *ops, void *ctx, int timeout_ms,
                              int *got_handshake_c)
{
    char win[384];
    int wlen = 0;
    int t0 = now_ms();
    int saw_xmodem = 0;

    *got_handshake_c = 0;
    memset(win, 0, sizeof(win));
    if (g_verbose) {
        fprintf(stderr, "--- board ---\n");
    }

    while (now_ms() - t0 < timeout_ms) {
        uint8_t c;
        int n = ops->read(ctx, &c, 1, 50);

        if (n < 0) {
            return PH_TIMEOUT;
        }
        if (n == 1) {
            echo_rx(c);
            fflush(stderr);
            window_push(win, &wlen, (int)sizeof(win), c);

            if (window_has(win, wlen, "@STM32:")) {
                echo_nl();
                return PH_LOADER_CLI;
            }
            if (window_has(win, wlen, "Press \"U\"") ||
                window_has(win, wlen, "to stay in bootloader") ||
                window_has(win, wlen, "stay in bootloader, enter CLI")) {
                echo_nl();
                return PH_LOADER_WAIT_U;
            }
            /* 真·空片恢复：boot 打印 "UART XMODEM"/"XMODEM-CRC" 横幅后才发裸 C。
             * 不靠 APP 日志里的 C 猜握手（CONNECT/CIPSTART 也会含 C）。 */
            if (window_has(win, wlen, "UART XMODEM") ||
                window_has(win, wlen, "XMODEM-CRC") ||
                window_has(win, wlen, "进入恢复模式")) {
                saw_xmodem = 1;
            }
            if (saw_xmodem && c == CRC_C) {
                *got_handshake_c = 1;
                echo_nl();
                return PH_BOOT_XMODEM;
            }
            /* 已有 loader 停在 YMODEM（复位线未接/上次遗留）：持续 C 流 */
            if (window_ymodem_c(win, wlen)) {
                *got_handshake_c = 1;
                echo_nl();
                return PH_LOADER_YMODEM;
            }
        }
        if (n == 0) {
            step_tick();   /* 步1: 等 boot/loader 横幅期间的进行中动画 */
        }
    }
    echo_nl();
    if (saw_xmodem) {
        return PH_BOOT_XMODEM;
    }
    return PH_TIMEOUT;
}

static int wait_token(const xfer_ops_t *ops, void *ctx, const char *token, int timeout_ms)
{
    char win[384];
    int wlen = 0;
    int t0 = now_ms();

    memset(win, 0, sizeof(win));
    if (g_verbose) {
        fprintf(stderr, "--- board ---\n");
    }
    while (now_ms() - t0 < timeout_ms) {
        uint8_t c;
        int n = ops->read(ctx, &c, 1, 50);

        if (n == 1) {
            echo_rx(c);
            fflush(stderr);
            window_push(win, &wlen, (int)sizeof(win), c);
            if (window_has(win, wlen, token)) {
                echo_nl();
                return 0;
            }
        }
        if (n == 0) {
            step_tick();
        }
    }
    echo_nl();
    return -1;
}

static int send_soh_packet(const xfer_ops_t *ops, void *ctx, uint8_t seq,
                           const uint8_t *payload, uint32_t plen, uint32_t size)
{
    uint8_t hdr[3];
    uint8_t crcbuf[2];
    uint16_t crc;
    uint8_t pad[PKT_1K];
    uint8_t *body;

    if (size != PKT_128 && size != PKT_1K) {
        return -1;
    }
    if (plen > size) {
        return -1;
    }
    hdr[0] = (size == PKT_1K) ? STX : SOH;
    hdr[1] = seq;
    hdr[2] = (uint8_t)(~seq);

    if (plen == size) {
        body = (uint8_t *)payload;
    } else {
        memset(pad, (plen == 0) ? 0x00 : 0x1A, size);
        if (payload && plen) {
            memcpy(pad, payload, plen);
        }
        body = pad;
    }
    crc = crc16_ccitt(body, size);
    crcbuf[0] = (uint8_t)(crc >> 8);
    crcbuf[1] = (uint8_t)(crc & 0xFF);

    if (ops->write(ctx, hdr, 3) != 3) {
        return -1;
    }
    if (ops->write(ctx, body, (int)size) != (int)size) {
        return -1;
    }
    if (ops->write(ctx, crcbuf, 2) != 2) {
        return -1;
    }
    return 0;
}

static int xmodem_send(const xfer_ops_t *ops, void *ctx, const uint8_t *data,
                       uint32_t len, int already_got_c)
{
    uint32_t off;
    uint8_t seq;
    int tries;
    uint32_t nblk;
    struct pstate ps;

    if (!already_got_c) {
        fprintf(stderr, "XMODEM: wait 'C' from boot...\n");
        if (wait_byte(ops, ctx, CRC_C, 15000) != 0) {
            anim_clear();   /* wait_byte 可能已画等待动画帧 */
            fprintf(stderr, "XMODEM: no 'C' handshake from boot\n");
            return -1;
        }
    }

    nblk = (len + PKT_128 - 1u) / PKT_128;
    anim_clear();   /* 等 'C' 期间可能画过动画帧, 先收尾再打全行 */
    fprintf(stderr, "XMODEM: send loader blob %u bytes (%u blocks)\n", len, nblk);

    pstate_init(&ps);
    seq = 1;
    off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        int ack;

        if (chunk > PKT_128) {
            chunk = PKT_128;
        }
        for (tries = 0; tries < 10; tries++) {
            if (send_soh_packet(ops, ctx, seq, data + off, chunk, PKT_128) != 0) {
                continue;
            }
            ack = wait_ack(ops, ctx, 5000);
            if (ack == 0) {
                break;
            }
        }
        if (tries >= 10) {
            pbar_clear();
            fprintf(stderr, "XMODEM: block %u ACK timeout at %u/%u\n", seq, off, len);
            pbar_unbind();
            return -1;
        }
        off += chunk;
        seq++;
        pbar_bind("XMODEM", (uint64_t)off, (uint64_t)len, &ps);
        progress_render2("XMODEM", (uint64_t)off, (uint64_t)len, &ps);
    }
    fprintf(stderr, "\n");
    pbar_line_done();
    pbar_unbind();

    for (tries = 0; tries < 10; tries++) {
        if (xfer_putc(ops, ctx, EOT) != 1) {
            continue;
        }
        if (wait_ack(ops, ctx, 5000) == 0) {
            fprintf(stderr, "XMODEM done\n");
            return 0;
        }
    }
    fprintf(stderr, "XMODEM: EOT ACK timeout\n");
    return -1;
}

/* 高速 YMODEM 握手(定义见下, 前置声明供 ymodem_send 使用) */
static int ymodem_handshake_c(const xfer_ops_t *ops, void *ctx);

static int ymodem_send(const xfer_ops_t *ops, void *ctx, const char *name,
                       const uint8_t *filebuf, long fsize, int got_ready)
{
    char hdr_payload[PKT_128];
    uint8_t seq;
    long off;
    int tries;
    struct pstate ps;

    if (!got_ready) {
        fprintf(stderr, "YMODEM: wait loader ready...\n");
        if (wait_token(ops, ctx, "Waiting for upg.bin", 15000) != 0) {
            fprintf(stderr, "YMODEM: no 'Waiting for upg.bin' from loader\n");
            return -1;
        }
        if (ymodem_handshake_c(ops, ctx) != 0) {
            return -1;
        }
    }

    memset(hdr_payload, 0, sizeof(hdr_payload));
    strncpy(hdr_payload, name, sizeof(hdr_payload) - 1);
    {
        size_t nlen = strlen(hdr_payload);
        snprintf(hdr_payload + nlen + 1, sizeof(hdr_payload) - nlen - 1, "%ld", fsize);
    }

    for (tries = 0; tries < 10; tries++) {
        if (send_soh_packet(ops, ctx, 0, (uint8_t *)hdr_payload, PKT_128, PKT_128) != 0) {
            continue;
        }
        if (wait_ack(ops, ctx, 10000) == 0) {
            break;
        }
    }
    if (tries >= 10) {
        fprintf(stderr, "YMODEM: filename packet ACK timeout\n");
        return -1;
    }
    (void)wait_byte(ops, ctx, CRC_C, 3000);

    pstate_init(&ps);
    seq = 1;
    off = 0;
    while (off < fsize) {
        uint32_t chunk = (uint32_t)(fsize - off);
        int ack;

        if (chunk > PKT_1K) {
            chunk = PKT_1K;
        }
        for (tries = 0; tries < 10; tries++) {
            uint32_t psz = (chunk > PKT_128) ? PKT_1K : PKT_128;
            if (send_soh_packet(ops, ctx, seq, filebuf + off, chunk, psz) != 0) {
                continue;
            }
            /* 首包会擦 APP 扇区，loader 段结束会写 SPI，ACK 可能很慢 */
            ack = wait_ack(ops, ctx, 60000);
            if (ack == 0) {
                break;
            }
        }
        if (tries >= 10) {
            pbar_clear();
            fprintf(stderr, "YMODEM: data packet %u ACK timeout at %ld/%ld\n",
                    seq, off, fsize);
            pbar_unbind();
            return -1;
        }
        off += chunk;
        seq++;
        pbar_bind("YMODEM", (uint64_t)off, (uint64_t)fsize, &ps);
        progress_render2("YMODEM", (uint64_t)off, (uint64_t)fsize, &ps);
    }
    fprintf(stderr, "\n");
    pbar_line_done();
    pbar_unbind();

    for (tries = 0; tries < 10; tries++) {
        if (xfer_putc(ops, ctx, EOT) != 1) {
            continue;
        }
        if (wait_ack(ops, ctx, 15000) == 0) {
            break;
        }
    }
    if (tries >= 10) {
        fprintf(stderr, "YMODEM: EOT ACK timeout\n");
        return -1;
    }
    (void)wait_byte(ops, ctx, CRC_C, 3000);

    memset(hdr_payload, 0, sizeof(hdr_payload));
    for (tries = 0; tries < 10; tries++) {
        if (send_soh_packet(ops, ctx, 0, (uint8_t *)hdr_payload, PKT_128, PKT_128) != 0) {
            continue;
        }
        if (wait_ack(ops, ctx, 15000) == 0) {
            break;
        }
    }
    if (tries >= 10) {
        fprintf(stderr, "YMODEM: empty filename ACK timeout\n");
        return -1;
    }

    fprintf(stderr, "YMODEM done (%ld bytes)\n", fsize);
    return 0;
}

/* ============ 高速 YMODEM 握手: 嗅探 loader "BAUD <N>" 横幅并同步切速 ============
 * loader(download.c SerialDownload)在 115200 打 "Waiting ..." 后打 "BAUD <N>"，
 * 延时后切到 N 并发 'C'。xfer 在此窗口解析 N: 本地切到 N 后等 'C'。
 * 旧 loader 无 BAUD 行 -> 维持当前速率等 'C', 行为与从前一致。 */

/* 读一小段窗口并搜索 "BAUD <digits>"。返回 >0 = 声明的波特率; 0 = 无(旧 loader) */
static int sniff_upg_baud(const xfer_ops_t *ops, void *ctx, int timeout_ms)
{
    char win[128];
    int wlen = 0;
    int t0 = now_ms();
    int r = 0;

    memset(win, 0, sizeof(win));
    while (now_ms() - t0 < timeout_ms) {
        uint8_t c;
        int n = ops->read(ctx, &c, 1, 50);

        if (n < 0) {
            break;
        }
        if (n == 1) {
            echo_rx(c);
            fflush(stderr);
            window_push(win, &wlen, (int)sizeof(win), c);
        }
    }
    {
        int i;
        for (i = 0; i + 5 <= wlen; i++) {
            if (memcmp(win + i, "BAUD ", 5) == 0) {
                int j = i + 5;
                int n = 0;
                int ok = 0;

                while (j < wlen && win[j] >= '0' && win[j] <= '9') {
                    if (n > 3000000) {
                        n = -1;   /* 防溢出 */
                        break;
                    }
                    n = n * 10 + (win[j] - '0');
                    ok = 1;
                    j++;
                }
                if (ok && n > 0) {
                    r = n;
                }
                break;
            }
        }
    }
    return r;
}

/* "Waiting for upg.bin" 横幅已见(115200): loader 打印 "BAUD <N>" 后会在 115200
 * 等一个确认字节(≤1s)。xfer 解析到 N 就回 BAUD_CONFIRM, 然后本地切速, 再等 'C';
 * loader 收到确认才切高速, 否则留在 115200 —— 因此 --no-fast / 强制速率
 * (不回确认) 对"新 loader"也能真正钉住 115200, 而不是单方面切速撞车。 */
static int ymodem_handshake_c(const xfer_ops_t *ops, void *ctx)
{
    int nb = 0;

    if (!g_no_fast) {
        nb = sniff_upg_baud(ops, ctx, 400);
    }
    if (nb > 0) {
        if (ops->set_baud == NULL) {
            fprintf(stderr, "transport has no set_baud, stay at %d\n",
                    XFER_DEFAULT_BAUD);
        } else {
            uint8_t okb = (uint8_t)BAUD_CONFIRM;

            /* 确认必须先于切速发: loader 此刻仍在 115200 听这个字节 */
            if (ops->write(ctx, &okb, 1) == 1) {
                fprintf(stderr, "fast-baud confirm -> loader switch to %d\n", nb);
            }
            if (ops->set_baud(ctx, nb) == 0) {
                fprintf(stderr, "switched to %d\n", nb);
            } else {
                fprintf(stderr, "fast baud %d unsupported, stay at %d\n",
                        nb, XFER_DEFAULT_BAUD);
            }
        }
    }
    if (wait_byte(ops, ctx, CRC_C, 5000) != 0) {
        fprintf(stderr, "no CRC 'C' after handshake\n");
        return -1;
    }
    return 0;
}

/* 等 loader 进入 YMODEM：以 SerialDownload 的 "Waiting for upg.bin" 横幅为准，
 * 再等真正的 CRC 'C'。不能只凭裸 'C' 判断——loader 进 CLI 时
 * "stay in bootloader, enter CLI..." 文案里就有大写 C，会把 YMODEM 握手带偏。 */
static int wait_ymodem_ready(const xfer_ops_t *ops, void *ctx, int timeout_ms)
{
    if (wait_token(ops, ctx, "Waiting for upg.bin", timeout_ms) != 0) {
        fprintf(stderr, "no 'Waiting for upg.bin' banner from loader\n");
        return -1;
    }
    return ymodem_handshake_c(ops, ctx);
}

/* 等 loader 出 YMODEM 横幅 或 进 CLI（@STM32:）。
 * 返回 0 = 已见横幅（进 YMODEM）；1 = 已见 CLI；-1 = 超时。 */
static int wait_banner_or_cli(const xfer_ops_t *ops, void *ctx, int timeout_ms)
{
    char win[384];
    int wlen = 0;
    int t0 = now_ms();

    memset(win, 0, sizeof(win));
    while (now_ms() - t0 < timeout_ms) {
        uint8_t c;
        int n = ops->read(ctx, &c, 1, 50);

        if (n < 0) {
            return -1;
        }
        if (n == 1) {
            echo_rx(c);
            fflush(stderr);
            window_push(win, &wlen, (int)sizeof(win), c);
            if (window_has(win, wlen, "Waiting for upg.bin")) {
                echo_nl();
                return 0;
            }
            if (window_has(win, wlen, "@STM32:")) {
                echo_nl();
                return 1;
            }
        }
        if (n == 0) {
            step_tick();
        }
    }
    echo_nl();
    return -1;
}

/* 逐字节慢速发送：旧 loader 的 UART_RecvByte 每 1ms 才轮询一次 RXNE，
 * 连续帧在 115200 下会 overrun 丢字节；逐字节+间隔能让每字节都等到。 */
static int send_paced(const xfer_ops_t *ops, void *ctx, const uint8_t *buf, int len, int gap_ms)
{
    int i;

    for (i = 0; i < len; i++) {
        if (ops->write(ctx, &buf[i], 1) != 1) {
            return -1;
        }
        if (gap_ms > 0 && i + 1 < len) {
            usleep((useconds_t)(gap_ms * 1000));
        }
    }
    return 0;
}

/* 统一进入 YMODEM 下载，兼容新旧 loader：
 *  新 loader：倒计时直接收 "upgrade" → SerialDownload 打印 "Waiting for upg.bin" + 'C'。
 *  若 "upgrade" 字节在 loader printf/轮询窗口里丢失，只会带起 CLI（@STM32:），走 CLI 兜底。
 * from_cli=1 表示已看到 @STM32:，跳过倒计时试探。
 * 返回 0 时 YMODEM 握手 'C' 已被消费，ymodem_send 传 got_ready=1。 */
static int enter_upgrade(const xfer_ops_t *ops, void *ctx, int from_cli)
{
    const char *cmd = "upgrade";
    uint8_t u = 'U';
    uint8_t cr = '\r';
    char full[16];
    int r;

    if (!from_cli) {
        /* 先等 150ms，让 loader 的倒计时 printf（约 3ms）走完、进到 1s 的收字节窗口，
         * 否则 "upgrade" 整帧撞进它的打印窗口会被 USART overrun 吞掉。 */
        fprintf(stderr, "countdown: settle, then send 'upgrade'\n");
        usleep(150000);
        if (send_paced(ops, ctx, (const uint8_t *)cmd, (int)strlen(cmd), 8) < 0) {
            return -1;
        }
        r = wait_banner_or_cli(ops, ctx, 6000);
        if (r == 0) {
            if (ymodem_handshake_c(ops, ctx) != 0) {
                return -1;
            }
            return 0;              /* 新 loader：YMODEM 'C' 已到手(可能已切高速) */
        }
        if (r < 0) {
            /* 完全没响应：字节多半丢了、loader 还在倒计时，补发 'U' 进 CLI */
            anim_clear();
            fprintf(stderr, "no banner/CLI, try 'U'\n");
            if (send_paced(ops, ctx, &u, 1, 0) < 0) {
                return -1;
            }
            usleep(150000);
            if (send_paced(ops, ctx, &u, 1, 0) < 0) {
                return -1;
            }
            if (wait_token(ops, ctx, "@STM32:", 6000) != 0) {
                return -1;
            }
        }
    }

    /* 已在 CLI（或刚进 CLI）：清残留再发 upgrade\r */
    anim_clear();
    fprintf(stderr, "CLI: flush + send 'upgrade\\r'\n");
    if (send_paced(ops, ctx, &cr, 1, 0) < 0) {
        return -1;
    }
    (void)wait_token(ops, ctx, "@STM32:", 4000);
    snprintf(full, sizeof(full), "upgrade\r");
    if (send_paced(ops, ctx, (const uint8_t *)full, (int)strlen(full), 8) < 0) {
        return -1;
    }
    if (wait_ymodem_ready(ops, ctx, 8000) != 0) {
        return -1;
    }
    return 0;
}

/* 一次完整尝试：复位 → 嗅探 → 进 YMODEM/CLI → 下载，失败返回 -1，由 main 重试。
 * 每次尝试会重置三步计时(复位识别 / 进升级模式 / YMODEM 传输),
 * 成功后由 steps_summary() 打印各步耗时与平均速率。 */
static int flash_attempt(const xfer_ops_t *ops, uart_ctx_t *uctx, const char *dev,
                         int do_reset, int send_cmd, int *got_c,
                         uint32_t magic, const uint8_t *loader_blob, uint32_t loader_len,
                         const char *name, const uint8_t *filebuf, long fsize)
{
    enum phase ph;
    int ready = 0;

    steps_reset();
    step_begin(0, "reset & detect", 1);
    ph = PH_TIMEOUT;
    if (do_reset) {
        /* 纯串口复位：DTR/RTS 控制线脉冲（USB-TTL 的 RTS/DTR 接板子 RST 时生效） */
        fprintf(stderr, "DTR/RTS reset pulse\n");
        xfer_uart_pulse_reset(uctx);
    }
    ph = sniff_phase(ops, uctx, do_reset ? 20000 : 8000, got_c);
    if (ph == PH_TIMEOUT && do_reset) {
        /* DTR/RTS 没接到复位线时，让用户手动按板子 RST，本工具继续等待一轮 */
        anim_clear();
        fprintf(stderr, "no boot/loader output after DTR/RTS reset.\n"
                        "If your USB-TTL DTR/RTS is not wired to the board RST,\n"
                        "press the board RESET button now — waiting 15s for boot banner...\n");
        ph = sniff_phase(ops, uctx, 15000, got_c);
    }

    anim_clear();
    if (ph == PH_BOOT_XMODEM) {
        if (!loader_blob) {
            fprintf(stderr, "boot is in XMODEM but file has no loader blob\n");
            return -1;
        }
        fprintf(stderr, "phase: boot XMODEM (no valid loader source)\n");
        step_end(0, 1);
        step_begin(1, "XMODEM bootstrap", 0);
        if (xmodem_send(ops, uctx, loader_blob, loader_len, *got_c) != 0) {
            step_end(1, 0);
            return -1;
        }
        step_end(1, 0);   /* "XMODEM done" 已由 xmodem_send 打印 */
        if (magic == LOADER_HDR_MAGIC) {
            steps_summary();   /* raw loader blob：XMODEM 已完成 */
            return 0;
        }
        /* 刚被 XMODEM 带起的 loader 在倒计时；进 upgrade */
        step_begin(1, "enter upgrade", 1);
        if (enter_upgrade(ops, uctx, 0) != 0) {
            step_end(1, 0);
            return -1;
        }
        step_end(1, 1);
        ready = 1;
    } else if (ph == PH_LOADER_WAIT_U) {
        fprintf(stderr, "phase: loader countdown\n");
        step_end(0, 1);
        step_begin(1, "enter upgrade", 1);
        if (enter_upgrade(ops, uctx, 0) != 0) {
            step_end(1, 0);
            return -1;
        }
        step_end(1, 1);
        ready = 1;
    } else if (ph == PH_LOADER_CLI) {
        fprintf(stderr, "phase: loader CLI\n");
        step_end(0, 1);
        if (send_cmd) {
            step_begin(1, "enter upgrade", 1);
            if (enter_upgrade(ops, uctx, 1) != 0) {
                step_end(1, 0);
                return -1;
            }
            step_end(1, 1);
            ready = 1;
        }
    } else if (ph == PH_LOADER_YMODEM) {
        fprintf(stderr, "phase: loader already in YMODEM (C stream)\n");
        step_end(0, 1);
        ready = 1;   /* 握手 'C' 已被 sniff 消费 */
    } else {
        fprintf(stderr, "no boot/loader handshake.\n"
                        "Board likely jumped to APP (WiFi/shell) or is not powered.\n"
                        "Close serialTerm if it holds %s.\n", dev);
        return -1;
    }

    if (send_cmd) {
        step_begin(2, "YMODEM transfer", 0);
        s_steps[2].bytes = (uint64_t)(fsize > 0 ? fsize : 0);
        if (ymodem_send(ops, uctx, name, filebuf, fsize, ready) != 0) {
            step_end(2, 0);
            return -1;
        }
        step_end(2, 0);   /* "YMODEM done" 已由 ymodem_send 打印 */
        steps_summary();
    }
    return 0;
}

/* ==================== 串口占用者发现 / 抢占 ==================== */

#define HOLDER_COMM_LEN  64
#define HOLDER_CMD_LEN   192

typedef struct {
    long  pid;
    uid_t uid;                       /* real uid: 决定能否 kill(跨用户要 sudo) */
    char  comm[HOLDER_COMM_LEN];     /* /proc/<pid>/comm: 进程名(内核截断 15 字符) */
    char  cmd[HOLDER_CMD_LEN];       /* /proc/<pid>/cmdline: 完整命令行 */
} holder_t;

/* 读 /proc/<pid>/comm; 进程已退出时留空串 */
static void holder_read_comm(long pid, char *out, size_t len)
{
    char path[64];
    FILE *f;

    out[0] = '\0';
    snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    if (fgets(out, (int)len, f) != NULL) {
        out[strcspn(out, "\n")] = '\0';
    } else {
        out[0] = '\0';
    }
    fclose(f);
}

/* 读 /proc/<pid>/cmdline, 把 NUL 分隔的 argv 拼成空格分隔的一行。
 * comm 只有 15 字节, 脚本常被截成 "python3"; 这里能看到真实脚本名和参数,
 * 用户才能确认"要杀的确实是我的串口工具"。 */
static void holder_read_cmdline(long pid, char *out, size_t len)
{
    char path[64];
    FILE *f;
    size_t n;
    size_t i;

    out[0] = '\0';
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    n = fread(out, 1, len - 1, f);
    fclose(f);
    out[n] = '\0';
    if (n == 0) {
        return;   /* 内核线程 / 僵尸: 无 cmdline */
    }
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)out[i];

        /* NUL 分隔的 argv → 空格; 其它控制字符(换行/制表等)也压成空格。
         * 否则 `python3 -c '<多行脚本>'` 这类 cmdline 会把提示撑成几十行。 */
        if (ch == '\0' || ch < 0x20 || ch == 0x7f) {
            out[i] = ' ';
        }
    }
    while (n > 1 && out[n - 1] == ' ') {
        out[--n] = '\0';
    }
}

/* 读 /proc/<pid>/status 的 Uid 行(real uid) */
static uid_t holder_read_uid(long pid)
{
    char path[64];
    char line[256];
    FILE *f;
    uid_t uid = (uid_t)-1;

    snprintf(path, sizeof(path), "/proc/%ld/status", pid);
    f = fopen(path, "r");
    if (f == NULL) {
        return uid;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "Uid:", 4) == 0) {
            unsigned long v = 0;
            if (sscanf(line + 4, "%lu", &v) == 1) {
                uid = (uid_t)v;
            }
            break;
        }
    }
    fclose(f);
    return uid;
}

/* 扫 /proc/<pid>/fd, 收集除自己以外所有持有 dev 的进程。返回个数(0=无)。
 *
 * 为什么需要: Linux tty 默认允许并发 open, 只有当占用方也用 flock/TIOCEXCL
 * 时 uart_open 才会因 EBUSY 失败。minicom/serialTerm(旧版)/cat > /dev/ttyUSB0
 * 这类裸 open 的程序不参与锁协议, xfer 打开不会报错, 但两边会抢字节导致烧录乱码。
 * 所以 open 成功后也要调一次, 把这类"无锁占用方"提示出来。 */
static int collect_holders(const char *dev, holder_t *out, int max)
{
    char real[PATH_MAX];
    char fddir[64];
    char link[PATH_MAX];
    char target[PATH_MAX];
    DIR *d;
    struct dirent *e;
    long self = (long)getpid();
    int n = 0;

    if (realpath(dev, real) == NULL) {
        return 0;   /* 设备不可达, open 阶段已报错 */
    }
    d = opendir("/proc");
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        long pid;
        DIR *dfd;
        struct dirent *fe;
        int hit = 0;

        if (e->d_name[0] < '0' || e->d_name[0] > '9') {
            continue;
        }
        pid = strtol(e->d_name, NULL, 10);
        if (pid <= 0 || pid == self) {
            continue;
        }
        snprintf(fddir, sizeof(fddir), "/proc/%ld/fd", pid);
        dfd = opendir(fddir);
        if (dfd == NULL) {
            continue;   /* 无权限或已退出 */
        }
        while ((fe = readdir(dfd)) != NULL) {
            ssize_t rn;
            char *tail;

            if (fe->d_name[0] == '.') {
                continue;
            }
            snprintf(link, sizeof(link), "%s/%s", fddir, fe->d_name);
            rn = readlink(link, target, sizeof(target) - 1);
            if (rn <= 0) {
                continue;
            }
            target[rn] = '\0';
            tail = strstr(target, " (deleted)");
            if (tail != NULL) {
                *tail = '\0';
            }
            if (strcmp(target, real) == 0) {
                hit = 1;
                break;
            }
        }
        closedir(dfd);
        if (!hit) {
            continue;
        }
        if (n >= max) {
            break;      /* 已够, 防止写越界 */
        }
        out[n].pid = pid;
        out[n].uid = holder_read_uid(pid);
        holder_read_comm(pid, out[n].comm, sizeof(out[n].comm));
        holder_read_cmdline(pid, out[n].cmd, sizeof(out[n].cmd));
        n++;
    }
    closedir(d);
    return n;
}

/* 无锁并发占用方(如未加锁的旧版 serialTerm)提示: 只警告, 不抢占。 */
static void warn_other_holders(const char *dev)
{
    holder_t h[XFER_PREEMPT_MAX_HOLDERS];
    int n;
    int i;

    n = collect_holders(dev, h, XFER_PREEMPT_MAX_HOLDERS);
    for (i = 0; i < n; i++) {
        fprintf(stderr,
                "WARN: pid %ld (%s) 也在使用 %s —— 若烧录异常, 请先关闭它再重试\n",
                h[i].pid, h[i].comm[0] != '\0' ? h[i].comm : "?", dev);
    }
}

/* 轮询尝试真正打开端口, 直到成功(=锁已到手)或超时。 */
static int wait_port_free(const xfer_ops_t *ops, void *ctx, const char *dev,
                          int baud, int timeout_ms)
{
    int deadline = now_ms() + timeout_ms;

    for (;;) {
        if (ops->open(ctx, dev, baud) == 0) {
            return 0;
        }
        if (now_ms() >= deadline) {
            return -1;
        }
        usleep(100 * 1000);
    }
}

/* 抢占被占用的串口: 列出占用者 → 征求同意 → SIGTERM/SIGKILL → 等释放 → 重开。
 *
 * 为什么要"杀"而不是"夺": Linux 没有任何接口能把别人手里的 tty fd 强行拿过来 ——
 * SIGSTOP 不关 fd(锁仍被持有), pidfd_getfd 需要 ptrace 且原进程会继续读同一 fd,
 * flock/TIOCEXCL 只在最后一个 fd 关闭时才释放。所以唯一可靠的办法是请占用方结束自己。
 *
 * 正因为它有破坏性, 默认必须交互确认; 只有显式 -y/--force 才静默执行,
 * 且非 tty(脚本/CI)一律拒绝 —— 否则会静默杀掉用户的数据记录进程。
 *
 * 返回 0 = 已成功打开(ctx 可用); -1 = 放弃(未做破坏, 或杀不掉)。 */
static int preempt_port(const xfer_ops_t *ops, void *ctx, const char *dev,
                        int baud, int force)
{
    holder_t h[XFER_PREEMPT_MAX_HOLDERS];
    int n;
    int i;
    int c;
    int ans;

    n = collect_holders(dev, h, XFER_PREEMPT_MAX_HOLDERS);
    if (n == 0) {
        fprintf(stderr,
                "port busy but no holding process found in /proc\n"
                "  (held by another user, or a stale lock?)\n"
                "  try: sudo fuser -v %s\n", dev);
        return -1;
    }

    fprintf(stderr, "\n%d process(es) hold %s:\n", n, dev);
    for (i = 0; i < n; i++) {
        fprintf(stderr, "  pid %-7ld uid %-6u %s\n",
                h[i].pid, (unsigned)h[i].uid,
                h[i].comm[0] != '\0' ? h[i].comm : "?");
        if (h[i].cmd[0] != '\0') {
            fprintf(stderr, "          %s\n", h[i].cmd);
        }
    }

    if (force) {
        fprintf(stderr, "\n--force: killing the above\n");
    } else if (!isatty(STDIN_FILENO)) {
        fprintf(stderr,
                "\nstdin is not a tty — refusing to kill without confirmation\n"
                "  re-run with -y/--force to preempt\n");
        return -1;
    } else {
        fprintf(stderr, "\nKill the above and take over %s? [y/N] ", dev);
        fflush(stderr);
        ans = getchar();
        while ((c = getchar()) != '\n' && c != EOF) {
            /* 吃掉本行剩余字符 */
        }
        if (ans != 'y' && ans != 'Y') {
            fprintf(stderr, "aborted — port left untouched\n");
            return -1;
        }
    }

    /* 第一轮: SIGTERM。给占用方收尾机会(比直接 KILL 干净), 然后等它关闭串口。 */
    for (i = 0; i < n; i++) {
        if (kill((pid_t)h[i].pid, SIGTERM) != 0 && errno == EPERM) {
            fprintf(stderr, "  pid %ld: permission denied — need sudo\n", h[i].pid);
        }
        /* ESRCH = 已自行退出, 视为成功, 无需处理 */
    }
    if (wait_port_free(ops, ctx, dev, baud, XFER_PREEMPT_TERM_WAIT_MS) == 0) {
        return 0;
    }

    /* 第二轮: 还在赖着 → SIGKILL(不可被忽略或捕获) */
    fprintf(stderr, "still busy after SIGTERM — escalating to SIGKILL\n");
    for (i = 0; i < n; i++) {
        if (kill((pid_t)h[i].pid, SIGKILL) != 0 && errno == EPERM) {
            fprintf(stderr, "  pid %ld: permission denied — need sudo\n", h[i].pid);
        }
    }
    if (wait_port_free(ops, ctx, dev, baud, XFER_PREEMPT_KILL_WAIT_MS) == 0) {
        return 0;
    }

    fprintf(stderr,
            "port still busy after SIGKILL — giving up\n"
            "  if nothing is listed above, the tty is still in exclusive mode\n"
            "  (driver not released); try unplug/replug the adapter, or:\n"
            "  sudo fuser -v %s\n", dev);
    return -1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [options] <serial> <upg.bin>\n"
            "\n"
            "Serial flasher for upg.bin / upg_web.bin (pure serial, no SWD/pyocd).\n"
            "DTR/RTS pulse resets the board, then 'upgrade' + YMODEM the package.\n"
            "Supports both new loader (countdown accepts 'upgrade' directly)\n"
            "and old loader (fallback to U + CLI). Automatically retries.\n"
            "\n"
            "Options:\n"
            "  -b, --baud N     serial baudrate (default %d); any explicit -b enters\n"
            "                   force mode: rate fixed, no fast-baud negotiation\n"
            "  --no-fast        disable fast-baud YMODEM negotiation (always baseline)\n"
            "  --no-reset       do not pulse DTR/RTS (board already in loader)\n"
            "  --no-cmd         skip 'upgrade' (loader already waiting 'C')\n"
            "  --retries N      auto retry attempts (default 3)\n"
            "  -y, --force      if the port is busy, kill the process(es) holding it\n"
            "                   without asking (default: list them, then ask y/N;\n"
            "                   non-tty stdin always refuses without this flag)\n"
            "  -v, --verbose    echo board serial output (default: progress only)\n"
            "  -h, --help\n"
            "\n"
            "Examples:\n"
            "  %s /dev/ttyUSB0 dist/upg.bin\n"
            "  %s --no-reset /dev/ttyUSB0 dist/upg_web.bin\n",
            argv0, XFER_DEFAULT_BAUD, argv0, argv0);
}

int main(int argc, char **argv)
{
    const char *dev = NULL;
    const char *file = NULL;
    int baud = XFER_DEFAULT_BAUD;
    int baud_explicit = 0;   /* 命令行显式给了 -b */
    int send_cmd = 1;
    int do_reset = 1;
    int retries = XFER_DEFAULT_RETRIES;
    int force = 0;           /* -y/--force: 端口忙时直接抢占, 不询问 */
    int attempt;
    int i;
    int got_c;
    uart_ctx_t uctx = { .fd = -1 };
    const xfer_ops_t *ops = &xfer_uart_ops;
    FILE *fp;
    uint8_t *filebuf = NULL;
    long fsize;
    uint32_t magic;
    uint32_t loader_len = 0;
    uint32_t loader_off = 0;
    const uint8_t *loader_blob = NULL;
    char name[64];
    char tmp[512];
    char *base;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
            continue;
        }
        if ((!strcmp(argv[i], "-b") || !strcmp(argv[i], "--baud")) && i + 1 < argc) {
            baud = atoi(argv[++i]);
            baud_explicit = 1;
            continue;
        }
        if (!strcmp(argv[i], "--no-cmd")) {
            send_cmd = 0;
            continue;
        }
        if (!strcmp(argv[i], "--no-fast")) {
            g_no_fast = 1;
            continue;
        }
        if (!strcmp(argv[i], "--no-reset")) {
            do_reset = 0;
            continue;
        }
        if (!strcmp(argv[i], "--retries") && i + 1 < argc) {
            retries = atoi(argv[++i]);
            if (retries < 0) {
                retries = 0;
            }
            continue;
        }
        if (!strcmp(argv[i], "-y") || !strcmp(argv[i], "--force")) {
            force = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        if (!dev) {
            dev = argv[i];
        } else if (!file) {
            file = argv[i];
        } else {
            fprintf(stderr, "extra arg %s\n", argv[i]);
            return 1;
        }
    }
    if (!dev || !file) {
        usage(argv[0]);
        return 1;
    }

    /* 显式 -b = 用户强制该速率: 关闭高速协商, 全程不变, 且不回确认字节。
     * 对"新 loader"(打印 BAUD 后等确认, 收不到就留 115200)这样能真正钉住;
     * 对"旧 loader"(无 BAUD 行)本就全程基线, 等价无害。
     * 注意: 强制非基线档(如 -b 460800)要求 loader 同样固定/已停在目标速率。 */
    if (baud_explicit) {
        g_no_fast = 1;
        fprintf(stderr, "-b %d explicit -> force mode (fast-baud negotiation off)\n", baud);
    }

    fp = fopen(file, "rb");
    if (!fp) {
        fprintf(stderr, "open %s: %s\n", file, strerror(errno));
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 1;
    }
    fsize = ftell(fp);
    rewind(fp);
    if (fsize < 16) {
        fclose(fp);
        fprintf(stderr, "file too small\n");
        return 1;
    }
    filebuf = (uint8_t *)malloc((size_t)fsize);
    if (!filebuf || fread(filebuf, 1, (size_t)fsize, fp) != (size_t)fsize) {
        fprintf(stderr, "read %s failed\n", file);
        free(filebuf);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    magic = rd_le32(filebuf);
    if (magic == UPG_HDR_MAGIC_LDR) {
        loader_len = rd_le32(filebuf + 4);
        loader_off = 24;
        if (loader_len == 0 || loader_off + loader_len > (uint32_t)fsize) {
            fprintf(stderr, "upg.bin loader_len invalid (%u)\n", loader_len);
            free(filebuf);
            return 1;
        }
        loader_blob = filebuf + loader_off;
        if (rd_le32(loader_blob) != LOADER_HDR_MAGIC) {
            fprintf(stderr, "loader blob magic 0x%08x != 2LDR\n", rd_le32(loader_blob));
            free(filebuf);
            return 1;
        }
        fprintf(stderr, "upg.bin: loader=%u app1=%u app2=%u web=%u total=%ld\n",
                loader_len, rd_le32(filebuf + 8), rd_le32(filebuf + 12),
                rd_le32(filebuf + 16), fsize);
    } else if (magic == LOADER_HDR_MAGIC) {
        loader_blob = filebuf;
        loader_len = (uint32_t)fsize;
        send_cmd = 0;
        fprintf(stderr, "raw loader blob %ld bytes (XMODEM only)\n", fsize);
    } else {
        fprintf(stderr, "note: not upg.bin (magic 0x%08x), YMODEM whole file\n", magic);
    }

    snprintf(tmp, sizeof(tmp), "%s", file);
    base = basename(tmp);
    snprintf(name, sizeof(name), "%s", base ? base : "upg.bin");
    name[sizeof(name) - 1] = '\0';

    if (ops->open(&uctx, dev, baud) != 0) {
        int oerr = errno;   /* fprintf/strerror 都可能改写 errno, 先存下来 */

        fprintf(stderr, "open %s: %s\n", dev, strerror(oerr));
        if (oerr != EBUSY) {
            free(filebuf);
            return 1;
        }
        fprintf(stderr, "port busy — close serialTerm.sh / other flash_upg first\n");
        /* 抢占: 列出占用者 → 征求同意(或 --force) → SIGTERM/SIGKILL → 重开 */
        if (preempt_port(ops, &uctx, dev, baud, force) != 0) {
            free(filebuf);
            return 1;
        }
    }
    fprintf(stderr, "opened %s @ %d (exclusive)\n", dev, baud);
    warn_other_holders(dev);   /* 无锁并发占用方(如 serialTerm)在此给出提示 */

    for (attempt = 0; attempt <= retries; attempt++) {
        if (attempt > 0) {
            fprintf(stderr, "\n== retry %d/%d ==\n", attempt, retries);
            usleep(500 * 1000);
        }
        /* 每次尝试都从用户初始波特率开始：上次失败可能已切到 loader 声明的高速档
         * (460800)，而 loader 复位后会回到基线 115200，必须同步回切再嗅探 */
        if (ops->set_baud && ops->set_baud(&uctx, baud) != 0) {
            fprintf(stderr, "set baud back to %d failed\n", baud);
            ops->close(&uctx);
            free(filebuf);
            return 1;
        }
        if (flash_attempt(ops, &uctx, dev, do_reset, send_cmd, &got_c,
                          magic, loader_blob, loader_len, name, filebuf, fsize) == 0) {
            break;
        }
        if (attempt == retries) {
            ops->close(&uctx);
            free(filebuf);
            fprintf(stderr, "flash failed after %d attempt(s)\n", retries + 1);
            return 1;
        }
    }

    ops->close(&uctx);
    free(filebuf);
    return 0;
}
