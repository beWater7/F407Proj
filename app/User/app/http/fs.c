/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/apps/httpd_opts.h"
#include "lwip/def.h"
#include "lwip/apps/fs.h"
#include <string.h>

/* added by liuday */
#include <stdio.h>
#include "bsp_spi_flash.h"
#if defined(CONFIG_APP_FATFS)
#include "ff.h"
#endif
#include "malloc.h"
#include "os_debug.h"
#include "flash_manage.h"
#include "upgrade.h"
#include "os_task.h"

uint8_t byUseBinary;

#include HTTPD_FSDATA_FILE
#if LWIP_HTTPD_CUSTOM_FILES

/*-----------------------------------------------------------------------------------*/
/* web file seq must equal to web.bin */
static WebFileDesc gs_webFileDesc[] = {
  {"index.shtml",                      WEB_HTML },
  {"chart.js",                         WEB_JS,   WEB_FILE_GZIP},
  {"chartjs-adapter-moment.min.js",    WEB_JS,   WEB_FILE_GZIP},
  {"chartjs-plugin-datalabels.min.js", WEB_JS,   WEB_FILE_GZIP},
  {"chartjs-plugin-streaming.min.js",  WEB_JS,   WEB_FILE_GZIP},
  {"favicon.ico",                      WEB_ICO,  WEB_FILE_GZIP},
  {"main.js",                          WEB_JS,   WEB_FILE_GZIP},
  {"moment.min.js",                    WEB_JS,   WEB_FILE_GZIP},
  {"style.css",                        WEB_CSS,  WEB_FILE_GZIP},
  {"tailwindcss_3_4_17.js",            WEB_JS,   WEB_FILE_GZIP},
  {"esp8266.jpg",                      WEB_JPG },
};

#define WEB_FILE_DESC_SIZE (sizeof(gs_webFileDesc)/sizeof(gs_webFileDesc[0]))

static WebBinHeader gs_webBinHdr = {0};
static uint8_t s_spi_web_tried;
void parse_web_bin(uint8_t *data);

/* 开机或升级后从 SPI 解析 web 头；空分区（0xFF）不解析，回落 ROM */
static int spi_web_header_ready(void)
{
    char buf[CRCCHECKSUMLEN + WEB_BIN_HDR_SIZE];
    unsigned i;
    int n;

    if (gs_webBinHdr.file_count != 0) {
        return 1;
    }
    if (s_spi_web_tried) {
        return 0;
    }
    s_spi_web_tried = 1;
    memset(buf, 0, sizeof(buf));
    n = SPI_FLASH_READ(PART_WEB, 0, (uint8_t *)buf, sizeof(buf));
    if (n <= 0) {
        return 0;
    }
    if ((uint8_t)buf[0] == 0xFFu) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        char c = buf[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    parse_web_bin((uint8_t *)buf + CRCCHECKSUMLEN);
    return (gs_webBinHdr.file_count != 0) ? 1 : 0;
}


void parse_web_bin(uint8_t* data) 
{
    uint32_t i = 0;
    uint32_t* ptr32 = (uint32_t*)data;
    uint8_t* entry_ptr = NULL;
    uint32_t file_count = 0;

    CUSTOM_ASSERT(NULL == data, return);

    file_count = ptr32[0];
    /* 空 Flash(0xFF) / 未写入时会读出超大值；合法包文件数应很小 */
    if (file_count == 0 || file_count > MAX_FILES) {
        os_debug("parse_web_bin: invalid file_count=%lu (web empty or corrupt)\r\n",
                 (unsigned long)file_count);
        gs_webBinHdr.file_count = 0;
        return;
    }

    gs_webBinHdr.file_count = (uint16_t)file_count;

    // header 每条 12B
    entry_ptr = data + 4;
    for(i = 0; i < file_count; i++) 
    {
        gs_webBinHdr.files[i].type   = *((uint32_t*)(entry_ptr + i*12 + 0));
        gs_webBinHdr.files[i].size   = *((uint32_t*)(entry_ptr + i*12 + 4));
        gs_webBinHdr.files[i].offset = *((uint32_t*)(entry_ptr + i*12 + 8));
        /* 再拦一道明显垃圾，避免刷屏拖垮串口 */
        if (gs_webBinHdr.files[i].size > (8U * 1024U * 1024U) ||
            gs_webBinHdr.files[i].offset > (8U * 1024U * 1024U)) {
            os_debug("parse_web_bin: entry %lu garbage, abort\r\n", (unsigned long)i);
            gs_webBinHdr.file_count = 0;
            return;
        }
        os_printf(KERN_INFO"type:%u size:%u offset:%u\r\n", 
                  gs_webBinHdr.files[i].type, 
                  gs_webBinHdr.files[i].size,
                  gs_webBinHdr.files[i].offset);
    }
}


void generate_http_header(char *header_html, size_t header_size, const char *content_type, const char *content_encoding, int content_length) 
{
    // 构建HTTP头部
    snprintf(header_html, header_size,
             "HTTP/1.1 200 OK\r\n"
             "Server: lwIP/2.0.3d (http://savannah.nongnu.org/projects/lwip)\r\n"
             "Connection: keep-alive\r\n"
             "Content-Type: %s\r\n"
             "Content-Encoding: %s\r\n"
             "Content-Length: %d\r\n"
             "Cache-Control: max-age=86400, private\r\n"   // browser cache time 86400 ==> 1 day
             "\r\n",
             content_type, content_encoding, content_length);
}


/* added by ldy */
int fs_open_custom(struct fs_file *file, const char *name)
{
    static uint32 dwWebReadLen = 0;
    #if WEB_CACHE_ENABLED
    char *pTmp = NULL;
    char *pRespData = NULL;
    #endif
    unsigned int dwWebFileCheckSum = 0;
    (void)dwWebFileCheckSum;
    unsigned int dwWebFileCheckSum2 = 0;
    (void)dwWebFileCheckSum2;
    #if WEB_FILE_CRC_CHECK
    char byTmpBuff[CRCCHECKSUMLEN + WEB_BIN_HDR_SIZE] = {0};
    #endif
    uint8 i = 0;
    uint8 byIndex = 0;
    uint32 dwHttpHdrLen = 0;
    char *ReadBuffer = NULL;
    char header_html[256] = {0};

    //os_debug("name: %s\r\n", name);
    /* use array data */
    if(byUseBinary)
    {
        return 0;
    }

    /* find resource index */
    for(i = 0; i < WEB_FILE_DESC_SIZE; i++)
    {
        if(0 == strcmp(name+1, gs_webFileDesc[i].byName))
        {
            byIndex = i;
            break;
        }
    }
    /* not found */
    if(WEB_FILE_DESC_SIZE == i)
    {
        os_printf(KERN_INFO"%s not found!\r\n", name);
        return 0;
    }
    file->fileIndex = byIndex;

    /* 升级后强制重解析；开机也尝试加载 SPI，避免只剩 ROM 保底页、没有 Wi-Fi 入口 */
    if (getWebUpgrade()) {
        s_spi_web_tried = 0;
        gs_webBinHdr.file_count = 0;
        setWebUpgrade(0);
    }
    (void)spi_web_header_ready();

    if (0 == gs_webBinHdr.file_count || byIndex >= gs_webBinHdr.file_count) {
        /* SPI web 未就绪：fs_open() 回落到片内 fsdata ROM */
        return 0;
    }

    {
        uint32_t file_size = gs_webBinHdr.files[byIndex].size;
        uint32_t need;

        if (ota_heap_busy()) {
            os_debug("OTA busy: skip open %s (save heap)\n", name);
            return 0;
        }

        if (0 == file_size || file_size > HTTPD_WEB_FILE_LEN) {
            os_debug("bad web file size %lu for %s\n",
                     (unsigned long)file_size, name);
            return 0;
        }
        need = file_size + sizeof(header_html);
        ReadBuffer = os_malloc(need);
        if(NULL == ReadBuffer)
        {
            os_debug("malloc failed! need=%lu free=%u\n",
                     (unsigned long)need,
                     (unsigned)os_heap_free());
            return 0;
        }
        memset(ReadBuffer, 0, need);
    }

#if WEB_FILE_CRC_CHECK
    //printf("1: %x  %x", dwWebFileCheckSum, crc32_checksum(ReadBuffer+strlen(header_html)+CRCCHECKSUMLEN, dwWebReadLen-CRCCHECKSUMLEN));
    memset(byTmpBuff, 0, sizeof(byTmpBuff));
    SPI_FLASH_READ(PART_WEB, 0, byTmpBuff, CRCCHECKSUMLEN - 1);
    sscanf(byTmpBuff, "%x", &dwWebFileCheckSum);
    dwWebFileCheckSum2 = crc32_checksum(ReadBuffer+CRCCHECKSUMLEN, dwWebReadLen-CRCCHECKSUMLEN);
    (void)dwWebFileCheckSum2;
    /* web file crc check */
    if(!(dwWebFileCheckSum && dwWebFileCheckSum == dwWebFileCheckSum2))
    {
        os_debug("web file err cheksum:%x cal chksum:%x!\n", dwWebFileCheckSum, dwWebFileCheckSum2);
        os_free(ReadBuffer);
        return 0;
    }
#endif

    #if !(LWIP_HTTPD_DYNAMIC_HEADERS)
    switch (gs_webBinHdr.files[byIndex].type)
    {
        case WEB_HTML:
            generate_http_header(header_html, sizeof(header_html), "text/html", "identity", gs_webBinHdr.files[byIndex].size);
            break;
        case WEB_ICO:
            generate_http_header(header_html, sizeof(header_html), "image/x-icon", "gzip", gs_webBinHdr.files[byIndex].size);
            break;
        case WEB_CSS:
            generate_http_header(header_html, sizeof(header_html), "text/css", "gzip", gs_webBinHdr.files[byIndex].size);
            break;
        case WEB_JS:
            generate_http_header(header_html, sizeof(header_html), "application/javascript", "gzip", gs_webBinHdr.files[byIndex].size);
            break;
        case WEB_JPG:
            generate_http_header(header_html, sizeof(header_html), "image/jpeg", "identity", gs_webBinHdr.files[byIndex].size);
            break;
        case WEB_GZIP:
            generate_http_header(header_html, sizeof(header_html), "application/javascript", "gzip", gs_webBinHdr.files[byIndex].size);
            break;
        default:
            break;
    }
    dwHttpHdrLen = strlen(header_html);
    /* http hdr */
    memcpy(ReadBuffer, header_html, dwHttpHdrLen);
    #endif
    //os_debug("name: %s\r\n", name);

    /**                          0     1     2     3 
     * web falsh: [crc] [count][HDR1][HDR2][HDR3][HDR4]... [index.html][css][js][jpg] 
     * 
     */
    dwWebReadLen = SPI_FLASH_READ(PART_WEB, 
                    CRCCHECKSUMLEN + gs_webBinHdr.files[byIndex].offset, 
                    (uint8_t *)(ReadBuffer + dwHttpHdrLen), 
                    gs_webBinHdr.files[byIndex].size);

    //PartitionRead(uint8_t index, uint32_t offset, uint8_t* data, uint32_t len, uint8_t id)

    os_printf(KERN_TRACE"read web len:%d\r\n", dwWebReadLen);
    //os_debug("name: %s\r\n", name);

    /* resp data */
    file->data = (ReadBuffer);
    file->len  =  gs_webBinHdr.files[byIndex].size + dwHttpHdrLen;
    file->index = file->len;
    file->pextension = NULL;

    /* custom http hdr */
    #if !(LWIP_HTTPD_DYNAMIC_HEADERS)
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    #else
    // When using dynamic headers, set compression flag for gzipped files
    file->flags |= FS_FILE_FLAGS_HEADER_PERSISTENT;
    if(gs_webFileDesc[byIndex].byIsGz)
    {
        // For gzipped content, set the gzip flag
        file->flags |= FS_FILE_FLAGS_GZIP;
    }
    #endif
    return 1;
}
/* added by liudayi end */


void fs_close_custom(struct fs_file *file)
{
    /* The reason for using os_free instead of free is that 
     * using free caused some resources to become corrupted, 
     * possibly due to memory conflicts. Previously, 
     * the lwIP heap was allocated in PSRAM, and web resources were also using malloc. 
     * After a long time debugging, 
     * it was finally discovered that using FreeRTOS's malloc and free APIs could resolve the issue, 
     * as os_free and os_malloc are based on FreeRTOS's memory APIs. 
     * Currently, most of the PSRAM has been allocated to FreeRTOS's heap.
     * by author ldy 2025 12
     */
    if(file && file->data)
    {
        os_free((void *)file->data);
    }

    return;
}


#if LWIP_HTTPD_FS_ASYNC_READ
u8_t fs_canread_custom(struct fs_file *file);
u8_t fs_wait_read_custom(struct fs_file *file, fs_wait_cb callback_fn, void *callback_arg);
int fs_read_async_custom(struct fs_file *file, char *buffer, int count, fs_wait_cb callback_fn, void *callback_arg);
#else /* LWIP_HTTPD_FS_ASYNC_READ */
int fs_read_custom(struct fs_file *file, char *buffer, int count);
#endif /* LWIP_HTTPD_FS_ASYNC_READ */
#endif /* LWIP_HTTPD_CUSTOM_FILES */

/*-----------------------------------------------------------------------------------*/
err_t
fs_open(struct fs_file *file, const char *name)
{
  const struct fsdata_file *f;

  if ((file == NULL) || (name == NULL)) {
    return ERR_ARG;
  }

#if LWIP_HTTPD_CUSTOM_FILES
  if (fs_open_custom(file, name)) {
    file->is_custom_file = 1;
    return ERR_OK;
  }
  file->is_custom_file = 0;
#endif /* LWIP_HTTPD_CUSTOM_FILES */

  for (f = FS_ROOT; f != NULL; f = f->next) {
    if (!strcmp(name, (const char *)f->name)) {
      file->data = (const char *)f->data;
      file->len = f->len;
      file->index = f->len;
      file->pextension = NULL;
      file->flags = f->flags;
#if HTTPD_PRECALCULATED_CHECKSUM
      file->chksum_count = f->chksum_count;
      file->chksum = f->chksum;
#endif /* HTTPD_PRECALCULATED_CHECKSUM */
#if LWIP_HTTPD_FILE_STATE
      file->state = fs_state_init(file, name);
#endif /* #if LWIP_HTTPD_FILE_STATE */
      return ERR_OK;
    }
  }
  /* file not found */
  return ERR_VAL;
}

/*-----------------------------------------------------------------------------------*/
void
fs_close(struct fs_file *file)
{
#if LWIP_HTTPD_CUSTOM_FILES
  if (file->is_custom_file) {
    fs_close_custom(file);
  }
#endif /* LWIP_HTTPD_CUSTOM_FILES */
#if LWIP_HTTPD_FILE_STATE
  fs_state_free(file, file->state);
#endif /* #if LWIP_HTTPD_FILE_STATE */
  LWIP_UNUSED_ARG(file);
}
/*-----------------------------------------------------------------------------------*/
#if LWIP_HTTPD_DYNAMIC_FILE_READ
#if LWIP_HTTPD_FS_ASYNC_READ
int
fs_read_async(struct fs_file *file, char *buffer, int count, fs_wait_cb callback_fn, void *callback_arg)
#else /* LWIP_HTTPD_FS_ASYNC_READ */
int
fs_read(struct fs_file *file, char *buffer, int count)
#endif /* LWIP_HTTPD_FS_ASYNC_READ */
{
  int read;
  if (file->index == file->len) {
    return FS_READ_EOF;
  }
#if LWIP_HTTPD_FS_ASYNC_READ
  LWIP_UNUSED_ARG(callback_fn);
  LWIP_UNUSED_ARG(callback_arg);
#endif /* LWIP_HTTPD_FS_ASYNC_READ */
#if LWIP_HTTPD_CUSTOM_FILES
  if (file->is_custom_file) {
#if LWIP_HTTPD_FS_ASYNC_READ
    return fs_read_async_custom(file, buffer, count, callback_fn, callback_arg);
#else /* LWIP_HTTPD_FS_ASYNC_READ */
    return fs_read_custom(file, buffer, count);
#endif /* LWIP_HTTPD_FS_ASYNC_READ */
  }
#endif /* LWIP_HTTPD_CUSTOM_FILES */

  read = file->len - file->index;
  if (read > count) {
    read = count;
  }

  MEMCPY(buffer, (file->data + file->index), read);
  file->index += read;

  return (read);
}
#endif /* LWIP_HTTPD_DYNAMIC_FILE_READ */
/*-----------------------------------------------------------------------------------*/
#if LWIP_HTTPD_FS_ASYNC_READ
int
fs_is_file_ready(struct fs_file *file, fs_wait_cb callback_fn, void *callback_arg)
{
  if (file != NULL) {
#if LWIP_HTTPD_FS_ASYNC_READ
#if LWIP_HTTPD_CUSTOM_FILES
    if (!fs_canread_custom(file)) {
      if (fs_wait_read_custom(file, callback_fn, callback_arg)) {
        return 0;
      }
    }
#else /* LWIP_HTTPD_CUSTOM_FILES */
    LWIP_UNUSED_ARG(callback_fn);
    LWIP_UNUSED_ARG(callback_arg);
#endif /* LWIP_HTTPD_CUSTOM_FILES */
#endif /* LWIP_HTTPD_FS_ASYNC_READ */
  }
  return 1;
}
#endif /* LWIP_HTTPD_FS_ASYNC_READ */
/*-----------------------------------------------------------------------------------*/
int
fs_bytes_left(struct fs_file *file)
{
  return file->len - file->index;
}
