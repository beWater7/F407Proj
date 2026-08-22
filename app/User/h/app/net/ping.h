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
void ping_run(const ip_addr_t *addr, int count);

#endif /* LWIP_PING_H */

