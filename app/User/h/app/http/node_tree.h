#ifndef __NODE_TREE_H__
#define __NODE_TREE_H__

#include "os_debug.h"


#define METHOD_GET  1
#define METHOD_PUT  2
#define METHOD_POST 3

#define PROTOCOL_SEND_BUF_SIZE 1024


#define PTC_DEBUG   0
//#define PTC_DEBUG_LEVEL "\0013"

#define PROTOCOL_DEBUG(format, ...)                            \
do {                                                           \
    if ((PTC_DEBUG)) {                                         \
        os_printf_api(KERN_TICK format, ##__VA_ARGS__);        \
    }                                                          \
} while(0)

typedef struct node_t
{
    char servicename[32];
    int (*process)(void *hs, void *args);
    struct node_t *prenode;
    struct node_t *nextnode;
    unsigned char method;
    char *description;
}node_t;

typedef enum
{
    HTTP_OK = 200,
    HTTP_NOT_FOUND = 404,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_IMPL = 501,
}HTTP_STATUS_T;


#define HTTP_CONTENT_TYPE(contenttype) "Content-Type: "contenttype"\r\n"
#define HTTP_CONTENT_DISPOSITION(contenttype, positiontype, filename) \
""contenttype"Content-Disposition: "positiontype"; filename="filename"\r\n"

//addicate browser download, not show file
#define HTTP_ATTACH             "attachment"
//addicate browser show file， not download
#define HTTP_INLINE             "inline"

#define HTTP_HDR_JSON            HTTP_CONTENT_TYPE("application/json")
#define HTTP_HDR_TEXT            HTTP_CONTENT_TYPE("text/plain")


#define HTTP_CODE                uint32_t
/* code:hight 16 bit: file type; low 16 bit: http status code */
#define HTTP_FILE_TYPE_LOG       0x0001
#define HTTP_FILE_TYPE_JSON      0x0002

/* set file type */
#define SET_HTTP_FILE_TYPE(code, type)  ((code & 0x0000FFFF) | ((type) << 16))
/*  */
#define GET_HTTP_FILE_TYPE(code)   (((code) >> 16) & 0xFFFF)

#define GET_HTTP_STATUS(code)      ((code) & 0x0000FFFF)

#define LOG_FILE_NAME            "system.log" 
#define HTTP_CONTENT_DISPO_LOG    HTTP_CONTENT_DISPOSITION(HTTP_HDR_TEXT, HTTP_ATTACH, LOG_FILE_NAME)


static const char* http_header_strings[] = {
    [HTTP_OK] = "HTTP/1.1 200 OK\r\n",
    [HTTP_NOT_FOUND] = "HTTP/1.1 404 File not found\r\n",
    [HTTP_BAD_REQUEST] = "HTTP/1.1 400 Bad Request\r\n",
    [HTTP_NOT_IMPL] = "HTTP/1.1 501 Not Implemented\r\n"
};


static const char* http_content_strings[] = {
    [HTTP_FILE_TYPE_LOG] = HTTP_CONTENT_DISPO_LOG,
    [HTTP_FILE_TYPE_JSON] = HTTP_HDR_JSON
};


typedef int (*process)(void *hs, void *args);

extern node_t root_node[];
process getProcess(node_t *node, char *path, node_t **targetNode);
int processProtocol(void *conn, char *url);
void sendCallback(void *conn, char *resp, HTTP_CODE code);
void sys_reboot_delay(uint32_t sec);

#endif /* __NODE_TREE_H__ */


