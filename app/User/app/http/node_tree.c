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
#include "devConfig.h"
#if defined(CONFIG_APP_ESP8266)
#include "bsp_esp8266.h"
#include "bsp_esp8266_test.h"
#endif
#include "sntp_api.h"
#include "log.h"

const char *http_header_strings[] = {
    [HTTP_OK] = "HTTP/1.1 200 OK\r\n",
    [HTTP_NOT_FOUND] = "HTTP/1.1 404 File not found\r\n",
    [HTTP_BAD_REQUEST] = "HTTP/1.1 400 Bad Request\r\n",
    [HTTP_NOT_IMPL] = "HTTP/1.1 501 Not Implemented\r\n"
};

const char *http_content_strings[] = {
    [HTTP_FILE_TYPE_LOG] = HTTP_CONTENT_DISPO_LOG,
    [HTTP_FILE_TYPE_JSON] = HTTP_HDR_JSON
};

#if !defined(CONFIG_APP_ESP8266)
static void getWeather(char *data, uint8_t len)
{
    if (!data || !len) {
        return;
    }
    data[0] = '\0';
}

static int getTemperature(void)
{
    return 0;
}
#endif

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

/* 统一信封：{status, code, errorMsg, data}。data 所有权转给本函数。 */
static int protocol_send_json(struct http_state *hs, HTTP_CODE status,
                              const char *errmsg, cJSON *data)
{
    char *resp = NULL;
    cJSON *root = NULL;
    uint16_t st = (uint16_t)GET_HTTP_STATUS(status);
    HTTP_CODE code = status;

    CUSTOM_ASSERT(NULL == hs, {
        if (data) {
            cJSON_Delete(data);
        }
        return HTTP_BAD_REQUEST;
    });

    if (0 == st) {
        st = HTTP_OK;
    }
    if (!errmsg) {
        errmsg = (HTTP_OK == st) ? "ok" : "fail";
    }

    root = cJSON_CreateObject();
    if (!root) {
        if (data) {
            cJSON_Delete(data);
        }
        return HTTP_BAD_REQUEST;
    }
    cJSON_AddNumberToObject(root, "status", st);
    cJSON_AddStringToObject(root, "code", (HTTP_OK == st) ? "0" : "1");
    cJSON_AddStringToObject(root, "errorMsg", errmsg);
    if (data) {
        cJSON_AddItemToObject(root, "data", data);
        data = NULL;
    } else {
        cJSON_AddObjectToObject(root, "data");
    }

    resp = (char *)os_malloc(PROTOCOL_SEND_BUF_SIZE);
    if (!resp) {
        cJSON_Delete(root);
        return HTTP_BAD_REQUEST;
    }
    memset_s(resp, PROTOCOL_SEND_BUF_SIZE, 0, PROTOCOL_SEND_BUF_SIZE);
    if (!cJSON_PrintPreallocated(root, resp, PROTOCOL_SEND_BUF_SIZE, 0)) {
        cJSON_Delete(root);
        os_free(resp);
        return HTTP_BAD_REQUEST;
    }
    cJSON_Delete(root);

    hs->send_flag = 1;
    code = SET_HTTP_FILE_TYPE(st, HTTP_FILE_TYPE_JSON);
    sendCallback(hs, resp, code);
    os_free(resp);
    return (int)st;
}


int systemConfig(void *conn, void *args)
{
    printf("[%s:%d]\n",__FUNCTION__,__LINE__);
    return HTTP_OK;
}

int systemInfo(void *conn, void *args)
{
    cJSON *data = NULL;
    char time_str[64] = {0};
    uint64_t ms_data = 0;
    struct http_state *hs = NULL;
    int8_t byCpuUsage = 0;
    int8_t byMemUsage = 0;

    (void)args;
    hs = (struct http_state *)conn;
    CUSTOM_ASSERT(NULL == hs, return HTTP_BAD_REQUEST);

    data = cJSON_CreateObject();
    if (!data) {
        return HTTP_BAD_REQUEST;
    }

    ms_data = sys_jiffies();
    format_uptime(ms_data, time_str, sizeof(time_str));
    cJSON_AddStringToObject(data, "uptime", time_str);

    byCpuUsage = (int8_t)getCpuUsage();
    if (byCpuUsage > 100) {
        byCpuUsage = 100;
    }
    cJSON_AddNumberToObject(data, "cpu_usage", byCpuUsage);

    byMemUsage = (int8_t)getMemUsage();
    if (byMemUsage > 100) {
        byMemUsage = 100;
    }
    cJSON_AddNumberToObject(data, "mem_usage", byMemUsage);
    cJSON_AddNumberToObject(data, "temperature", getTemperature());

    memset(time_str, 0, sizeof(time_str));
    getWeather(time_str, sizeof(time_str));
    cJSON_AddStringToObject(data, "weather", time_str);

    memset(time_str, 0, sizeof(time_str));
    print_timestamp(time_str);
    cJSON_AddStringToObject(data, "systime", time_str);

    return protocol_send_json(hs, HTTP_OK, "ok", data);
}


int logExport(void *conn, void *args)
{
    char *resp  = NULL;
    HTTP_CODE dwHttpCode = HTTP_OK;
    cJSON *root = NULL;
    cJSON *data = NULL;
    (void)data;
    char time_str[64] = {0};
    (void)time_str;
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
/*****************************************************
 * @fn       wifiConfig
 * @brief    /protocol/wifi/config  节点始终挂在树上；无 ESP8266 时回明确错误
 *****************************************************/
int wifiConfig(void *conn, void *args)
{
    struct http_state *hs = NULL;
    cJSON *payload = NULL;
    char nv_ssid[WIFI_SSID_MAX + 1];
    char nv_psk[WIFI_PSK_MAX + 1];

    (void)args;
    hs = (struct http_state *)conn;
    CUSTOM_ASSERT(NULL == hs, return HTTP_BAD_REQUEST);

    memset(nv_ssid, 0, sizeof(nv_ssid));
    memset(nv_psk, 0, sizeof(nv_psk));
    if (getWifiStaParam(nv_ssid, sizeof(nv_ssid), nv_psk, sizeof(nv_psk)) != RET_OK) {
        nv_ssid[0] = '\0';
        nv_psk[0] = '\0';
    }
#if defined(CONFIG_APP_ESP8266)
    /* 参数区还是空（升级后尚未迁完 / 未落盘）时，用 RAM 里正在用的凭据 */
    if (nv_ssid[0] == '\0' && getEsp8266Ssid() && getEsp8266Ssid()[0]) {
        strncpy(nv_ssid, getEsp8266Ssid(), WIFI_SSID_MAX);
        nv_ssid[WIFI_SSID_MAX] = '\0';
    }
    if (nv_psk[0] == '\0' && getEsp8266Psk() && getEsp8266Psk()[0]) {
        strncpy(nv_psk, getEsp8266Psk(), WIFI_PSK_MAX);
        nv_psk[WIFI_PSK_MAX] = '\0';
    }
#endif

    /* 有 JSON body 就当 POST（避免 method 位丢失时把配置请求当成查询） */
    if ((hs->http_method != METHOD_POST) &&
        !(hs->pkt && hs->pkt[0] && (hs->pkt[0] == '{' || hs->pkt[0] == '['))) {
        payload = cJSON_CreateObject();
        if (!payload) {
            return HTTP_BAD_REQUEST;
        }
#if defined(CONFIG_APP_ESP8266)
        cJSON_AddStringToObject(payload, "state", ESP8266_WifiApplyStateStr());
        cJSON_AddStringToObject(payload, "error", ESP8266_WifiApplyError());
#else
        cJSON_AddStringToObject(payload, "state", "disabled");
        cJSON_AddStringToObject(payload, "error", "");
#endif
        cJSON_AddStringToObject(payload, "ssid", nv_ssid);
        cJSON_AddStringToObject(payload, "psk", nv_psk);
        cJSON_AddStringToObject(payload, "password", nv_psk);
        return protocol_send_json(hs, HTTP_OK, "ok", payload);
    }

#if defined(CONFIG_APP_ESP8266)
    {
        cJSON *root = NULL;
        cJSON *data = NULL;
        cJSON *ssid = NULL;
        cJSON *psk = NULL;
        const char *ssid_str = NULL;
        const char *psk_str = NULL;
        size_t ssid_len;
        size_t psk_len;
        int persist_ok;

        if (NULL == hs->pkt || hs->pkt[0] == '\0') {
            os_debug("wifiConfig empty body method=%u recved=%lu\r\n",
                     (unsigned)hs->http_method, (unsigned long)hs->pkt_recved_len);
            return protocol_send_json(hs, HTTP_BAD_REQUEST, "empty body", NULL);
        }

        os_debug("wifiConfig POST method=%u body:%s\r\n",
                 (unsigned)hs->http_method, hs->pkt);
        root = cJSON_Parse(hs->pkt);
        if (!root) {
            return protocol_send_json(hs, HTTP_BAD_REQUEST, "JSON parse error", NULL);
        }

        data = cJSON_GetObjectItem(root, "data");
        if (!data) {
            data = root;
        }

        ssid = cJSON_GetObjectItem(data, "ssid");
        if (!ssid || !cJSON_IsString(ssid) || !ssid->valuestring) {
            cJSON_Delete(root);
            return protocol_send_json(hs, HTTP_BAD_REQUEST, "missing ssid", NULL);
        }
        ssid_str = ssid->valuestring;
        while (*ssid_str == ' ' || *ssid_str == '\t') {
            ssid_str++;
        }
        ssid_len = strlen(ssid_str);
        if (ssid_len == 0 || ssid_len > ESP8266_SSID_MAX) {
            cJSON_Delete(root);
            return protocol_send_json(hs, HTTP_BAD_REQUEST, "bad ssid", NULL);
        }

        psk = cJSON_GetObjectItem(data, "psk");
        if (!psk) {
            psk = cJSON_GetObjectItem(data, "password");
        }
        if (!psk || !cJSON_IsString(psk) || !psk->valuestring) {
            cJSON_Delete(root);
            return protocol_send_json(hs, HTTP_BAD_REQUEST, "missing psk", NULL);
        }
        psk_str = psk->valuestring;
        psk_len = strlen(psk_str);
        if (psk_len > ESP8266_PSK_MAX) {
            cJSON_Delete(root);
            return protocol_send_json(hs, HTTP_BAD_REQUEST, "bad psk", NULL);
        }

        setEsp8266Ssid((char *)ssid_str);
        setEsp8266Psk((char *)psk_str);
        persist_ok = (ESP8266_WifiCredSave() == 0) ? 1 : 0;
        os_debug("wifiConfig: ssid_len=%u psk_len=%u persist=%d\r\n",
                 (unsigned)ssid_len, (unsigned)psk_len, persist_ok);
        ESP8266_RequestWifiReconfig();

        payload = cJSON_CreateObject();
        if (payload) {
            cJSON_AddStringToObject(payload, "apply", "async");
            cJSON_AddStringToObject(payload, "ssid", getEsp8266Ssid());
            cJSON_AddStringToObject(payload, "psk", getEsp8266Psk());
            cJSON_AddStringToObject(payload, "password", getEsp8266Psk());
            cJSON_AddStringToObject(payload, "state", ESP8266_WifiApplyStateStr());
            cJSON_AddNumberToObject(payload, "persist", persist_ok);
        }
        cJSON_Delete(root);
        return protocol_send_json(hs, HTTP_OK, "accepted", payload);
    }
#else
    return protocol_send_json(hs, HTTP_BAD_REQUEST, "ESP8266 disabled", NULL);
#endif
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
    {"wifi", wifiConfig, NULL, wifi, METHOD_GET | METHOD_POST, "无线参数"},
    {"/root_node", NULL, NULL, NULL, METHOD_GET, "hello"},
};

static node_t log[] = {
    {"export", logExport, root_node, NULL, METHOD_GET, NULL},
    {"config", NULL, root_node, logConfig, METHOD_GET, NULL},
    {"info", logInfo, root_node, NULL, METHOD_GET, NULL},
    {"/logInfo",NULL, NULL, NULL, METHOD_GET, NULL},
};

static node_t systems[] = {
    {"config", systemConfig, root_node, NULL, METHOD_GET, NULL},
    {"info", systemInfo, root_node, NULL, METHOD_GET, NULL},
    {"/system", NULL, NULL, NULL, METHOD_GET, NULL},
};

static node_t wifi[] = {
    {"config", wifiConfig, root_node, NULL, METHOD_GET | METHOD_POST, NULL},
    {"/wifi", NULL, NULL, NULL, METHOD_GET, NULL},
};

static node_t logConfig[] = {
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
    cJSON_AddStringToObject(root, "code", (200 == status) ? "0" : "1");
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
        os_debug("no protocol handler for '%s' (after %s)\r\n", url, XXX_PROTOCOL);
        protocol_callback(hs, HTTP_NOT_FOUND);
        hs->http_method = 0;
        if (hs->pkt) {
            os_free(hs->pkt);
            hs->pkt = NULL;
        }
        return RET_ERR;
    }
    PROTOCOL_DEBUG("servicename:%s\r\n",node->servicename);
    if (node->method && hs->http_method &&
        ((node->method & hs->http_method) == 0)) {
        protocol_send_json(hs, HTTP_BAD_REQUEST, "method not allowed", NULL);
        hs->http_method = 0;
        if (hs->pkt) {
            os_free(hs->pkt);
            hs->pkt = NULL;
        }
        return RET_ERR;
    }
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
    char pathCopy[128] = {0};
    char *token = NULL;
    node_t *currentLevel = node;
    node_t *foundNode = NULL;

    if (!node || !path)
        return NULL;

    strncpy(pathCopy, path, sizeof(pathCopy) - 1);
    pathCopy[sizeof(pathCopy) - 1] = '\0';
    {
        char *q = strchr(pathCopy, '?');
        if (q) {
            *q = '\0';
        }
    }

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

