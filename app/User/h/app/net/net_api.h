/***************************************************************
 * @file    :  net_api.h
 * @brief   :  网络状态查询/配置抽象（应用层不直接访问 struct netif / lwIP）
 ***************************************************************/
#ifndef __NET_API_H__
#define __NET_API_H__

#include "typedef.h"

typedef struct {
    uint8_t addr[4];
} net_ipv4_t;

int net_get_ip(net_ipv4_t *ip, net_ipv4_t *mask, net_ipv4_t *gw);
int net_set_ip(const net_ipv4_t *ip, const net_ipv4_t *mask, const net_ipv4_t *gw);
int net_get_mac(uint8_t mac[6]);
int net_is_up(void);
int net_is_link_up(void);
int net_parse_ipv4(const char *text, net_ipv4_t *out);
void net_ipv4_to_str(const net_ipv4_t *ip, char *buf, size_t len);

#endif /* __NET_API_H__ */
