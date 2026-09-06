#ifndef _HAL_ETH_H
#define _HAL_ETH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_eth_config(void);
void hal_eth_check_link(void);
void hal_eth_check_frame_received(void);
uint16_t hal_eth_read_phy(uint16_t reg);

#define HAL_ETH_INIT_FLAG  0x01u
#define HAL_ETH_LINK_FLAG  0x10u
uint32_t hal_eth_get_status(void);
void hal_eth_on_link(void *netif);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_ETH_H */
