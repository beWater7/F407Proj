/**
  ******************************************************************************
  * @file    netconf.c
  * @author  MCD Application Team
  * @version V1.1.0
  * @date    31-July-2013
  * @brief   Network connection configuration
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2013 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/tcp.h"
/*#include "lwip/tcp_impl.h"*/
#include "lwip/priv/tcp_priv.h" //BY ldy 
#include "lwip/udp.h"
#include "lwip/etharp.h" //BY ldy
#include "lwip/dhcp.h"
#include "ethernetif.h"
#include "netconf.h"
#include "os_log.h"
#include <stdio.h>
#include "hal_eth.h"
#include "hal_led.h"
#include "netif/ethernet.h" //?????????ethernet_input added by ldy
#include "lwip/netif.h"
#include "lwip/tcpip.h"


/* Private typedef -----------------------------------------------------------*/
#define MAX_DHCP_TRIES        4

/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
struct netif gnetif;
uint32_t TCPTimer = 0;
uint32_t ARPTimer = 0;
uint32_t LinkTimer = 0;
uint32_t IPaddress = 0;

#ifdef USE_DHCP
uint32_t DHCPfineTimer = 0;
uint32_t DHCPcoarseTimer = 0;
volatile uint8_t DHCP_state;
#endif


/* Private functions ---------------------------------------------------------*/
void LwIP_DHCP_Process_Handle(void);
/**
* @brief  Initializes the lwIP stack for ????
* @param  None
* @retval None
*/
void LwIP_Init(void)
{
  /* LWIP 2.1.2 ??????????????LWIP_IPV4???LWIP_IPV6???,  
   * LWIP???????????????ipv4 ??? ipv6???????????????????? 
   */
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;

  /* Initializes the dynamic memory heap defined by MEM_SIZE.*/
  mem_init();

  /* Initializes the memory pools defined by MEMP_NUM_x.*/
  memp_init();

#ifdef USE_DHCP
//  ipaddr.u_addr.ip4.addr = 0;
//  netmask.u_addr.ip4.addr = 0;
//  gw.u_addr.ip4.addr = 0;
	
  ipaddr.addr = 0;
  netmask.addr = 0;
  gw.addr = 0;
#else
//  IP4_ADDR(&ipaddr.u_addr.ip4, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
//  IP4_ADDR(&netmask.u_addr.ip4, NETMASK_ADDR0, NETMASK_ADDR1 , NETMASK_ADDR2, NETMASK_ADDR3);
//  IP4_ADDR(&gw.u_addr.ip4, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);

  IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
  IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1 , NETMASK_ADDR2, NETMASK_ADDR3);
  IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
#endif  
  
  /* - netif_add(struct netif *netif, struct ip_addr *ipaddr,
  struct ip_addr *netmask, struct ip_addr *gw,
  void *state, err_t (* init)(struct netif *netif),
  err_t (* input)(struct pbuf *p, struct netif *netif))

  Adds your network interface to the netif_list. Allocate a struct
  netif and pass a pointer to this structure as the first argument.
  Give pointers to cleared ip_addr structures when using DHCP,
  or fill them with sane numbers otherwise. The state pointer may be NULL.

  The init function pointer must point to a initialization function for
  your ethernet netif interface. The following code illustrates it's use.*/
  
  //netif_add(&gnetif, &ipaddr.u_addr.ip4, &netmask.u_addr.ip4, &gw.u_addr.ip4, NULL, &ethernetif_init, &ethernet_input);
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

  /*  Registers the default network interface.*/
  netif_set_default(&gnetif);

  if (hal_eth_get_status() == (HAL_ETH_INIT_FLAG | HAL_ETH_LINK_FLAG))
  { 
    /* Set Ethernet link flag */
    gnetif.flags |= NETIF_FLAG_LINK_UP;

    /* When the netif is fully configured this function must be called.*/
    netif_set_up(&gnetif);
#ifdef USE_DHCP
    DHCP_state = DHCP_START;
#else
    LOGR(LOG_MOD_NET, "static IP %d.%d.%d.%d\r\n", IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
#endif /* USE_DHCP */
  }
  else
  {
    /*  When the netif link is down this function must be called.*/
    netif_set_down(&gnetif);
#ifdef USE_DHCP
    DHCP_state = DHCP_LINK_DOWN;
#endif /* USE_DHCP */
    LOGR(LOG_MOD_NET, "cable not connected\r\n");
  }

#if LWIP_NETIF_LINK_CALLBACK
  /* Set the link callback function, this function is called on change of link status*/
  netif_set_link_callback(&gnetif, (netif_status_callback_fn)hal_eth_on_link);
#endif
}


/**
* @brief  ??????????????????lwip netif????????
* @param  None
* @retval None
*/
void lwip_netif_init(void)
{
 /* LWIP 2.1.2 ??????????????LWIP_IPV4???LWIP_IPV6???,  
  * LWIP???????????????ipv4 ??? ipv6???????????????????? 
  */
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;

//#ifdef USE_DHCP
//  ipaddr.u_addr.ip4.addr = 0;
//  netmask.u_addr.ip4.addr = 0;
//  gw.u_addr.ip4.addr = 0;
	
  ipaddr.addr = 0;
  netmask.addr = 0;
  gw.addr = 0;
//#else
//  IP4_ADDR(&ipaddr.u_addr.ip4, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
//  IP4_ADDR(&netmask.u_addr.ip4, NETMASK_ADDR0, NETMASK_ADDR1 , NETMASK_ADDR2, NETMASK_ADDR3);
//  IP4_ADDR(&gw.u_addr.ip4, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);

  IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
  IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1 , NETMASK_ADDR2, NETMASK_ADDR3);
  IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
//#endif
  
  /* - netif_add(struct netif *netif, struct ip_addr *ipaddr,
  struct ip_addr *netmask, struct ip_addr *gw,
  void *state, err_t (* init)(struct netif *netif),
  err_t (* input)(struct pbuf *p, struct netif *netif))

  Adds your network interface to the netif_list. Allocate a struct
  netif and pass a pointer to this structure as the first argument.
  Give pointers to cleared ip_addr structures when using DHCP,
  or fill them with sane numbers otherwise. The state pointer may be NULL.

  The init function pointer must point to a initialization function for
  your ethernet netif interface. The following code illustrates it's use.*/
  
  //netif_add(&gnetif, &ipaddr.u_addr.ip4, &netmask.u_addr.ip4, &gw.u_addr.ip4, NULL, &ethernetif_init, &ethernet_input);
  //netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

  /* ????????????????tcpip_input */
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);


  /*  Registers the default network interface.*/
  netif_set_default(&gnetif);

  printf("EthStatus=0x%lx (need INIT|LINK=0x11)\n", (unsigned long)hal_eth_get_status());

  if (hal_eth_get_status() & HAL_ETH_INIT_FLAG)
  {
    if (hal_eth_get_status() & HAL_ETH_LINK_FLAG)
    {
      /* Set Ethernet link flag */
      gnetif.flags |= NETIF_FLAG_LINK_UP;

      /* When the netif is fully configured this function must be called.*/
      netif_set_up(&gnetif);
#ifdef USE_DHCP
      DHCP_state = DHCP_START;
#endif
    }
    else
    {
      /* ????????? down?? ETH_CheckLinkStatus + link callback ? up */
      netif_set_down(&gnetif);
#ifdef USE_DHCP
      DHCP_state = DHCP_LINK_DOWN;
#endif /* USE_DHCP */
      LOGR(LOG_MOD_NET, "cable not connected\r\n");
    }
  }
  else
  {
    netif_set_down(&gnetif);
    LOGW(LOG_MOD_NET, "ETH_Init failed, EthStatus=0x%lx\r\n", (unsigned long)hal_eth_get_status());
  }

#if LWIP_NETIF_LINK_CALLBACK
  /* Set the link callback function, this function is called on change of link status*/
  netif_set_link_callback(&gnetif, (netif_status_callback_fn)hal_eth_on_link);
#endif	

}



/**
* @brief  Called when a frame is received
* @param  None
* @retval None
*/
void LwIP_Pkt_Handle(void)
{
  /* Read a received packet from the Ethernet buffers and send it to the lwIP for handling */
  ethernetif_input(&gnetif);
}

/**
* @brief  LwIP periodic tasks
* @param  localtime the current LocalTime value
* @retval None
*/
void LwIP_Periodic_Handle(volatile uint32_t localtime)
{
#if LWIP_TCP
  /* TCP periodic process every 250 ms */
  if (localtime - TCPTimer >= TCP_TMR_INTERVAL)
  {
    TCPTimer =  localtime;
    tcp_tmr();
  }
#endif

  /* ARP periodic process every 5s */
  if ((localtime - ARPTimer) >= ARP_TMR_INTERVAL)
  {
    ARPTimer =  localtime;
    etharp_tmr();
  }

	/* Check link status periodically */
	if ((localtime - LinkTimer) >= LINK_TIMER_INTERVAL) {
		hal_eth_check_link();
		LinkTimer=localtime;
	}
	
#ifdef USE_DHCP
  if (localtime - DHCPfineTimer >= DHCP_FINE_TIMER_MSECS)
  {
    DHCPfineTimer =  localtime;
    dhcp_fine_tmr();
    if ((DHCP_state != DHCP_ADDRESS_ASSIGNED) && 
        (DHCP_state != DHCP_TIMEOUT) &&
          (DHCP_state != DHCP_LINK_DOWN))
    {
#ifdef SERIAL_DEBUG
			hal_led_toggle(HAL_LED_1);
#endif /* SERIAL_DEBUG */

      /* process DHCP state machine */
      LwIP_DHCP_Process_Handle();    
    }
  }

  /* DHCP Coarse periodic process every 60s */
  if (localtime - DHCPcoarseTimer >= DHCP_COARSE_TIMER_MSECS)
  {
    DHCPcoarseTimer =  localtime;
    dhcp_coarse_tmr();
  }
  //printf("2222222222222222222222222222\n");
#endif
}

#ifdef USE_DHCP
uint8_t gw_addr2;

/* ??????lwip2.1.2 DHCP ip??????????????????????????????????? added by liudayi*/
void dhcp_done(struct netif *netif)
{
	  struct dhcp *dhcp = netif_dhcp_data(&gnetif);
	  ip_addr_t netmask;
  	  ip_addr_t gw;
  
	  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp_stop()\n"));
	  /* netif is DHCP configured? */
	  if (dhcp != NULL) {
#if LWIP_DHCP_AUTOIP_COOP
		if(dhcp->autoip_coop_state == DHCP_AUTOIP_COOP_STATE_ON) {
		  autoip_stop(netif);
		  dhcp->autoip_coop_state = DHCP_AUTOIP_COOP_STATE_OFF;
		}
#endif /* LWIP_DHCP_AUTOIP_COOP */
	
		//LWIP_ASSERT("reply wasn't freed", /*dhcp->msg_in == NULL*/);
		//dhcp_set_state(dhcp, DHCP_OFF);

	  gw_addr2 = (uint8_t)(gnetif.ip_addr.addr >> 16);
	  
	  /* ??????????ip??????? */
	  IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
	  IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, gw_addr2, GW_ADDR3);
	  netif_set_addr(&gnetif, &gnetif.ip_addr, &netmask, &gw);

	  }
}


/**
* @brief  LwIP_DHCP_Process_Handle
* @param  None
* @retval None
*/
void LwIP_DHCP_Process_Handle(void)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;
  /* LWIP 2.1.2 ? dhcp ????????????netif???????????? by ldy */
  struct dhcp *dhcp = netif_dhcp_data(&gnetif);

  switch (DHCP_state)
  {
    case DHCP_START:
    {
      DHCP_state = DHCP_WAIT_ADDRESS;
      dhcp_start(&gnetif);
      /* clear old IP every time we (re)start DHCP */
      IPaddress = 0;
      LOGR(LOG_MOD_NET, "dhcp start\r\n");
    }
    break;

    case DHCP_WAIT_ADDRESS:
    {
      IPaddress = gnetif.ip_addr.addr;

      if (IPaddress != 0)
      {
        DHCP_state = DHCP_ADDRESS_ASSIGNED;
        /* keep negotiated IP, fill static netmask/gateway */
        dhcp_done(&gnetif);
        LOGR(LOG_MOD_NET, "IP assigned %d.%d.%d.%d (mask %d.%d.%d.%d gw %d.%d.%d.%d)\r\n",
             (uint8_t)(IPaddress), (uint8_t)(IPaddress >> 8),
             (uint8_t)(IPaddress >> 16), (uint8_t)(IPaddress >> 24),
             NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3,
             GW_ADDR0, GW_ADDR1, gw_addr2, GW_ADDR3);
        hal_led_on(HAL_LED_1);
      }
      else
      {
        /* no address yet: keep waiting; give up after MAX_DHCP_TRIES */
        if (dhcp->tries > MAX_DHCP_TRIES)
        {
          DHCP_state = DHCP_TIMEOUT;
          dhcp_stop(&gnetif);

          /* fallback to static configuration */
          IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
          IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
          IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
          netif_set_addr(&gnetif, &ipaddr, &netmask, &gw);

          LOGR(LOG_MOD_NET, "DHCP timeout, fallback static IP %d.%d.%d.%d\r\n",
               IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
          hal_led_on(HAL_LED_1);
        }
      }
    }
    break;

    default:
    break;
  }

}
#endif

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
