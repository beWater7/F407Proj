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

extern struct http_state* gs_hs[2];  //保存需要的http连接、web和固件升级请求
char *gs_byUserFile = NULL;
/* malloc 原地址；fw 路径会做指针偏移，释放必须用 base */
static char *gs_byUserFileBase = NULL;

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
	char byTmpBuff[16] = {0};

    CUSTOM_ASSERT(NULL == data, return RET_ERR);
    CUSTOM_ASSERT(0 == len, return RET_ERR);
    /* 计算web文件的校验和用于校验 */
    dwWebFileCheckSum = crc32_checksum(data, len);

    snprintf(byTmpBuff, sizeof(byTmpBuff), "%08x", dwWebFileCheckSum);

    //os_printf("[%s:%d]gs_byUserFile len:%d len2:%d\n",__FUNCTION__,__LINE__, len, gs_hs[0]->pkt_len);

    /* 使用 memmove 移动字符串, 将arr内容移动到arr+shift */
    memmove(data + CRCCHECKSUMLEN, data,  len); // +1 是为了移动结尾的 '\0'
    memset(data, 0, CRCCHECKSUMLEN);

    /* 32位数最大 0xFFFFFFFF, gs_byWebFile前8个字节存校验和 */
    memcpy(data, byTmpBuff, CRCCHECKSUMLEN-1);

    /* 使用'\n'作为分隔符，避免\0导致strlen无法获取正常长度 */
    data[CRCCHECKSUMLEN-1] = '\n'; 
    //debug_api("[%s:%d] gs_byUserFile:%s %d %d\n",__FUNCTION__,__LINE__, data, strlen(data), len);

    if(SPI_FLASH_WRITE(PART_WEB, 0, data, len + CRCCHECKSUMLEN))
    {
        os_debug("web write fail! len=%lu\r\n", (unsigned long)(len + CRCCHECKSUMLEN));
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
	uint32_t webWriteNum = 0;
    u32_t dwTotalLen = 0;
    char *content_type = NULL;
    char *tmp = NULL;
    struct http_state *hs = (struct http_state*)conn;
    char *post_pkt = NULL;
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
            gs_byUserFile = (char *)os_malloc(hs->pkt_len + CRCCHECKSUMLEN);
            if(NULL == gs_byUserFile)
            {
                os_debug("gs_byUserFile os_malloc failed (need %lu)!\r\n",
                         (unsigned long)(hs->pkt_len + CRCCHECKSUMLEN));
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
        //printf("2gs_byUserFile:%p\r\n   ", gs_byUserFile);
        //flash_data_print(gs_byUserFile, hs->pkt_len + CRCCHECKSUMLEN);
        /* buff may contain zero data */
        tmp = gs_byUserFile + (gs_hs[0]->pkt_len - 48);
        /* 清空接收到web文件结尾分割符 */
        tmp = strstr(tmp, "\r\n------");
        if(tmp)
        {
            #if WEB_UPGRADE_DEBUG
            os_printf("find \\r\\n------ !\r\n");
            #endif
            memset(tmp, 0, strlen(tmp));
        }

        /* delete http header data */
        if((tmp = strstr(gs_byUserFile, CRLFCRLF)))
        {
            tmp += strlen(CRLFCRLF);
            /* web数据长度 */
            dwTotalLen = gs_hs[0]->pkt_len - (tmp - gs_byUserFile);
            #if WEB_UPGRADE_DEBUG
            os_printf("web len: %u\r\n", dwTotalLen);
            #endif
        }
        else
        {
            tmp = gs_byUserFile;
            dwTotalLen = gs_hs[0]->pkt_len;
            os_printf(KERN_WARN"no CRLFCRLF, use full pkt_len=%lu\n",
                      (unsigned long)dwTotalLen);
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
        if(!byRet)
        {
            gs_hs[0]->pkt_len = 0;
            gs_hs[0]->pkt_recved_len = 0;
            /* 发送自定义数据 */
            // altcp_write(hs->pcb, custom_data, strlen(custom_data), TCP_WRITE_FLAG_COPY);
            // altcp_output(hs->pcb);
            code = SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON);
            sendCallback(hs, NULL, code);
            os_printf(KERN_REPORT"web upgrade success!\n");
            //sys_reboot_delay(2);
        }
        upgrade_buf_release();
        gs_hs[0] = NULL;
        return byRet;
    }

    return byRet;
}


/* web 升级程序 */
uint8_t fw_upgrade(void *conn, char *webFile, int len)
{
    u16_t wBoundaryHeaderLen = 0;
    uint32_t fwWriteNum = 0;
    uint32_t dwWebFileCheckSum = 0;
    u16_t wDataLen = 0;
    char *post_pkt = NULL;
    struct http_state *hs = (struct http_state*)conn;
    struct upg_header stOtaHeader = {0};
    uint32_t dwOffsest = 0;
    HTTP_CODE code = HTTP_OK;

    CUSTOM_ASSERT(NULL == hs, return ERR_ARG);
    CUSTOM_ASSERT(NULL == webFile, return ERR_ARG);

    /* 仅处理固件升级会话 */
    if (NULL == gs_hs[1] || gs_hs[1] != hs) {
        return ERR_ARG;
    }

    /* webFile */
    os_printf(KERN_INFO"Received pkt_len: %d, recved_len: %d, len: %d\n", gs_hs[1]->pkt_len, gs_hs[1]->pkt_recved_len, len);

    /* 当前接收到第一个报文, http头 */
    if(0 == gs_hs[1]->pkt_recved_len && NULL == gs_byUserFile)
    {
        os_printf(KERN_WARN"[%s:%d]fw upgrade start!\n",__FUNCTION__,__LINE__);
        char *content_type = strstr(webFile, CONTENT_TYPE);
        if(content_type)
        {
            content_type += CONTENT_TYPE_LEN;

            /* content-type:XX/XX  后有\r\n\r\n */
            post_pkt = strstr(content_type, CRLFCRLF);
            if(NULL == post_pkt)
            {
                os_debug("invalid packets!\n");
            }

            /* post_pkt 指向正文 */
            post_pkt += strlen(CRLFCRLF);

            /* payload中的分割符长度 */
            wBoundaryHeaderLen = post_pkt - webFile;
            //os_printf("boundary len:%d\n", wBoundaryHeaderLen);

            /* 减去boundary头之后获取升级包正文总长度 */
            gs_hs[1]->pkt_len -= wBoundaryHeaderLen;

            //os_printf(" gs_hs[1]->pkt_len :%d  hs->pkt_len:%d\n", gs_hs[1]->pkt_len,  hs->pkt_len);

            /* http的正文-payload: 分割符号 + 有效数据 */
            wDataLen = len - wBoundaryHeaderLen;
            //os_printf("webFile:%s post_pkt:%s \n", webFile, post_pkt - strlen(CRLFCRLF));

            os_printf("first fw pkt len:%d\n", wDataLen);

            /* 统计已经接收的正文数据长度 */
            gs_hs[1]->pkt_recved_len += wDataLen;

            os_printf(KERN_REPORT"fw pkt total len:%d\n", hs->pkt_len);

            if(gs_byUserFile)
            {
                os_printf(KERN_ERROR"gs_byUserFile already exist!\n");
                return ERR_ARG;
            }

            /* 为升级包申请空间 */
            gs_byUserFile = (char *)os_malloc(hs->pkt_len);
            if(NULL == gs_byUserFile)
            {
                os_debug("gs_byUserFile os_malloc failed !\r\n");
                return ERR_MEM;
            }
            gs_byUserFileBase = gs_byUserFile;
            memset(gs_byUserFile, 0, hs->pkt_len);

            /* 如果第一个包携带正文则将其拷贝到缓存区 */
            if(wDataLen)
                memcpy(gs_byUserFile, post_pkt, wDataLen);
        }
        else
        {
            os_debug("no content length!\n");
            return ERR_ARG;
        }
    }
    else if(gs_hs[1]->pkt_len != gs_hs[1]->pkt_recved_len)
    {
        memcpy(gs_byUserFile + gs_hs[1]->pkt_recved_len, webFile, len);
        gs_hs[1]->pkt_recved_len += len;
    }

    /* Content-Length = 开始分割符 + 正文内容 + 结束分隔符 */

    /* 数据都接收完毕（pkt_len 必须已设置且 >0，避免 0==0 误触发） */
    if(gs_hs[1]->pkt_len > 0 && gs_hs[1]->pkt_len == gs_hs[1]->pkt_recved_len)
    {
        //printf("[%s:%d]gs_byUserFile:%s\n", __FUNCTION__,__LINE__,gs_byUserFile);
        os_printf(KERN_WARN"[%s:%d]fw recved over len:%d\n",__FUNCTION__,__LINE__, gs_hs[1]->dwData_len);
        // /* 你的自定义数据 */
        // const char *custom_data = "HTTP/1.1 200 OK\r\n"
        //                           "Content-Type: text/plain\r\n"
        //                           "Content-Length: 28\r\n"
        //                           //"\r\n"
        //                           "fw file recved succ!";

        /* 清空接收到文件中分割符
         * \r\n
         * ------WebKitFormBoundaryzBRfaxs82R1PIPpN--
         */
//        char *tmp = memcmp(gs_byUserFile, "\r\n------", );
//        if(tmp)
//        {
//            printf("------------------------\n");
//            memset(tmp, 0, strlen(tmp));
//        }
//        printf("[%s:%d]gs_byUserFile len:%d\n",__FUNCTION__,__LINE__,strlen(gs_byUserFile));
        if(gs_hs[1]->pkt_recved_len > gs_hs[1]->dwData_len)
        {
            os_printf("[%s:%d] useless data len:%d\n",__FUNCTION__,__LINE__,gs_hs[1]->pkt_recved_len -  gs_hs[1]->dwData_len);
            memset(gs_byUserFile + gs_hs[1]->dwData_len, 0, gs_hs[1]->pkt_recved_len -  gs_hs[1]->dwData_len);
        }

        gs_hs[1]->pkt_len = 0;
        gs_hs[1]->pkt_recved_len = 0;
        /* 发送自定义数据 */
        // altcp_write(hs->pcb, custom_data, strlen(custom_data), TCP_WRITE_FLAG_COPY);
        // altcp_output(hs->pcb);
        code = SET_HTTP_FILE_TYPE(code, HTTP_FILE_TYPE_JSON);
        sendCallback(hs, NULL, code);
        os_printf(KERN_WARN"fw write over, wait to reboot!\n");

        for(int i = 0; i < 24; i++)
        {
             printf(" %02x", gs_byUserFile[i]);
        }
        printf("\r\n");
#if OTA_FW_WITH_WEB
        {
            char *payload = gs_byUserFileBase ? gs_byUserFileBase : gs_byUserFile;
            memcpy_s(&stOtaHeader, sizeof(stOtaHeader), payload, sizeof(stOtaHeader));
            if(UPG_HDR_MAGIC == stOtaHeader.magic)
            {
                payload += sizeof(stOtaHeader);
                os_printf(KERN_REPORT"fw len:%d  web len:%d \r\n", stOtaHeader.fw_len, stOtaHeader.web_len);
#if OTA_REGION_SPI_FLASH
                upgrade_write_fw_v2(payload, stOtaHeader.fw_len);
#else
                upgrade_write_fw(payload, stOtaHeader.fw_len);
#endif
                payload += stOtaHeader.fw_len;
                upgrade_web((uint8 *)payload, stOtaHeader.web_len);
                os_sleep_ms(200);
                writeLog("fw upgrade");
            }
            else
            {
                os_debug("err stOtaHeader.magic:%08x!\r\n", stOtaHeader.magic);
            }
        }
#else
#if OTA_REGION_SPI_FLASH
        upgrade_write_fw_v2(gs_byUserFile,  gs_hs[1]->dwData_len);
#else
        upgrade_write_fw(gs_byUserFile,  gs_hs[1]->dwData_len);
#endif
#endif
        upgrade_buf_release();
        gs_hs[1] = NULL;
        /* 三秒之后重启 */
        sys_reboot_delay(2);
    }
}




