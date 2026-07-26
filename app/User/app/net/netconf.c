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
#include <stdio.h>
#include "stm32f4x7_phy.h"
#include "netif/ethernet.h" //¥¶¿Ì“‘Ã´Õ¯ethernet_input added by ldy
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
#include "../LED/bsp_led.h" 
uint32_t DHCPfineTimer = 0;
uint32_t DHCPcoarseTimer = 0;
__IO uint8_t DHCP_state;
#endif
extern __IO uint32_t  EthStatus;


/* Private functions ---------------------------------------------------------*/
void LwIP_DHCP_Process_Handle(void);
/**
* @brief  Initializes the lwIP stack for Ë£∏Ê?∫
* @param  None
* @retval None
*/
void LwIP_Init(void)
{
  /* LWIP 2.1.2 È??Ë¶ÅÂê?Ê?∂Ê??Âº?LWIP_IPV4Â??LWIP_IPV6‰∏§‰∏™ÂÆè,  
   * LWIPÂèØ‰ª•Ê?ØÊ?ÅÂè?Ê†?Ôº?ÂØπipv4 Â?? ipv6Â??Â?´Â§?Áê?Ë??‰∏ç‰º?Â?≤Á™Å 
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

  if (EthStatus == (ETH_INIT_FLAG | ETH_LINK_FLAG))
  { 
    /* Set Ethernet link flag */
    gnetif.flags |= NETIF_FLAG_LINK_UP;

    /* When the netif is fully configured this function must be called.*/
    netif_set_up(&gnetif);
#ifdef USE_DHCP
    DHCP_state = DHCP_START;
#else
#ifdef SERIAL_DEBUG
		printf("\n  Static IP address   \n");
		printf("IP: %d.%d.%d.%d\n",IP_ADDR0,IP_ADDR1,IP_ADDR2,IP_ADDR3);
		printf("NETMASK: %d.%d.%d.%d\n",NETMASK_ADDR0,NETMASK_ADDR1,NETMASK_ADDR2,NETMASK_ADDR3);
		printf("Gateway: %d.%d.%d.%d\n",GW_ADDR0,GW_ADDR1,GW_ADDR2,GW_ADDR3);
#endif /* SERIAL_DEBUG */
#endif /* USE_DHCP */
  }
  else
  {
    /*  When the netif link is down this function must be called.*/
    netif_set_down(&gnetif);
#ifdef USE_DHCP
    DHCP_state = DHCP_LINK_DOWN;
#endif /* USE_DHCP */
#ifdef SERIAL_DEBUG
		printf("\n  Network Cable is  \n");
		printf("    not connected   \n");
#endif /* SERIAL_DEBUG */
  }

#if LWIP_NETIF_LINK_CALLBACK
  /* Set the link callback function, this function is called on change of link status*/
  netif_set_link_callback(&gnetif, ETH_link_callback);
#endif
}


/**
* @brief  ‰∏∫Â∏¶Ê?ç‰Ω?Á≥ªÁª?Â??Â§?Á??lwip netifÂ?ùÂß?Â??
* @param  None
* @retval None
*/
void lwip_netif_init(void)
{
 /* LWIP 2.1.2 È??Ë¶ÅÂê?Ê?∂Ê??Âº?LWIP_IPV4Â??LWIP_IPV6‰∏§‰∏™ÂÆè,  
  * LWIPÂèØ‰ª•Ê?ØÊ?ÅÂè?Ê†?Ôº?ÂØπipv4 Â?? ipv6Â??Â?´Â§?Áê?Ë??‰∏ç‰º?Â?≤Á™Å 
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

  /* Ê?∞ÊçÆÊ?•Ê?∂Â??Ë∞?‰∏∫tcpip_input */
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);


  /*  Registers the default network interface.*/
  netif_set_default(&gnetif);

  printf("EthStatus=0x%lx (need INIT|LINK=0x11)\n", (unsigned long)EthStatus);

  if (EthStatus & ETH_INIT_FLAG)
  {
    if (EthStatus & ETH_LINK_FLAG)
    {
      /* Set Ethernet link flag */
      gnetif.flags |= NETIF_FLAG_LINK_UP;

      /* When the netif is fully configured this function must be called.*/
      netif_set_up(&gnetif);
#ifdef USE_DHCP
      DHCP_state = DHCP_START;
#endif
#ifdef SERIAL_DEBUG
      printf("\n  Static IP address   \n");
      printf("IP: %d.%d.%d.%d\n",IP_ADDR0,IP_ADDR1,IP_ADDR2,IP_ADDR3);
      printf("NETMASK: %d.%d.%d.%d\n",NETMASK_ADDR0,NETMASK_ADDR1,NETMASK_ADDR2,NETMASK_ADDR3);
      printf("Gateway: %d.%d.%d.%d\n",GW_ADDR0,GW_ADDR1,GW_ADDR2,GW_ADDR3);
#endif /* SERIAL_DEBUG */
    }
    else
    {
      /* ????????? down?? ETH_CheckLinkStatus + link callback ? up */
      netif_set_down(&gnetif);
#ifdef USE_DHCP
      DHCP_state = DHCP_LINK_DOWN;
#endif /* USE_DHCP */
#ifdef SERIAL_DEBUG
      printf("\n  Network Cable is  \n");
      printf("    not connected   \n");
#endif /* SERIAL_DEBUG */
    }
  }
  else
  {
    netif_set_down(&gnetif);
#ifdef SERIAL_DEBUG
    printf("\n  ETH_Init failed, EthStatus=0x%lx\n", (unsigned long)EthStatus);
#endif
  }

#if LWIP_NETIF_LINK_CALLBACK
  /* Set the link callback function, this function is called on change of link status*/
  netif_set_link_callback(&gnetif, ETH_link_callback);
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
void LwIP_Periodic_Handle(__IO uint32_t localtime)
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
		ETH_CheckLinkStatus(ETHERNET_PHY_ADDRESS);
		LinkTimer=localtime;
	}
	
#ifdef USE_DHCP
  /* Fine DHCP periodic process every 500ms */
  if (localtime - DHCPfineTimer >= DHCP_FINE_TIMER_MSECS)
  {
    DHCPfineTimer =  localtime;
    dhcp_fine_tmr();
    if ((DHCP_state != DHCP_ADDRESS_ASSIGNED) && 
        (DHCP_state != DHCP_TIMEOUT) &&
          (DHCP_state != DHCP_LINK_DOWN))
    {
#ifdef SERIAL_DEBUG
			LED1_TOGGLE;
			printf("\nFine DHCP periodic process every 500ms\n");
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

/* È??È?çlwip2.1.2 DHCP ipË?∑Âè?ÂÆ?Ê?êÊ?∂Ôº?Ëµ?Ê∫êÈ??Ê?æ„?ÅËÆæÁΩÆÁ?∂Ê?Å added by liudayi*/
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
	  
	  /* Ê†πÊçÆË?∑Âè?Á??ipËÆæÁΩÆÁΩ?Â?≥ */
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
  /* LWIP 2.1.2 ‰∏≠ dhcp Á?∏Â?≥ÂÆ?‰Ω?‰ª?netif‰∏≠Á?¨Á´?Â?∫Êù•‰∫? by ldy */
  struct dhcp *dhcp = netif_dhcp_data(&gnetif);

  switch (DHCP_state)
  {
    case DHCP_START:
    {
      DHCP_state = DHCP_WAIT_ADDRESS;
      dhcp_start(&gnetif);
      /* IP address should be set to 0 
         every time we want to assign a new DHCP address */
      IPaddress = 0;
#ifdef SERIAL_DEBUG
			printf("\n     Looking for    \n");
			printf("     DHCP server    \n");
			printf("     please wait... \n");
#endif /* SERIAL_DEBUG */
    }
    break;

    case DHCP_WAIT_ADDRESS:
    {
      /* Read the new IP address */
	   printf("DHCP_state:%d \n", DHCP_state);
      //IPaddress = gnetif.ip_addr.u_addr.ip4.addr;
      IPaddress = gnetif.ip_addr.addr;
	  printf("IP: %d.%d.%d.%d\n",(uint8_t)(IPaddress),(uint8_t)(IPaddress >> 8),
											(uint8_t)(IPaddress >> 16),(uint8_t)(IPaddress >> 24));

      if (IPaddress!=0) 
      {
        DHCP_state = DHCP_ADDRESS_ASSIGNED;	
		printf("IP: %d.%d.%d.%d\n",(uint8_t)(IPaddress),(uint8_t)(IPaddress >> 8),
											  (uint8_t)(IPaddress >> 16),(uint8_t)(IPaddress >> 24));

        /* LWIP 2.1.2Á??dhcp_stop‰º?È??Ê?æÂ?®Ê?ÅÂ??È?çÁ??IPÔº?Âê?Ê?∂Áª?Êù?dhcp client */
        //dhcp_stop(&gnetif);
        dhcp_done(&gnetif);

		printf("IP: %d.%d.%d.%d\n",(uint8_t)(IPaddress),(uint8_t)(IPaddress >> 8),
											  (uint8_t)(IPaddress >> 16),(uint8_t)(IPaddress >> 24));

#ifdef SERIAL_DEBUG
      	printf("\n  IP address assigned \n");
				printf("    by a DHCP server   \n");
		    printf("IP: %d.%d.%d.%d\n",(uint8_t)(IPaddress),(uint8_t)(IPaddress >> 8),
				                       (uint8_t)(IPaddress >> 16),(uint8_t)(IPaddress >> 24));
				printf("NETMASK: %d.%d.%d.%d\n",NETMASK_ADDR0,NETMASK_ADDR1,NETMASK_ADDR2,NETMASK_ADDR3);
				printf("Gateway: %d.%d.%d.%d\n",GW_ADDR0,GW_ADDR1, gw_addr2, GW_ADDR3);
				  IP4_ADDR(&ipaddr, IP_ADDR0 ,IP_ADDR1 , IP_ADDR2 , IP_ADDR3 );



        LED1_ON;
#endif /* SERIAL_DEBUG */
      }
      else
      {
        /* DHCP timeout */
        if (dhcp->tries > MAX_DHCP_TRIES)
        {
          DHCP_state = DHCP_TIMEOUT;

		  /* È??Ê?æÂ?®Ê?ÅÂ??È?çÁ??IPÔº?Âê?Ê?∂Áª?Êù?dhcp client */
          dhcp_stop(&gnetif);
      

          /* Static address used */
//          IP4_ADDR(&ipaddr.u_addr.ip4, IP_ADDR0 ,IP_ADDR1 , IP_ADDR2 , IP_ADDR3 );
//          IP4_ADDR(&netmask.u_addr.ip4, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
//          IP4_ADDR(&gw.u_addr.ip4, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
//          netif_set_addr(&gnetif, &ipaddr.u_addr.ip4, &netmask.u_addr.ip4, &gw.u_addr.ip4);

		  IP4_ADDR(&ipaddr, IP_ADDR0 ,IP_ADDR1 , IP_ADDR2 , IP_ADDR3 );
		  IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
		  IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
		  netif_set_addr(&gnetif, &ipaddr, &netmask, &gw);



#ifdef SERIAL_DEBUG
          printf("\n    DHCP timeout    \n");
          printf("  Static IP address   \n");
		      printf("IP: %d.%d.%d.%d\n",IP_ADDR0,IP_ADDR1,IP_ADDR2,IP_ADDR3);
					printf("NETMASK: %d.%d.%d.%d\n",NETMASK_ADDR0,NETMASK_ADDR1,NETMASK_ADDR2,NETMASK_ADDR3);
					printf("Gateway: %d.%d.%d.%d\n",GW_ADDR0,GW_ADDR1,GW_ADDR2,GW_ADDR3);
          LED1_ON;
#endif /* SERIAL_DEBUG */
        }
      }
    }
    break;
  default: break;
  }

}
#endif

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
