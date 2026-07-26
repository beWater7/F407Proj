#include <stdio.h>
#include <string.h>
#include "node_tree.h"
#include "cJSON.h"
#include "safe_utils.h"
#include "lwip/def.h"
#include "lwip/apps/fs.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "httpd_structs.h"
#include "httpd.h"
#include "os_mutex.h"
#include "flash_manage.h"
#include "node_tree.h"
#include "log.h"
#include "bsp_esp8266.h"
#include "bsp_esp8266_test.h"
#include "log.h"

void format_uptime(uint64_t ms, char *buf, size_t buf_size);
u8_t http_send(struct altcp_pcb *pcb, struct http_state *hs);
err_t http_init_file(struct http_state *hs, struct fs_file *file, int is_09, const char *uri,
               u8_t tag_check, char *params);
uint8_t getCpuUsage(void);
uint8_t getMemUsage(void);

static node_t systems[];
static node_t log[];
static node_t logConfig[];
static node_t wifi[];

void sendCallback(void *conn, char *resp, HTTP_CODE code)
{
    struct http_state *hs = (struct http_state *)conn;
    cJSON *root = NULL;
    char *reply = NULL;
    char *body = resp;
    char *body_alloc = NULL;
    uint32 dwLen = 0;
    uint16 wHttpStatus = 0;
    uint16 wType = 0;
    const char *hdr;
    const char *ctype;

    CUSTOM_ASSERT(NULL == hs || NULL == hs->pcb, return);

    wHttpStatus = (uint16)GET_HTTP_STATUS(code);
    wType = (uint16)GET_HTTP_FILE_TYPE(code);
    if (0 == wHttpStatus) {
        wHttpStatus = HTTP_OK;
    }
    if (0 == wType) {
        wType = HTTP_FILE_TYPE_JSON;
    }

    if (NULL == body) {
        body_alloc = (char *)os_malloc(160);
        if (NULL == body_alloc) {
            return;
        }
        memset_s(body_alloc, 160, 0, 160);
        root = cJSON_CreateObject();
        if (NULL == root) {
            os_free(body_alloc);
            return;
        }
        cJSON_AddNumberToObject(root, "status", wHttpStatus);
        cJSON_AddStringToObject(root, "code", "0x00000000");
        cJSON_AddStringToObject(root, "errorMsg", (HTTP_OK == wHttpStatus) ? "ok" : "fail");
        cJSON_PrintPreallocated(root, body_alloc, 160, 0);
        cJSON_Delete(root);
        root = NULL;
        body = body_alloc;
    }

    hdr = http_header_strings[wHttpStatus];
    ctype = http_content_strings[wType];
    if (NULL == hdr) {
        hdr = "HTTP/1.1 200 OK\r\n";
    }
    if (NULL == ctype) {
        ctype = HTTP_HDR_JSON;
    }

    dwLen = 192U + (uint32)strlen(body);
    reply = (char *)os_malloc(dwLen);
    if (NULL == reply) {
        if (body_alloc) {
            os_free(body_alloc);
        }
        return;
    }
    memset_s(reply, dwLen, 0, dwLen);

    snprintf(reply, dwLen,
             "%s"
             "Connection: close\r\n"
             "%s"
             "Content-Length: %d\r\n"
             "\r\n"
             "%s",
             hdr, ctype, (int)strlen(body), body);

    altcp_write(hs->pcb, reply, (u16_t)strlen(reply), TCP_WRITE_FLAG_COPY);
    altcp_output(hs->pcb);

    os_free(reply);
    if (body_alloc) {
        os_free(body_alloc);
    }
}


int systemConfig(void *conn, void *args)
{
    printf("[%s:%d]\n",__FUNCTION__,__LINE__);
    return HTTP_OK;
}

int systemInfo(void *conn, void *args)
{
    char *resp  = NULL;
    HTTP_CODE dwHttpCode = HTTP_OK;
    cJSON *root = NULL;
    cJSON *data = NULL;
    char time_str[64] = {0};
    uint64_t ms_data = 0;
    struct http_state *hs = NULL;
    //char *json_str = NULL;
    int8_t byCpuUsage = 0;
    int8_t byMemUsage = 0;

    hs = (struct http_state *)conn;
    CUSTOM_ASSERT(NULL == hs, return HTTP_BAD_REQUEST);

    resp = (char *)os_malloc(PROTOCOL_SEND_BUF_SIZE);
    CUSTOM_ASSERT(!resp, return HTTP_BAD_REQUEST);
    memset_s(resp, PROTOCOL_SEND_BUF_SIZE, 0, PROTOCOL_SEND_BUF_SIZE);

    // json_str = (char *)os_malloc(PROTOCOL_SEND_BUF_SIZE);
    // CUSTOM_ASSERT(!json_str, return RET_ERR);
    // memset_s(json_str, PROTOCOL_SEND_BUF_SIZE, 0, PROTOCOL_SEND_BUF_SIZE);

    ms_data = sys_jiffies();  // sys uptime
    format_uptime(ms_data, time_str, sizeof(time_str));
    /* general reply */
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "status", HTTP_OK);
    cJSON_AddStringToObject(root, "code", "0x00000000");
    cJSON_AddStringToObject(root, "errorMsg", "ok");

    /* protocol payload */
    data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "uptime", time_str);
    byCpuUsage = getCpuUsage();
    byCpuUsage = (byCpuUsage > 100) ? 100:byCpuUsage;
    cJSON_AddNumberToObject(data, "cpu_usage", byCpuUsage);

    byMemUsage = getMemUsage();
    byMemUsage = (byMemUsage > 100) ? 100:byMemUsage;
    cJSON_AddNumberToObject(data, "mem_usage", byMemUsage);

    cJSON_AddNumberToObject(data, "temperature", getTemperature());

    memset(time_str, 0, sizeof(time_str));
    getWeather(time_str, sizeof(time_str));
    cJSON_AddStringToObject(data, "weather", time_str);

    memset(time_str, 0, sizeof(time_str));
    print_timestamp(time_str);
    cJSON_AddStringToObject(data, "systime", time_str);

    cJSON_PrintPreallocated(root, resp, PROTOCOL_SEND_BUF_SIZE, 0);

    PROTOCOL_DEBUG("dwLen:%d resp:%s\r\n", strlen(resp), resp);

    hs->send_flag = 1;
    dwHttpCode = SET_HTTP_FILE_TYPE(dwHttpCode, HTTP_FILE_TYPE_JSON);

    /* 发送自定义数据 */
    sendCallback(hs, resp, dwHttpCode);

    cJSON_Delete(root);   // 释放 cJSON 对象
    os_free(resp);
    //os_free(json_str);
    return HTTP_OK;
}


int logExport(void *conn, void *args)
{
    char *resp  = NULL;
    HTTP_CODE dwHttpCode = HTTP_OK;
    cJSON *root = NULL;
    cJSON *data = NULL;
    char time_str[64] = {0};
    struct http_state *hs = NULL;
    //char *log_str = NULL;

    hs = (struct http_state *)conn;
    CUSTOM_ASSERT(NULL == hs, return HTTP_BAD_REQUEST);

    resp = (char *)os_malloc(LOG_BUF_MAX_SIZE + 256);
    CUSTOM_ASSERT(!resp, return HTTP_BAD_REQUEST);
    memset_s(resp, LOG_BUF_MAX_SIZE + 256, 0, LOG_BUF_MAX_SIZE + 256);

    // log_str = (char *)os_malloc(LOG_BUF_MAX_SIZE);
    // CUSTOM_ASSERT(!log_str, return RET_ERR);
    // memset_s(log_str, LOG_BUF_MAX_SIZE, 0, LOG_BUF_MAX_SIZE);

    readLogAll(resp, LOG_BUF_MAX_SIZE);
    PROTOCOL_DEBUG("log_str:%s\n", resp);

    // snprintf(resp, LOG_BUF_MAX_SIZE + 256,
    // "HTTP/1.1 200 OK\r\n"
    // "Content-Type: text/plain\r\n"
    // "Content-Disposition: attachment; filename=\"system.log\"\r\n"
    // "Content-Length: %d\r\n"
    // "\r\n"
    // "%s",
    // strlen(log_str), log_str);

    PROTOCOL_DEBUG("dwLen:%d resp:%s\r\n", strlen(resp), resp);

    hs->send_flag = 1;
    dwHttpCode = SET_HTTP_FILE_TYPE(dwHttpCode, HTTP_FILE_TYPE_LOG);
    /* 发送自定义数据 */
    sendCallback(hs, resp, dwHttpCode);

    cJSON_Delete(root);   // 释放 cJSON 对象
    os_free(resp);
    //os_free(log_str);
    return HTTP_OK;
}


int logConfigV2(void *conn, void *args)
{
    HTTP_CODE dwHttpCode = HTTP_OK;
    struct http_state *hs = NULL;
    //char *json_str = NULL;
    cJSON *root = NULL;
    cJSON *status = NULL;

    hs = (struct http_state *)conn;
    CUSTOM_ASSERT(NULL == hs, return HTTP_BAD_REQUEST);

    PROTOCOL_DEBUG("[%s:%d] \n",__FUNCTION__,__LINE__);
    if(hs->pkt)
    {
        PROTOCOL_DEBUG("POST payload:%s \n", hs->pkt);
    }

    root = cJSON_Parse(hs->pkt);
    if (!root)
    {
        os_debug("JSON parse error\r\n");
        dwHttpCode = HTTP_BAD_REQUEST;
    }

    /* test */
    status = cJSON_GetObjectItem(root, "test");
    if (cJSON_IsNumber(status)) 
    {
        os_printf("test: %d\n", status->valueint);
    }
    else if(cJSON_IsString(status))
    {
        os_printf("test: %s\n", status->valuestring);
    }

    return dwHttpCode;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
int wifiConfig(void *conn, void *args)
{
    /* pkt:
     * {
     *   data:{
     *     ssid: XXX
     *      psk: XXX
     *   }
     * }
     */
    HTTP_CODE dwHttpCode = HTTP_OK;
    struct http_state *hs = NULL;
    //char *json_str = NULL;
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *ssid = NULL;
    cJSON *psk = NULL;

    hs = (struct http_state *)conn;
    CUSTOM_ASSERT(NULL == hs, return HTTP_BAD_REQUEST);
    CUSTOM_ASSERT(NULL == hs->pkt, return HTTP_BAD_REQUEST);

    root = cJSON_Parse(hs->pkt);
    if (!root)
    {
        os_debug("JSON parse error\r\n");
        goto err_exit;
    }

    /* test */
    data = cJSON_GetObjectItem(root, "data");
    if (!data)
    {
        os_debug("JSON parse error\r\n");
        goto err_exit;
    }

    ssid = cJSON_GetObjectItem(data, "ssid");
    if(!ssid)
    {
        os_debug("JSON parse error\r\n");
        goto err_exit;
    }

    psk = cJSON_GetObjectItem(data, "psk");
    if(!psk)
    {
        os_debug("JSON parse error\r\n");
        goto err_exit;
    }

    /* test */
    //printf("ssid:%s  pwd:%s\r\n", ssid->valuestring, psk->valuestring);
    setEsp8266Ssid(ssid->valuestring);
    setEsp8266Psk(psk->valuestring);
    //sys_thread_new("tcpClient_task", ESP8266_StaTcpClient_Unvarnish_ConfigTest, NULL, 256, TASK_PRIORITY_NORMAL);
    ESP8266_StaTcpClient_Unvarnish_ConfigTest();
    os_free(hs->pkt);
    return dwHttpCode;

err_exit:
    dwHttpCode = HTTP_BAD_REQUEST;
    os_free(hs->pkt);
    return dwHttpCode;
}

int logInfo(void *conn, void *args)
{
    printf("[%s:%d]\n",__FUNCTION__,__LINE__);
    return HTTP_OK;
}

int network(void *conn, void *args)
{
    printf("[%s:%d]\n",__FUNCTION__,__LINE__);
    return HTTP_OK;
}

int logInfoV2(void *conn, void *args)
{
    printf("[%s:%d]\n",__FUNCTION__,__LINE__);
    return HTTP_OK;
}


/* 根节点数组 */
node_t root_node[] = {
    {"system", NULL, NULL, systems, METHOD_GET, "系统调用"},
    {"logManage", NULL, NULL, log, METHOD_GET, "日志"},
    {"network", network, NULL, NULL, METHOD_GET | METHOD_POST, "网络参数"},
    {"wifi", NULL, NULL, wifi, METHOD_GET | METHOD_POST, " 无线参数"},
    {"/root_node", NULL, NULL, NULL, METHOD_GET, "hello"},
};

node_t log[] = {
    {"export", logExport, root_node, NULL, METHOD_GET, NULL},
    {"config", NULL, root_node, logConfig, METHOD_GET, NULL},
    {"info", logInfo, root_node, NULL, METHOD_GET, NULL},
    {"/logInfo",NULL, NULL, NULL, METHOD_GET, NULL},
};

node_t systems[] = {
    {"config", systemConfig, root_node, NULL, METHOD_GET, NULL},
    {"info", systemInfo, root_node, NULL, METHOD_GET, NULL},
    {"/system", NULL, NULL, NULL, METHOD_GET, NULL},
};

node_t wifi[] = {
    {"config", wifiConfig, root_node, NULL, METHOD_GET, NULL},
    {"/system", NULL, NULL, NULL, METHOD_GET, NULL},
};

node_t logConfig[] = {
    {"info", logInfoV2, log, NULL, METHOD_GET, NULL},
    {"test", logConfigV2, log, NULL, METHOD_GET, NULL},
    {"/system", NULL, NULL, NULL, METHOD_GET, NULL},
};


int protocol_callback(struct http_state *hs, uint32_t status)
{
    char *resp  = NULL;
    //char *json_str  = NULL;
    //uint32 dwLen = 0;
    cJSON *root = NULL;

    CUSTOM_ASSERT(NULL == hs, return RET_ERR);
    /* has send */
    if(hs->send_flag)
    {
        hs->send_flag = 0;
        return RET_OK;
    }
    resp = (char *)os_malloc(PROTOCOL_SEND_BUF_SIZE);
    CUSTOM_ASSERT(!resp, return RET_ERR);
    memset_s(resp, PROTOCOL_SEND_BUF_SIZE, 0, PROTOCOL_SEND_BUF_SIZE);

    // json_str = (char *)os_malloc(PROTOCOL_SEND_BUF_SIZE);
    // CUSTOM_ASSERT(!json_str, return RET_ERR);
    // memset_s(json_str, PROTOCOL_SEND_BUF_SIZE, 0, PROTOCOL_SEND_BUF_SIZE);
    status = GET_HTTP_STATUS(status);
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "status", status);
    cJSON_AddStringToObject(root, "code", "0x00000000");
    cJSON_AddStringToObject(root, "errorMsg", (200 == status)?"ok":"fail");
    /* json data to string(json_str) */
    cJSON_PrintPreallocated(root, resp, PROTOCOL_SEND_BUF_SIZE, 0);

    // snprintf(resp, PROTOCOL_SEND_BUF_SIZE, 
    //          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", 
    //          strlen(json_str), json_str);

    //dwLen = strlen(resp);
    //PROTOCOL_DEBUG("resp:%s dwLen:%d\r\n", resp, strlen(resp));
    PROTOCOL_DEBUG("resp:%s dwLen:%d\r\n", resp, strlen(resp));
    status = SET_HTTP_FILE_TYPE(status, HTTP_FILE_TYPE_JSON);
    sendCallback(hs, resp, status);

    cJSON_Delete(root); // 释放 cJSON 对象
    os_free(resp);
    //os_free(json_str);
    return RET_OK;
}


int processProtocol(void *conn, char *url)
{
    struct http_state *hs = NULL;
    CUSTOM_ASSERT(NULL == url, return RET_ERR);
    node_t *node = NULL;
    process system_process = NULL;
    HTTP_CODE dwHttpCode = 0;

    hs = (struct http_state*)conn;
    PROTOCOL_DEBUG("[%s:%d] url:%s \r\n",__FUNCTION__,__LINE__, url);

    url += strlen(XXX_PROTOCOL);
    system_process = getProcess(root_node, url, &node);
    if(NULL == system_process)
    {
        os_debug("system_process is NULL!\r\n");
        return RET_ERR;
    }
    PROTOCOL_DEBUG("servicename:%s\r\n",node->servicename);
    dwHttpCode = (*system_process)(hs, NULL);
    protocol_callback(hs, dwHttpCode);

    hs->http_method = 0;
    if (hs->pkt) {
        os_free(hs->pkt);
        hs->pkt = NULL;
    }

    return RET_OK;
}


/* 根据url获取node和回调函数、基于一个前提：node有next节点就不能有回调函数
   获取节点需要传二级指针、传一级指针不行
*/
process getProcess(node_t *node, char *path, node_t **targetNode)
{
    #if 0
    char *p1 = NULL;
    char *p2 = NULL;
    char servicename[32] = {0};
    int len = 0;
    node_t *pcurrentnode = NULL;
    node_t *lastnode = NULL;
    /* 标识是否找到匹配的节点 */
    char byFindService = 1;

    if(NULL == path || NULL == node)
    {
        os_debug("invalid param!\r\n");
        return NULL;
    }
    p1 = path;

    pcurrentnode = node;

    /* logInfo/config/config */
    /* 遍历root_node数组中所有根节点 */
    while(*(pcurrentnode->servicename) != '/')
    {
        /* 获取当前节点 */
        *targetNode = pcurrentnode;
        printf("node->servicename:%s\n",pcurrentnode->servicename);
#if 1
        if(byFindService)
        {
            p2 = strstr(p1, "/");
            if(NULL == p2)
            {
                /* 只剩最后一个，获取service */
                strcpy(servicename, p1);
                goto exit;
            }
            /* 获取service name len */
            len = p2 - p1;
            /* 跳过 "/" */
            p2 += 1;
            //printf("%d\n",len);
            if(len < 0 || len > sizeof(servicename))
            {
                printf("invalid len\n");
                return NULL;
            }
            //printf("%s\n", p1);
            memset(servicename, 0, sizeof(servicename));
            strncpy(servicename, p1, len);
            p1 = p2;
#if DEBUG_MODE
            printf("name:%s\n",servicename);
#endif
        }
exit:
#endif
        if(strstr(pcurrentnode->servicename, servicename))
        {
            /* 找到回调函数 */
            if(pcurrentnode->process)
            {
                printf("exit %s\n",pcurrentnode->servicename);
                return pcurrentnode->process;
            }
            else if(pcurrentnode->nextnode)
            {
                /* 找到下一个节点 */
                pcurrentnode = pcurrentnode->nextnode;
                byFindService = 1;
                continue;
            }
            else
            {
                /* 节点的回调函数为空，且没有下一个节点 */
                return NULL;
            }
        }
        else
        {
            byFindService = 0;
        }
        /* 进入下一个节点寻找 */
        pcurrentnode++;
    }
    printf("no match node\n");
    return NULL;
    #endif
    char pathCopy[128] = {0};
    char *token = NULL;
    node_t *currentLevel = node;
    node_t *foundNode = NULL;

    if (!node || !path)
        return NULL;

    strncpy(pathCopy, path, sizeof(pathCopy) - 1);
    pathCopy[sizeof(pathCopy) - 1] = '\0';

    // 以 “/” 分割路径，如 logInfo/config/config
    token = strtok(pathCopy, "/");
    while (token && currentLevel)
    {
        foundNode = NULL;

        // 遍历当前层节点数组，匹配名字（以 '/' 开头的 servicename 为结束哨兵）
        while (currentLevel->servicename[0] && currentLevel->servicename[0] != '/')
        {
            if (strcmp(currentLevel->servicename, token) == 0)
            {
                foundNode = currentLevel;
                break;
            }
            currentLevel++;
        }

        if (!foundNode)
        {
            os_debug("no match node: %s\n", token);
            return NULL;
        }

        // 匹配到节点后，看是否是最后一级
        token = strtok(NULL, "/");
        if (!token)
        {
            // 最后一级 → 返回其 process
            if (foundNode->process)
            {
                *targetNode = foundNode;
                return foundNode->process;
            }
            else
            {
                os_debug("node has no service: %s\n", foundNode->servicename);
                return NULL;
            }
        }
        else
        {
            // 继续往下一级
            currentLevel = foundNode->nextnode;
        }
    }
    os_debug("no match node: %s\n", path);
    return NULL;
}


#if 0
/* 提取下一个路径段 (例如 "systems/info" -> "systems") */
static const char* get_next_segment(const char *path, char *buf, size_t buflen)
{
    if (!path || !*path) return NULL;

    const char *slash = strchr(path, '/');
    size_t len = slash ? (size_t)(slash - path) : strlen(path);

    if (len >= buflen) return NULL;  // 防止溢出

    strncpy(buf, path, len);
    buf[len] = '\0';

    return slash ? slash + 1 : NULL;
}

/* 遍历节点，根据路径找到对应的 process 回调 */
process getProcess(node_t *nodes, char *path, node_t **targetNode)
{
    char segment[256] = {0};
    const char *rest = path;
    node_t *current = nodes;

    if (!nodes || !path || !*path) {
        os_debug("invalid param!\r\n");
        return NULL;
    }

    while (rest) {
        rest = get_next_segment(rest, segment, sizeof(segment));
        if (!*segment) break;  // 空段直接退出

        // 遍历当前节点数组
        int found = 0;
        for (node_t *n = current; n->servicename != NULL; n++) {
            if (strcmp(n->servicename, segment) == 0) {
                *targetNode = n;

                // 如果有子节点，进入下一层
                if (n->nextnode) {
                    current = n->nextnode;
                    found = 1;
                    break;
                }

                // 如果有回调函数，且这是最后一个 segment
                if (!rest || !*rest) {
                    return n->process;
                }

                // 找到了，但没有子节点也没有 process
                return NULL;
            }
        }

        if (!found) {
            // 当前层没找到匹配节点
            return NULL;
        }
    }

    return NULL;
}

/* get_next_segment: 从 path 提取下一个非空段到 buf 中（跳过前导 /） */
/* 返回值：指向下一个段的起始处（不含前导 /），若没有更多段或出错则返回 NULL */
static const char* get_next_segment(const char *path, char *buf, size_t buflen)
{
    if (!path || !buf || buflen == 0) {
        if (buf && buflen) buf[0] = '\0';
        return NULL;
    }

    /* skip leading slashes */
    while (*path == '/') path++;
    if (!*path) {
        buf[0] = '\0';
        return NULL;
    }

    const char *slash = strchr(path, '/');
    size_t len = slash ? (size_t)(slash - path) : strlen(path);

    /* 太长则视为错误（避免溢出），并把 buf 置空 */
    if (len >= buflen) {
        buf[0] = '\0';
        return NULL;
    }

    memcpy(buf, path, len);
    buf[len] = '\0';

    return slash ? (slash + 1) : NULL;
}


/* getProcess（更安全版本）
 * nodes: 当前层节点数组（应以 servicename==NULL 结尾，或传入受控数组）
 * path: 路径，例如 "systems/info"
 * targetNode: 输出找到的最后一级 node（若需要）
 *
 * 返回：找到的 process 回调，找不到或出错返回 NULL
 */
process getProcess(node_t *nodes, char *path, node_t **targetNode)
{
    char segment[256];
    const char *rest = path;
    node_t *current = nodes;

    if (targetNode) *targetNode = NULL;
    if (!nodes || !path || !*path) {
        os_debug("getProcess: invalid param\n");
        return NULL;
    }

    while (1) {
        rest = get_next_segment(rest, segment, sizeof(segment));
        if (segment[0] == '\0') {
            /* 空段或出错（例如段过长），视为路径结束或错误 */
            return NULL;
        }

        /* 在 current 层查找匹配的 servicename */
        int found = 0;
        int scan_count = 0;
        for (node_t *n = current; n && n->servicename; n++) {
            if (++scan_count > 1024) { // 防护：避免未终止数组导致无限循环
                os_debug("getProcess: node list too long or not terminated\n");
                return NULL;
            }

            if (strcmp(n->servicename, segment) == 0) {
                /* 找到该段对应的节点 */
                if (targetNode) *targetNode = n;

                /* 如果还有更多段（rest 非空且不以 '\0' 开头） */
                if (rest && *rest) {
                    if (n->nextnode) {
                        /* 进入子节点继续匹配 */
                        current = n->nextnode;
                        found = 1;
                        break;
                    } else {
                        /* 还有段但没有子节点，路径无效 */
                        return NULL;
                    }
                } else {
                    /* 最后一段，返回 process（可能为 NULL） */
                    return n->process;
                }
            }
        } /* end for */

        if (!found) {
            /* 当前层没找到匹配节点 */
            return NULL;
        }

        /* 如果 rest 为 NULL，下一循环会早退出；但上面已经处理了 last-segment 的返回 */
    }

    /* 不可达 */
    return NULL;
}
#endif

