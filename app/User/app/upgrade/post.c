#include <string.h> /* memset */
#include <stdlib.h> /* atoi */
#include <stdio.h>
#include "typedef.h"
#include "httpd_structs.h"
#include "lwip/def.h"
#include "lwip/apps/fs.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "httpd.h"
#include "upgrade.h"
#include "safe_utils.h"
#include "os_debug.h"
#include "flash_manage.h"
#include "log.h"
#include "node_tree.h"
#include "FreeRTOS.h"
#include "task.h"
#include "crc.h"
#include "malloc.h"

extern struct http_state* gs_hs[2];  //保存需要的http连接、web和固件升级请求
char *gs_byUserFile = NULL;
/* malloc 原地址；fw 路径会做指针偏移，释放必须用 base */
static char *gs_byUserFileBase = NULL;

#define OTA_BG_TASK_MAX 8
static TaskHandle_t s_ota_bg_tasks[OTA_BG_TASK_MAX];
static uint8_t s_ota_bg_n;
static uint8_t s_ota_heap_busy;

void setWebUpgrade(uint8_t byStatus);

void upgrade_buf_release(void)
{
    if (gs_byUserFileBase) {
        os_free(gs_byUserFileBase);
    } else if (gs_byUserFile) {
        os_free(gs_byUserFile);
    }
    gs_byUserFileBase = NULL;
    gs_byUserFile = NULL;
}

void ota_register_background_task(void *task_handle)
{
    TaskHandle_t h = (TaskHandle_t)task_handle;
    if (h == NULL || s_ota_bg_n >= OTA_BG_TASK_MAX) {
        return;
    }
    s_ota_bg_tasks[s_ota_bg_n++] = h;
}

uint8_t ota_heap_busy(void)
{
    return s_ota_heap_busy;
}

/**
 * @brief OTA 收包前腾堆：释放旧缓冲、关掉其它 HTTP、挂起非网络后台任务
 * @note  固件 OTA 成功会 reboot；web 升级不重启，结束时必须 ota_finish_heap_prepare。
 */
void ota_prepare_heap(void *keep_http_state)
{
    uint32_t before;
    uint32_t after;
    uint8_t i;
    TaskHandle_t telnet;

    /* URI 解析与首包各会调一次；已腾过则跳过，避免日志像“升了两次” */
    if (s_ota_heap_busy) {
        return;
    }

    before = xPortGetFreeHeapSize();

    upgrade_buf_release();
    s_ota_heap_busy = 1;

    /* 关掉其它 GET/POST，释放 fs.c 里可能高达数百 KB 的 ReadBuffer */
    httpd_close_all_except(keep_http_state);

    for (i = 0; i < s_ota_bg_n; i++) {
        if (s_ota_bg_tasks[i] != NULL) {
            vTaskSuspend(s_ota_bg_tasks[i]);
        }
    }

    /* Telnet 会话任务按需创建，用名字兜底挂起 */
    telnet = xTaskGetHandle("TelnetSession");
    if (telnet != NULL) {
        vTaskSuspend(telnet);
    }

    after = xPortGetFreeHeapSize();
    os_printf(KERN_WARN"OTA prepare heap: free %lu -> %lu (+%ld)\r\n",
              (unsigned long)before,
              (unsigned long)after,
              (long)after - (long)before);
}

/** 结束腾堆：允许再开 web 文件，并恢复后台任务（可重复调用） */
void ota_finish_heap_prepare(void)
{
    uint8_t i;
    TaskHandle_t telnet;

    if (!s_ota_heap_busy) {
        return;
    }
    s_ota_heap_busy = 0;
    for (i = 0; i < s_ota_bg_n; i++) {
        if (s_ota_bg_tasks[i] != NULL) {
            vTaskResume(s_ota_bg_tasks[i]);
        }
    }
    telnet = xTaskGetHandle("TelnetSession");
    if (telnet != NULL) {
        vTaskResume(telnet);
    }
}

static void ota_abort_heap_prepare(void)
{
    ota_finish_heap_prepare();
}


/*****************************************************
 * @fn       upgrade_web
 * @brief    web写入spi flash 分区
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int upgrade_web(uint8 *data, uint32 len)
{
    unsigned int dwWebFileCheckSum = 0;
    uint8_t crc_prefix[CRCCHECKSUMLEN];
    char byTmpBuff[16] = {0};

    CUSTOM_ASSERT(NULL == data, return RET_ERR);
    CUSTOM_ASSERT(0 == len, return RET_ERR);
    /* 计算web文件的校验和用于校验 */
    dwWebFileCheckSum = crc32_checksum(data, len);

    snprintf(byTmpBuff, sizeof(byTmpBuff), "%08x", dwWebFileCheckSum);

    /*
     * 旧实现：memmove(data+9, data, len) 在缓冲区内腾 CRC 前缀。
     * fw_upgrade 分配长度恰好 = upg.bin，web 段贴在缓冲区末尾，
     * memmove 会越界 9 字节踩坏 FreeRTOS 堆 → 随后 free/malloc HardFault，
     * 表现为「web 升级后整机挂死」。
     * 改为：不改动调用方缓冲，CRC 与正文分两次写入。
     */
    memset(crc_prefix, 0, sizeof(crc_prefix));
    memcpy(crc_prefix, byTmpBuff, CRCCHECKSUMLEN - 1);
    /* 使用'\n'作为分隔符，避免\0导致strlen无法获取正常长度 */
    crc_prefix[CRCCHECKSUMLEN - 1] = '\n';

    if (SPI_FLASH_WRITE(PART_WEB, 0, crc_prefix, CRCCHECKSUMLEN)) {
        os_debug("web write fail (crc prefix)! len=%u\r\n",
                 (unsigned)CRCCHECKSUMLEN);
        return RET_ERR;
    }
    if (SPI_FLASH_WRITE(PART_WEB, CRCCHECKSUMLEN, data, len)) {
        os_debug("web write fail! len=%lu\r\n", (unsigned long)len);
        return RET_ERR;
    }
    /* 读回前 16 字节确认非 0xFF */
    {
        uint8_t chk[16];
        SPI_FLASH_READ(PART_WEB, 0, chk, sizeof(chk));
        if (chk[0] == 0xFF && chk[1] == 0xFF && chk[2] == 0xFF && chk[3] == 0xFF) {
            os_debug("web write verify: still 0xFF, SPI program failed\r\n");
            return RET_ERR;
        }
        os_printf(KERN_REPORT"web flash head: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  chk[0], chk[1], chk[2], chk[3], chk[4], chk[5], chk[6], chk[7]);
    }
    spi_flash_table[PART_WEB].size_used = len + CRCCHECKSUMLEN;
    os_printf(KERN_REPORT"web write ok, len=%lu size_used=%lu\n",
              (unsigned long)(len + CRCCHECKSUMLEN),
              (unsigned long)spi_flash_table[PART_WEB].size_used);
    writeLog("web upgrade");
    setWebUpgrade(1);
    return RET_OK;
}


/* web 升级程序 */
uint8_t web_upgrade(void *conn, char *webFile, int len)
{
    //extern FIL stWebFile;
    //FILINFO fno;
    //FRESULT res_WebFile;				  /* 文件操作结果 */
	u16_t wBoundaryHeaderLen = 0;
	(void)wBoundaryHeaderLen;
	uint32_t webWriteNum = 0;
	(void)webWriteNum;
    u32_t dwTotalLen = 0;
    char *content_type = NULL;
    char *tmp = NULL;
    struct http_state *hs = (struct http_state*)conn;
    char *post_pkt = NULL;
    (void)post_pkt;
    int8 byRet = RET_OK;
    HTTP_CODE code = HTTP_OK;

    CUSTOM_ASSERT(NULL == hs, return ERR_ARG);
    CUSTOM_ASSERT(NULL == webFile, return ERR_ARG);

    /* 仅处理当前 web 会话，避免与 fw_upgrade 串台 */
    if (gs_hs[0] != hs) {
        return ERR_ARG;
    }

    /* 当前接收到第一个报文, http头 */
    if(0 == gs_hs[0]->pkt_recved_len && NULL == gs_byUserFile)
    {
        content_type = strstr(webFile, CONTENT_TYPE);
        if(content_type)
        {
            /* 设备的全局区内存不够使用, 所以web数据拷贝一直失败，只能拷贝2k多一点
             * 故使用SRAM os_malloc 为web升级包申请空间 
             */
            os_printf(KERN_WARN"[%s:%d]web upgrade start!\n",__FUNCTION__,__LINE__);
            ota_prepare_heap(hs);
            gs_byUserFile = (char *)os_malloc(hs->pkt_len + CRCCHECKSUMLEN);
            if(NULL == gs_byUserFile)
            {
                os_debug("gs_byUserFile os_malloc failed (need %lu free=%u)!\r\n",
                         (unsigned long)(hs->pkt_len + CRCCHECKSUMLEN),
                         (unsigned)xPortGetFreeHeapSize());
                ota_abort_heap_prepare();
                return ERR_MEM;
            }
            gs_byUserFileBase = gs_byUserFile;
            memset(gs_byUserFile, 0, hs->pkt_len + CRCCHECKSUMLEN);

            /* copy webfile，webfile may contain http header， process then pkt received over */
            memcpy(gs_byUserFile, webFile, len);
            #if WEB_UPGRADE_DEBUG
            /* test */
            os_printf("web buff os_malloc success\r\n");
            #endif
        }
        else
        {
            os_debug("illegal web pkt!\r\n");
            return ERR_ARG;
        }
    }
    else if(gs_hs[0]->pkt_len != gs_hs[0]->pkt_recved_len)
    {
        memcpy(gs_byUserFile + gs_hs[0]->pkt_recved_len, webFile, len);
    }
#if WEB_UPGRADE_DEBUG
    os_printf("[%s:%d] pkt_len:%d, recved len:%d, post_content_len_left:%d, gs_byUserFile:%s\n", \
            __FUNCTION__,__LINE__, gs_hs[0]->pkt_len, gs_hs[0]->pkt_recved_len,               \
            gs_hs[0]->post_content_len_left, gs_byUserFile + gs_hs[0]->pkt_recved_len);
#endif
    gs_hs[0]->pkt_recved_len += len;

    /* Content-Length = 开始分割符 + 正文内容 + 结束分隔符 */
    /* 数据都接受完毕 */
    if(gs_hs[0]->pkt_len == gs_hs[0]->pkt_recved_len && gs_hs[0]->pkt_len > 0)
    {
        /* 你的自定义数据 */
        const char *custom_data = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: 28\r\n"
                                  //"\r\n"
                                  "web file recved succ!";
        (void)custom_data;
        //printf("2gs_byUserFile:%p\r\n   ", gs_byUserFile);
        //flash_data_print(gs_byUserFile, hs->pkt_len + CRCCHECKSUMLEN);
        /* multipart 尾：只在包尾附近找，避免误伤 web.bin 正文 */
        {
            u32_t scan_off = (gs_hs[0]->pkt_len > 64) ? (gs_hs[0]->pkt_len - 64) : 0;
            char *trail = strstr(gs_byUserFile + scan_off, "\r\n------");
            if (trail) {
#if WEB_UPGRADE_DEBUG
                os_printf("find \\r\\n------ !\r\n");
#endif
                *trail = '\0';
            }

            /* delete multipart header (boundary + Content-Disposition + blank line) */
            if ((tmp = strstr(gs_byUserFile, CRLFCRLF))) {
                tmp += strlen(CRLFCRLF);
                if (trail && trail > tmp) {
                    dwTotalLen = (u32_t)(trail - tmp);
                } else {
                    dwTotalLen = gs_hs[0]->pkt_len - (u32_t)(tmp - gs_byUserFile);
                }
#if WEB_UPGRADE_DEBUG
                os_printf("web len: %u\r\n", dwTotalLen);
#endif
            } else {
                tmp = gs_byUserFile;
                dwTotalLen = trail ? (u32_t)(trail - tmp) : gs_hs[0]->pkt_len;
                os_printf(KERN_WARN"no CRLFCRLF, use len=%lu\n",
                          (unsigned long)dwTotalLen);
            }
        }

        if (0 == dwTotalLen || NULL == tmp) {
            os_debug("web upgrade: empty payload\r\n");
            byRet = RET_ERR;
        } else {
            os_printf(KERN_WARN"upgrade_web begin len=%lu\n", (unsigned long)dwTotalLen);
            byRet = upgrade_web((uint8 *)tmp, dwTotalLen);
        }
#if 0
        /* 创建web文件, 保存至flash中，FA_CREATE_ALWAYS会创建新文件，并清除旧内容 */
        res_WebFile = f_open(&stWebFile, "1:web.bin",FA_CREATE_ALWAYS | FA_WRITE);
        if (FR_OK == res_WebFile)
        {
            printf(" create web.bin succ, start writting\r\n");
            /* 将指定存储区内容写入到文件内 */
            res_WebFile=f_write(&stWebFile, gs_byUserFile, dwTotalLen + CRCCHECKSUMLEN, &webWriteNum);
            if(FR_OK == res_WebFile)
            {
                printf("wirte len: %d\n",webWriteNum);
                //printf("wirte data:\r\n%s\r\n",gs_byUserFile);
            }
            else
            {
                printf("write failed!!: (%d)\n",res_WebFile);
            }

            // 3) 强制刷新缓存到 Flash
            res_WebFile = f_sync(&stWebFile);
            if (res_WebFile != FR_OK) {
                printf("sync failed\n");
                f_close(&stWebFile);
                return -1;
            }

            res_WebFile = f_stat("1:web.bin", &fno);
            printf("web.bin size: %lu\n", fno.fsize);

            /* 不再读写，关闭文件 */
            f_close(&stWebFile);
        }
        else
        {
            printf("create web.bin failed!!\r\n");
        }

        // 写完后保险起见：重新挂载文件系统
        f_mount(NULL, "1:", 0);  // 卸载
        f_mount(&fs, "1:", 1);   // 重挂载
        Delay(500);             // 等底层刷完
        /* TEST */
#endif
        /* 先释放 ~300KB 缓冲并清 busy，再回 200：否则浏览器马上 GET /index.shtml 会被拒 */
        upgrade_buf_release();
        gs_hs[0] = NULL;
        ota_finish_heap_prepare();
        if(!byRet)
        {
            code = SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON);
            sendCallback(hs, NULL, code);
            os_printf(KERN_REPORT"web upgrade success!\n");
        } else {
            code = SET_HTTP_FILE_TYPE(HTTP_BAD_REQUEST, HTTP_FILE_TYPE_JSON);
            sendCallback(hs, NULL, code);
            os_printf(KERN_ERROR"web upgrade failed\n");
        }
        return byRet;
    }

    return byRet;
}


/* web 升级程序 */
static int fw_find_multipart_payload(char *buf, int len, char **out_payload, u16_t *out_hdr_len)
{
    char *disp;
    char *blank;

    if (out_payload) {
        *out_payload = NULL;
    }
    if (out_hdr_len) {
        *out_hdr_len = 0;
    }
    if (!buf || len <= 0) {
        return -1;
    }

    /* 原始 upg.bin / app 包：开头就是 magic */
    if (len >= (int)sizeof(uint32_t)) {
        uint32_t magic = 0;
        memcpy(&magic, buf, sizeof(magic));
        if (magic == UPG_HDR_MAGIC || magic == UPG_HDR_MAGIC_DUAL) {
            if (out_payload) {
                *out_payload = buf;
            }
            if (out_hdr_len) {
                *out_hdr_len = 0;
            }
            return 0;
        }
    }

    /* multipart：找 Content-Disposition 后的空行；无 Content-Type 的浏览器也能过 */
    disp = strstr(buf, "Content-Disposition");
    if (!disp) {
        disp = strstr(buf, "content-disposition");
    }
    blank = strstr(disp ? disp : buf, CRLFCRLF);
    if (!blank) {
        return -1;
    }
    blank += strlen(CRLFCRLF);
    if (blank > buf + len) {
        return -1;
    }
    if (out_payload) {
        *out_payload = blank;
    }
    if (out_hdr_len) {
        *out_hdr_len = (u16_t)(blank - buf);
    }
    return 0;
}

static uint32_t fw_trim_multipart_trailer(char *buf, uint32_t recved, uint32_t expect_file_len)
{
    char *trail;
    uint32_t useful = recved;

    if (!buf || recved == 0) {
        return 0;
    }

    /* 优先用前端传来的文件长度 */
    if (expect_file_len > 0 && expect_file_len <= recved) {
        useful = expect_file_len;
    } else {
        /* 去掉结尾 \r\n------xxxx-- */
        trail = NULL;
        if (recved > 8) {
            for (uint32_t i = recved; i > 8; i--) {
                if (buf[i - 8] == '\r' && buf[i - 7] == '\n' &&
                    buf[i - 6] == '-' && buf[i - 5] == '-') {
                    trail = &buf[i - 8];
                    break;
                }
            }
        }
        if (trail && trail > buf) {
            useful = (uint32_t)(trail - buf);
        }
    }

    if (useful < recved) {
        memset(buf + useful, 0, recved - useful);
    }
    return useful;
}

#if OTA_STREAM_FALLBACK

typedef enum {
    OTA_ST_HDR = 0,
    OTA_ST_SKIP,
    OTA_ST_FW,
    OTA_ST_WEB,
    OTA_ST_DRAIN,
    OTA_ST_DONE,
    OTA_ST_ERR
} ota_stream_phase_t;

typedef struct {
    uint8_t active;
    ota_stream_phase_t phase;
    uint8_t hdr_buf[sizeof(struct upg_header_dual)];
    uint8_t hdr_got;
    uint8_t hdr_need;
    uint32_t skip_left;
    uint32_t skip_after_fw; /* dual: 写完空闲槽后再跳过另一份 */
    uint32_t fw_left;
    uint32_t web_left;
    uint32_t fw_written;
    uint32_t web_written;
    uint32_t fw_crc_run;
    uint32_t web_crc_run;
    uint32_t fw_len;
    uint32_t web_len;
    uint32_t payload_need; /* hdr + all sections */
    uint32_t payload_got;
    uint8_t web_started;
} ota_stream_ctx_t;

static ota_stream_ctx_t s_ota_stream;
/* web CRC 回写时 RMW 首扇区，放 PSRAM 避免占内部 SRAM */
static __EXRAM uint8_t s_ota_web_sector[SECTOR_SIZE];

static void ota_stream_reset(void)
{
    memset(&s_ota_stream, 0, sizeof(s_ota_stream));
}

static int ota_stream_parse_header(void)
{
    ota_stream_ctx_t *st = &s_ota_stream;
    uint32_t magic = 0;
    uint8_t cur;

    memcpy(&magic, st->hdr_buf, sizeof(magic));

#if (OTA_MODE == DUAL_APP)
    if (magic == UPG_HDR_MAGIC_DUAL) {
        struct upg_header_dual dual;
        memcpy(&dual, st->hdr_buf, sizeof(dual));
        if (dual.fw1_len == 0 || dual.fw2_len == 0 ||
            dual.fw1_len > APP_FLASH_SIZE || dual.fw2_len > APP_FLASH_SIZE) {
            os_debug("stream: bad dual fw lens\r\n");
            return -1;
        }
        cur = ota_get_active_slot();
        st->payload_need = sizeof(dual) + dual.fw1_len + dual.fw2_len + dual.web_len;
        st->web_len = dual.web_len;
        st->web_left = dual.web_len;
        if (cur == 1U) {
            /* 跑 APP2 → 刷 APP1；先写 fw1，再跳过 fw2 */
            st->skip_left = 0;
            st->skip_after_fw = dual.fw2_len;
            st->fw_len = dual.fw1_len;
            st->fw_left = dual.fw1_len;
            os_printf(KERN_WARN"stream dual: active=APP2 stage=APP1 fw=%lu web=%lu\r\n",
                      (unsigned long)st->fw_len, (unsigned long)st->web_len);
        } else {
            /* 跑 APP1 → 刷 APP2；先跳过 fw1，再写 fw2 */
            st->skip_left = dual.fw1_len;
            st->skip_after_fw = 0;
            st->fw_len = dual.fw2_len;
            st->fw_left = dual.fw2_len;
            os_printf(KERN_WARN"stream dual: active=APP1 stage=APP2 fw=%lu web=%lu\r\n",
                      (unsigned long)st->fw_len, (unsigned long)st->web_len);
        }
        st->fw_crc_run = crc32_begin();
        st->web_crc_run = crc32_begin();
        st->phase = (st->skip_left > 0) ? OTA_ST_SKIP : OTA_ST_FW;
        return 0;
    }
    os_debug("stream: need dual upg (0x55475022), magic=0x%08lx\r\n",
             (unsigned long)magic);
    return -1;
#else
    if (magic == UPG_HDR_MAGIC) {
        struct upg_header single;
        memcpy(&single, st->hdr_buf, sizeof(single));
        if (single.fw_len == 0 || single.fw_len > APP_FLASH_SIZE) {
            return -1;
        }
        st->payload_need = sizeof(single) + single.fw_len + single.web_len;
        st->skip_left = 0;
        st->skip_after_fw = 0;
        st->fw_len = single.fw_len;
        st->fw_left = single.fw_len;
        st->web_len = single.web_len;
        st->web_left = single.web_len;
        st->fw_crc_run = crc32_begin();
        st->web_crc_run = crc32_begin();
        st->phase = OTA_ST_FW;
        os_printf(KERN_WARN"stream single: fw=%lu web=%lu\r\n",
                  (unsigned long)st->fw_len, (unsigned long)st->web_len);
        return 0;
    }
    return -1;
#endif
}

static int ota_stream_start_web(void)
{
    /* 先写占位 CRC，擦掉首扇区，再流式写 body（与 upgrade_web 顺序一致） */
    uint8_t ph[CRCCHECKSUMLEN];
    memset(ph, '0', CRCCHECKSUMLEN - 1);
    ph[CRCCHECKSUMLEN - 1] = '\n';
    if (SPI_FLASH_WRITE(PART_WEB, 0, ph, CRCCHECKSUMLEN)) {
        os_debug("stream: web placeholder write fail\r\n");
        return -1;
    }
    s_ota_stream.web_started = 1;
    return 0;
}

static int ota_stream_finalize_web(void)
{
    ota_stream_ctx_t *st = &s_ota_stream;
    uint32_t web_crc;
    char byTmpBuff[16];
    uint8_t crc_prefix[CRCCHECKSUMLEN];

    if (st->web_len == 0) {
        return 0;
    }
    if (!st->web_started || st->web_written != st->web_len) {
        os_debug("stream: web incomplete %lu/%lu\r\n",
                 (unsigned long)st->web_written, (unsigned long)st->web_len);
        return -1;
    }

    web_crc = crc32_finish(st->web_crc_run);
    snprintf(byTmpBuff, sizeof(byTmpBuff), "%08lx", (unsigned long)web_crc);
    memset(crc_prefix, 0, sizeof(crc_prefix));
    memcpy(crc_prefix, byTmpBuff, CRCCHECKSUMLEN - 1);
    crc_prefix[CRCCHECKSUMLEN - 1] = '\n';

    /* RMW 首扇区写入真实 CRC，避免 offset=0 小写擦掉 body */
    if (SPI_FLASH_READ(PART_WEB, 0, s_ota_web_sector, SECTOR_SIZE) < 0) {
        os_debug("stream: web sector read fail\r\n");
        return -1;
    }
    memcpy(s_ota_web_sector, crc_prefix, CRCCHECKSUMLEN);
    if (SPI_FLASH_WRITE(PART_WEB, 0, s_ota_web_sector, SECTOR_SIZE)) {
        os_debug("stream: web CRC RMW fail\r\n");
        return -1;
    }
    spi_flash_table[PART_WEB].size_used = st->web_len + CRCCHECKSUMLEN;
    setWebUpgrade(1);
    os_printf(KERN_REPORT"stream web ok len=%lu crc=0x%08lx\r\n",
              (unsigned long)st->web_len, (unsigned long)web_crc);
    return 0;
}

/**
 * @return 0 继续；1 负载段已收完；-1 失败
 */
static int ota_stream_feed(const uint8_t *data, uint32_t len)
{
    ota_stream_ctx_t *st = &s_ota_stream;

    if (!st->active || st->phase == OTA_ST_ERR) {
        return -1;
    }
    if (st->phase == OTA_ST_DONE || st->phase == OTA_ST_DRAIN) {
        st->payload_got += len;
        return (st->phase == OTA_ST_DONE) ? 1 : 0;
    }

    while (len > 0) {
        uint32_t n;

        if (st->phase == OTA_ST_HDR) {
            n = st->hdr_need - st->hdr_got;
            if (n > len) {
                n = len;
            }
            memcpy(st->hdr_buf + st->hdr_got, data, n);
            st->hdr_got = (uint8_t)(st->hdr_got + n);
            data += n;
            len -= n;
            st->payload_got += n;
            if (st->hdr_got >= st->hdr_need) {
                if (ota_stream_parse_header() != 0) {
                    st->phase = OTA_ST_ERR;
                    return -1;
                }
            }
            continue;
        }

        if (st->phase == OTA_ST_SKIP) {
            n = st->skip_left;
            if (n > len) {
                n = len;
            }
            data += n;
            len -= n;
            st->skip_left -= n;
            st->payload_got += n;
            if (st->skip_left == 0) {
                if (st->fw_len > 0 && st->fw_written == st->fw_len) {
                    /* 写完 FW 后的 skip（另一槽镜像）结束 → web */
                    if (st->web_left > 0) {
                        if (ota_stream_start_web() != 0) {
                            st->phase = OTA_ST_ERR;
                            return -1;
                        }
                        st->phase = OTA_ST_WEB;
                    } else {
                        st->phase = OTA_ST_DRAIN;
                    }
                } else {
                    st->phase = OTA_ST_FW;
                }
            }
            continue;
        }

        if (st->phase == OTA_ST_FW) {
            n = st->fw_left;
            if (n > len) {
                n = len;
            }
            if (n > 0) {
                if (SPI_FLASH_WRITE(PART_APP1, st->fw_written, (uint8_t *)data, n)) {
                    os_debug("stream: FW write fail @%lu\r\n",
                             (unsigned long)st->fw_written);
                    st->phase = OTA_ST_ERR;
                    return -1;
                }
                st->fw_crc_run = crc32_update(st->fw_crc_run, data, n);
                st->fw_written += n;
                st->fw_left -= n;
                data += n;
                len -= n;
                st->payload_got += n;
            }
            if (st->fw_left == 0) {
                if (st->skip_after_fw > 0) {
                    st->skip_left = st->skip_after_fw;
                    st->skip_after_fw = 0;
                    st->phase = OTA_ST_SKIP;
                } else if (st->web_left > 0) {
                    if (ota_stream_start_web() != 0) {
                        st->phase = OTA_ST_ERR;
                        return -1;
                    }
                    st->phase = OTA_ST_WEB;
                } else {
                    st->phase = OTA_ST_DRAIN;
                }
                os_printf(KERN_INFO"stream: FW done %lu bytes\r\n",
                          (unsigned long)st->fw_written);
            }
            continue;
        }

        if (st->phase == OTA_ST_WEB) {
            n = st->web_left;
            if (n > len) {
                n = len;
            }
            if (n > 0) {
                if (SPI_FLASH_WRITE(PART_WEB, CRCCHECKSUMLEN + st->web_written,
                                    (uint8_t *)data, n)) {
                    os_debug("stream: web write fail @%lu\r\n",
                             (unsigned long)st->web_written);
                    st->phase = OTA_ST_ERR;
                    return -1;
                }
                st->web_crc_run = crc32_update(st->web_crc_run, data, n);
                st->web_written += n;
                st->web_left -= n;
                data += n;
                len -= n;
                st->payload_got += n;
            }
            if (st->web_left == 0) {
                st->phase = OTA_ST_DRAIN;
                os_printf(KERN_INFO"stream: web body done %lu\r\n",
                          (unsigned long)st->web_written);
            }
            continue;
        }

        /* DRAIN / DONE */
        st->payload_got += len;
        len = 0;
    }

    if (st->phase == OTA_ST_DRAIN && st->payload_need > 0 &&
        st->payload_got >= st->payload_need) {
        st->phase = OTA_ST_DONE;
        return 1;
    }
    return 0;
}

static int ota_stream_finish(void)
{
    ota_stream_ctx_t *st = &s_ota_stream;
    uint32_t fw_crc;

    if (st->phase == OTA_ST_ERR) {
        return -1;
    }
    if (st->fw_written != st->fw_len || st->fw_left != 0) {
        os_debug("stream: FW incomplete %lu/%lu\r\n",
                 (unsigned long)st->fw_written, (unsigned long)st->fw_len);
        return -1;
    }
    if (ota_stream_finalize_web() != 0) {
        return -1;
    }
    fw_crc = crc32_finish(st->fw_crc_run);
    if (upgrade_commit_ota_flag(st->fw_len, fw_crc) != 0) {
        return -1;
    }
    writeLog("fw upgrade stream");
    st->phase = OTA_ST_DONE;
    return 0;
}

static int ota_stream_begin(uint32_t magic_probe)
{
    ota_stream_reset();
    s_ota_stream.active = 1;
    s_ota_stream.phase = OTA_ST_HDR;
    if (magic_probe == UPG_HDR_MAGIC_DUAL) {
        s_ota_stream.hdr_need = (uint8_t)sizeof(struct upg_header_dual);
    } else if (magic_probe == UPG_HDR_MAGIC) {
        s_ota_stream.hdr_need = (uint8_t)sizeof(struct upg_header);
    } else {
        /* 默认按当前模式期望长度；首 4 字节到齐后再校正 */
#if (OTA_MODE == DUAL_APP)
        s_ota_stream.hdr_need = (uint8_t)sizeof(struct upg_header_dual);
#else
        s_ota_stream.hdr_need = (uint8_t)sizeof(struct upg_header);
#endif
    }
    os_printf(KERN_WARN"OTA stream fallback ON (need=%u)\r\n",
              (unsigned)s_ota_stream.hdr_need);
    return 0;
}

#endif /* OTA_STREAM_FALLBACK */

uint8_t fw_upgrade(void *conn, char *webFile, int len)
{
    u16_t wBoundaryHeaderLen = 0;
    (void)wBoundaryHeaderLen;
    u16_t wDataLen = 0;
    char *post_pkt = NULL;
    (void)post_pkt;
    struct http_state *hs = (struct http_state*)conn;
    struct upg_header stOtaHeader = {0};
    HTTP_CODE code = HTTP_OK;
    int byOk = 0;
#if OTA_STREAM_FALLBACK
    int stream_mode = 0;
#endif

    CUSTOM_ASSERT(NULL == hs, return ERR_ARG);
    CUSTOM_ASSERT(NULL == webFile, return ERR_ARG);

    if (NULL == gs_hs[1] || gs_hs[1] != hs) {
        return ERR_ARG;
    }

#if OTA_STREAM_FALLBACK
    stream_mode = s_ota_stream.active;
#endif

    os_printf(KERN_INFO"Received pkt_len: %d, recved_len: %d, len: %d\n",
              gs_hs[1]->pkt_len, gs_hs[1]->pkt_recved_len, len);

    /* 首包：剥 multipart 头（或识别裸 upg.bin）并分配缓冲区 */
    if (0 == gs_hs[1]->pkt_recved_len && NULL == gs_byUserFile
#if OTA_STREAM_FALLBACK
        && !s_ota_stream.active
#endif
        )
    {
        os_printf(KERN_WARN"[%s:%d]fw upgrade start!\n",__FUNCTION__,__LINE__);

        if (0 == gs_hs[1]->pkt_len) {
            os_debug("fw upgrade: pkt_len=0 (Content-Length missing?)\r\n");
            code = HTTP_BAD_REQUEST;
            sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
            gs_hs[1] = NULL;
            return ERR_ARG;
        }

        if (0 != fw_find_multipart_payload(webFile, len, &post_pkt, &wBoundaryHeaderLen)) {
            os_debug("fw upgrade: cannot find payload start (need upg.bin or multipart file)\r\n");
            code = HTTP_BAD_REQUEST;
            sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
            gs_hs[1] = NULL;
            return ERR_ARG;
        }

        if (wBoundaryHeaderLen > 0) {
            if (gs_hs[1]->pkt_len > wBoundaryHeaderLen) {
                gs_hs[1]->pkt_len -= wBoundaryHeaderLen;
            }
        }

        wDataLen = (u16_t)(len - (int)wBoundaryHeaderLen);
        gs_hs[1]->pkt_recved_len += wDataLen;

        os_printf(KERN_REPORT"fw pkt total len:%d first:%d hdr_skip:%d\n",
                  hs->pkt_len, wDataLen, wBoundaryHeaderLen);

        ota_prepare_heap(hs);
        gs_byUserFile = (char *)os_malloc(hs->pkt_len ? hs->pkt_len : 1);
        if (NULL == gs_byUserFile) {
            os_debug("gs_byUserFile os_malloc failed! need=%lu free=%u\r\n",
                     (unsigned long)(hs->pkt_len ? hs->pkt_len : 1),
                     (unsigned)xPortGetFreeHeapSize());
#if OTA_STREAM_FALLBACK
            {
                uint32_t magic = 0;
                if (wDataLen >= 4 && post_pkt) {
                    memcpy(&magic, post_pkt, 4);
                }
                ota_stream_begin(magic);
                stream_mode = 1;
                if (wDataLen && post_pkt) {
                    if (ota_stream_feed((uint8_t *)post_pkt, wDataLen) < 0) {
                        ota_stream_reset();
                        ota_abort_heap_prepare();
                        code = HTTP_BAD_REQUEST;
                        sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
                        gs_hs[1] = NULL;
                        return ERR_ARG;
                    }
                }
            }
#else
            ota_abort_heap_prepare();
            code = HTTP_BAD_REQUEST;
            sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
            gs_hs[1] = NULL;
            return ERR_MEM;
#endif
        } else {
            gs_byUserFileBase = gs_byUserFile;
            memset(gs_byUserFile, 0, hs->pkt_len ? hs->pkt_len : 1);

            if (wDataLen && post_pkt) {
                memcpy(gs_byUserFile, post_pkt, wDataLen);
            }
        }
    }
#if OTA_STREAM_FALLBACK
    else if (s_ota_stream.active)
    {
        if (gs_hs[1]->pkt_recved_len + (uint32_t)len > gs_hs[1]->pkt_len) {
            os_debug("fw upgrade stream: overflow recv\r\n");
            ota_stream_reset();
            ota_abort_heap_prepare();
            code = HTTP_BAD_REQUEST;
            sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
            gs_hs[1] = NULL;
            return ERR_ARG;
        }
        if (ota_stream_feed((uint8_t *)webFile, (uint32_t)len) < 0) {
            ota_stream_reset();
            ota_abort_heap_prepare();
            code = HTTP_BAD_REQUEST;
            sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
            gs_hs[1] = NULL;
            return ERR_ARG;
        }
        gs_hs[1]->pkt_recved_len += len;
        stream_mode = 1;
    }
#endif
    else if (gs_byUserFile && gs_hs[1]->pkt_len != gs_hs[1]->pkt_recved_len)
    {
        if (gs_hs[1]->pkt_recved_len + (uint32_t)len > gs_hs[1]->pkt_len) {
            os_debug("fw upgrade: overflow recv\r\n");
            upgrade_buf_release();
            code = HTTP_BAD_REQUEST;
            sendCallback(hs, NULL, SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON));
            gs_hs[1] = NULL;
            return ERR_ARG;
        }
        memcpy(gs_byUserFile + gs_hs[1]->pkt_recved_len, webFile, len);
        gs_hs[1]->pkt_recved_len += len;
    }

    if (gs_hs[1]->pkt_len > 0 && gs_hs[1]->pkt_len == gs_hs[1]->pkt_recved_len)
    {
#if OTA_STREAM_FALLBACK
        if (stream_mode || s_ota_stream.active) {
            os_printf(KERN_WARN"[%s:%d]fw stream recved over\n",
                      __FUNCTION__, __LINE__);
            gs_hs[1]->pkt_len = 0;
            gs_hs[1]->pkt_recved_len = 0;
            if (0 == ota_stream_finish()) {
                byOk = 1;
            } else {
                os_debug("ota_stream_finish failed\r\n");
            }
            ota_stream_reset();

            if (byOk) {
                code = SET_HTTP_FILE_TYPE(HTTP_OK, HTTP_FILE_TYPE_JSON);
                sendCallback(hs, NULL, code);
                os_printf(KERN_WARN"fw stream write over, wait to reboot!\n");
                gs_hs[1] = NULL;
                sys_reboot_delay(2);
            } else {
                code = SET_HTTP_FILE_TYPE(HTTP_BAD_REQUEST, HTTP_FILE_TYPE_JSON);
                sendCallback(hs, NULL, code);
                gs_hs[1] = NULL;
                ota_abort_heap_prepare();
                os_printf(KERN_ERROR"fw stream upgrade aborted\n");
            }
            return ERR_OK;
        }
#endif
        {
        uint32_t useful_len;

        os_printf(KERN_WARN"[%s:%d]fw recved over len:%d\n",
                  __FUNCTION__, __LINE__, gs_hs[1]->dwData_len);

        useful_len = fw_trim_multipart_trailer(gs_byUserFile,
                                              gs_hs[1]->pkt_recved_len,
                                              gs_hs[1]->dwData_len);

        gs_hs[1]->pkt_len = 0;
        gs_hs[1]->pkt_recved_len = 0;

        {
            char *payload = gs_byUserFileBase ? gs_byUserFileBase : gs_byUserFile;
            if (useful_len < sizeof(stOtaHeader)) {
                os_debug("fw upgrade: payload too short (%lu)\r\n",
                         (unsigned long)useful_len);
            } else {
                uint32_t magic = 0;
                memcpy(&magic, payload, sizeof(magic));

#if OTA_FW_WITH_WEB
#if (OTA_MODE == DUAL_APP)
                if (UPG_HDR_MAGIC_DUAL == magic) {
                    struct upg_header_dual dual = {0};
                    uint8_t cur;
                    uint8_t *fw_sel;
                    uint32_t fw_len;
                    uint32_t need;

                    if (useful_len < sizeof(dual)) {
                        os_debug("fw upgrade: dual header truncated\r\n");
                    } else {
                        memcpy_s(&dual, sizeof(dual), payload, sizeof(dual));
                        os_printf(KERN_REPORT"upg dual magic:%08x fw1:%lu fw2:%lu web:%lu\r\n",
                                  dual.magic,
                                  (unsigned long)dual.fw1_len,
                                  (unsigned long)dual.fw2_len,
                                  (unsigned long)dual.web_len);
                        need = sizeof(dual) + dual.fw1_len + dual.fw2_len + dual.web_len;
                        if (need > useful_len) {
                            os_debug("fw upgrade: dual sizes exceed payload (%lu > %lu)\r\n",
                                     (unsigned long)need, (unsigned long)useful_len);
                        } else {
                            payload += sizeof(dual);
                            cur = ota_get_active_slot();
                            /* 当前 APP1 → 写 APP2 镜像；当前 APP2 → 写 APP1 镜像 */
                            if (cur == 1U) {
                                fw_sel = (uint8_t *)payload;
                                fw_len = dual.fw1_len;
                            } else {
                                fw_sel = (uint8_t *)(payload + dual.fw1_len);
                                fw_len = dual.fw2_len;
                            }
                            os_printf(KERN_REPORT"dual OTA: active=APP%u stage=APP%u len=%lu\r\n",
                                      (unsigned)(cur + 1U),
                                      (unsigned)((cur == 1U) ? 1U : 2U),
                                      (unsigned long)fw_len);
#if OTA_REGION_SPI_FLASH
                            if (0 != upgrade_write_fw_v2(fw_sel, fw_len)) {
                                os_debug("upgrade_write_fw_v2 failed\r\n");
                            } else {
                                if (0 != upgrade_web((uint8 *)(payload + dual.fw1_len + dual.fw2_len),
                                                     dual.web_len)) {
                                    os_debug("upgrade_web failed (fw meta already written)\r\n");
                                }
                                writeLog("fw upgrade dual");
                                byOk = 1;
                            }
#else
                            if (0 != upgrade_write_fw(fw_sel, fw_len)) {
                                os_debug("upgrade_write_fw failed\r\n");
                            } else {
                                if (0 != upgrade_web((uint8 *)(payload + dual.fw1_len + dual.fw2_len),
                                                     dual.web_len)) {
                                    os_debug("upgrade_web failed\r\n");
                                }
                                writeLog("fw upgrade dual");
                                byOk = 1;
                            }
#endif
                        }
                    }
                } else if (UPG_HDR_MAGIC == magic) {
                    os_debug("err: need dual upg.bin (magic 0x55475022); rebuild APP\r\n");
                } else {
                    os_debug("err stOtaHeader.magic:%08x\r\n", magic);
                }
#else /* MAIN_APP single-slot */
                memcpy_s(&stOtaHeader, sizeof(stOtaHeader), payload, sizeof(stOtaHeader));
                os_printf(KERN_REPORT"upg magic:%08x fw:%lu web:%lu\r\n",
                          stOtaHeader.magic,
                          (unsigned long)stOtaHeader.fw_len,
                          (unsigned long)stOtaHeader.web_len);

                if (UPG_HDR_MAGIC == stOtaHeader.magic) {
                    uint32_t need = sizeof(stOtaHeader) + stOtaHeader.fw_len + stOtaHeader.web_len;
                    if (need > useful_len) {
                        os_debug("fw upgrade: header sizes exceed payload (%lu > %lu)\r\n",
                                 (unsigned long)need, (unsigned long)useful_len);
                    } else {
                        payload += sizeof(stOtaHeader);
#if OTA_REGION_SPI_FLASH
                        if (0 != upgrade_write_fw_v2((uint8 *)payload, stOtaHeader.fw_len)) {
                            os_debug("upgrade_write_fw_v2 failed\r\n");
                        } else {
                            payload += stOtaHeader.fw_len;
                            if (0 != upgrade_web((uint8 *)payload, stOtaHeader.web_len)) {
                                /* FW 元数据已落盘，仍重启让 boot 刷 APP；web 可下次单独补 */
                                os_debug("upgrade_web failed (fw meta already written)\r\n");
                            }
                            writeLog("fw upgrade");
                            byOk = 1;
                        }
#else
                        if (0 != upgrade_write_fw((uint8 *)payload, stOtaHeader.fw_len)) {
                            os_debug("upgrade_write_fw failed\r\n");
                        } else {
                            payload += stOtaHeader.fw_len;
                            if (0 != upgrade_web((uint8 *)payload, stOtaHeader.web_len)) {
                                os_debug("upgrade_web failed\r\n");
                            }
                            writeLog("fw upgrade");
                            byOk = 1;
                        }
#endif
                    }
                } else {
                    os_debug("err stOtaHeader.magic:%08x (upload upg.bin from genUpgBin.py)\r\n",
                             stOtaHeader.magic);
                }
#endif /* OTA_MODE */
#else
#if OTA_REGION_SPI_FLASH
                upgrade_write_fw_v2((uint8 *)payload, useful_len);
#else
                upgrade_write_fw((uint8 *)payload, useful_len);
#endif
                byOk = 1;
#endif
            }
        }

        if (byOk) {
            code = SET_HTTP_FILE_TYPE(HTTP_OK, HTTP_FILE_TYPE_JSON);
            sendCallback(hs, NULL, code);
            os_printf(KERN_WARN"fw write over, wait to reboot!\n");
            upgrade_buf_release();
            gs_hs[1] = NULL;
            sys_reboot_delay(2);
        } else {
            code = SET_HTTP_FILE_TYPE(HTTP_BAD_REQUEST, HTTP_FILE_TYPE_JSON);
            sendCallback(hs, NULL, code);
            upgrade_buf_release();
            gs_hs[1] = NULL;
            ota_abort_heap_prepare();
            os_printf(KERN_ERROR"fw upgrade aborted (no reboot)\n");
        }
        } /* non-stream complete */
    }

    return ERR_OK;
}