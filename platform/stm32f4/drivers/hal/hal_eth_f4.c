#include "hal_eth.h"
#include "stm32f4x7_eth.h"
#include "stm32f4x7_phy.h"
#include "lwip/netif.h"

extern __IO uint32_t EthStatus;

void hal_eth_config(void)
{
    ETH_BSP_Config();
}

void hal_eth_check_link(void)
{
    ETH_CheckLinkStatus(ETHERNET_PHY_ADDRESS);
}

void hal_eth_check_frame_received(void)
{
    ETH_CheckFrameReceived();
}

uint16_t hal_eth_read_phy(uint16_t reg)
{
    return (uint16_t)ETH_ReadPHYRegister(ETHERNET_PHY_ADDRESS, reg);
}

uint32_t hal_eth_get_status(void)
{
    return EthStatus;
}

void hal_eth_on_link(void *netif)
{
    ETH_link_callback((struct netif *)netif);
}
