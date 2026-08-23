/***************************************************************
 * @file    :  net_api.c
 * @brief   :  lwIP netif 封装，仅供 net/、http/ 等协议层之外的上层模块使用
 **************************************************************/
#include <stdio.h>
#include <string.h>
#include "net_api.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"

extern struct netif gnetif;

static void ip_to_net(const ip_addr_t *src, net_ipv4_t *dst)
{
    uint32_t v = ip4_addr_get_u32(ip_2_ip4(src));
    dst->addr[0] = (uint8_t)(v);
    dst->addr[1] = (uint8_t)(v >> 8);
    dst->addr[2] = (uint8_t)(v >> 16);
    dst->addr[3] = (uint8_t)(v >> 24);
}

static void net_to_ip(const net_ipv4_t *src, ip_addr_t *dst)
{
    uint32_t v = ((uint32_t)src->addr[0]) |
                 ((uint32_t)src->addr[1] << 8) |
                 ((uint32_t)src->addr[2] << 16) |
                 ((uint32_t)src->addr[3] << 24);
    ip_addr_set_ip4_u32(dst, v);
}

int net_get_ip(net_ipv4_t *ip, net_ipv4_t *mask, net_ipv4_t *gw)
{
    if (!ip || !mask || !gw) {
        return -1;
    }
    ip_to_net(&gnetif.ip_addr, ip);
    ip_to_net(&gnetif.netmask, mask);
    ip_to_net(&gnetif.gw, gw);
    return 0;
}

int net_set_ip(const net_ipv4_t *ip, const net_ipv4_t *mask, const net_ipv4_t *gw)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gateway;

    if (!ip || !mask || !gw) {
        return -1;
    }
    net_to_ip(ip, &ipaddr);
    net_to_ip(mask, &netmask);
    net_to_ip(gw, &gateway);
    netif_set_addr(&gnetif, &ipaddr, &netmask, &gateway);
    return 0;
}

int net_get_mac(uint8_t mac[6])
{
    if (!mac) {
        return -1;
    }
    memcpy(mac, gnetif.hwaddr, 6);
    return 0;
}

int net_is_up(void)
{
    return netif_is_up(&gnetif) ? 1 : 0;
}

int net_is_link_up(void)
{
    return netif_is_link_up(&gnetif) ? 1 : 0;
}

int net_parse_ipv4(const char *text, net_ipv4_t *out)
{
    ip_addr_t addr;
    if (!text || !out) {
        return -1;
    }
    if (!ipaddr_aton(text, &addr)) {
        return -1;
    }
    ip_to_net(&addr, out);
    return 0;
}

void net_ipv4_to_str(const net_ipv4_t *ip, char *buf, size_t len)
{
    if (!ip || !buf || len == 0) {
        return;
    }
    snprintf(buf, len, "%u.%u.%u.%u",
             ip->addr[0], ip->addr[1], ip->addr[2], ip->addr[3]);
}
