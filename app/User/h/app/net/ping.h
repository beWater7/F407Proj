#ifndef LWIP_PING_H
#define LWIP_PING_H

#include "lwip/ip_addr.h"

#include "os_debug.h"

/**
 * PING_USE_SOCKETS: Set to 1 to use sockets, otherwise the raw api is used
 */
#ifndef PING_USE_SOCKETS
#define PING_USE_SOCKETS    0 // LWIP_SOCKET
#endif

void ping_init(const ip_addr_t* ping_addr);

#if !PING_USE_SOCKETS
void ping_send_now(void);
#endif /* !PING_USE_SOCKETS */

void ping_send(ip_addr_t *addr);

#define PING_PRINT_EN   1

#define PING_PRINT(format, ...)                            \
do {                                                       \
    if ((PING_PRINT_EN)) {                                    \
        os_printf_api(format, ##__VA_ARGS__);              \
    }                                                      \
} while(0)                                                 


#define IP4_ADDR_PRINT_PARTS(a, b, c, d) \
  PING_PRINT("%" U16_F ".%" U16_F ".%" U16_F ".%" U16_F, a, b, c, d)
#define IP4_ADDR_PRINT(ipaddr) \
  IP4_ADDR_PRINT_PARTS(     \
                      (u16_t)((ipaddr) != NULL ? ip4_addr1_16(ipaddr) : 0),       \
                      (u16_t)((ipaddr) != NULL ? ip4_addr2_16(ipaddr) : 0),       \
                      (u16_t)((ipaddr) != NULL ? ip4_addr3_16(ipaddr) : 0),       \
                      (u16_t)((ipaddr) != NULL ? ip4_addr4_16(ipaddr) : 0))


#endif /* LWIP_PING_H */

