/**
 * @file
 * Ethernet Interface Skeleton
 *
 */

#include "lwip/opt.h"

#if LWIP_NETIF_IFINDEX

#include "lwip/if_api.h"

#endif /* LWIP_NETIF_IFINDEX */

#include "lwip/timeouts.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "netif/ethernet.h"
#include "ethernetif.h"

#include "stm32f4x7_eth.h"
#include "netconf.h"
#include <string.h>

#include "FreeRTOS.h"		  //FreeRTOS使用
#include "task.h"


#ifndef NO_SYS
#define NO_SYS 0
#endif


/* Define those to better describe your network interface. */
#define IFNAME0 'e'
#define IFNAME1 'n'

QueueHandle_t s_xSemaphore;

#define POLL_INTERVAL 100  // 每100毫秒轮询一次
#define NETIF_IN_TASK_STACK_SIZE 512
#define NETIF_IN_TASK_PRIORITY 7

/* ETH开头的为驱动层内容 */

/* Ethernet Rx & Tx DMA Descriptors */
extern ETH_DMADESCTypeDef  DMARxDscrTab[ETH_RXBUFNB], DMATxDscrTab[ETH_TXBUFNB];

/* Ethernet Driver Receive buffers  */
extern uint8_t Rx_Buff[ETH_RXBUFNB][ETH_RX_BUF_SIZE]; 

/* Ethernet Driver Transmit buffers */
extern uint8_t Tx_Buff[ETH_TXBUFNB][ETH_TX_BUF_SIZE]; 

/* Global pointers to track current transmit and receive descriptors */
extern ETH_DMADESCTypeDef  *DMATxDescToSet;
extern ETH_DMADESCTypeDef  *DMARxDescToGet;

/* Global pointer for last received frame infos */
extern ETH_DMA_Rx_Frame_infos *DMA_RX_FRAME_infos;


/**
 * Helper struct to hold private data used to operate your ethernet interface.
 */
struct ethernetif {
  struct eth_addr *ethaddr;
  /* Add whatever per-interface state that is needed here. */
};


// 包装函数，适配 lwIP 的线程入口定义, 适配GCC
static void ethernetif_input_wrapper(void *arg)
{
    ethernetif_input((struct netif *)arg);
}


/**
 * In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwIP network interface structure for this ethernetif
 */
static void
low_level_init(struct netif *netif)
{
  struct ethernetif *ethernetif = netif->state;
  int i = 0;

  /* set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] = 0x02;
  netif->hwaddr[1] = 0x00;
  netif->hwaddr[2] = 0x00;
  netif->hwaddr[3] = 0x00;
  netif->hwaddr[4] = 0x00;
  netif->hwaddr[5] = 0x00;

  /* initialize MAC address in ethernet MAC */ 
  ETH_MACAddressConfig(ETH_MAC_Address0, netif->hwaddr); 


  /* maximum transfer unit */
  netif->mtu = 1500;

  /* device capabilities */
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

#if LWIP_IPV6 && LWIP_IPV6_MLD
  /*
   * For hardware/netifs that implement MAC filtering.
   * All-nodes link-local is handled by default, so we must let the hardware know
   * to allow multicast packets in.
   * Should set mld_mac_filter previously. */
  if (netif->mld_mac_filter != NULL) {
    ip6_addr_t ip6_allnodes_ll;
    ip6_addr_set_allnodes_linklocal(&ip6_allnodes_ll);
    netif->mld_mac_filter(netif, &ip6_allnodes_ll, NETIF_ADD_MAC_FILTER);
  }
#endif /* LWIP_IPV6 && LWIP_IPV6_MLD */

  /* Do whatever else is needed to initialize your hardware. */

	/* Initialize Tx Descriptors list: Chain Mode */
	ETH_DMATxDescChainInit(DMATxDscrTab, &Tx_Buff[0][0], ETH_TXBUFNB);
	/* Initialize Rx Descriptors list: Chain Mode  */
	ETH_DMARxDescChainInit(DMARxDscrTab, &Rx_Buff[0][0], ETH_RXBUFNB);
	
#ifdef CHECKSUM_BY_HARDWARE
	/* Enable the TCP, UDP and ICMP checksum insertion for the Tx frames */
	for(i=0; i<ETH_TXBUFNB; i++)
	{
	  ETH_DMATxDescChecksumInsertionConfig(&DMATxDscrTab[i], ETH_DMATxDesc_ChecksumTCPUDPICMPFull);
	}
#endif
	  /* USER CODE BEGIN PHY_PRE_CONFIG */

     s_xSemaphore = xSemaphoreCreateCounting(40,0);

     /* create the task that handles the ETH_MAC */
     sys_thread_new("ETHIN",
                     ethernetif_input_wrapper,  /* 任务入口函数 */
                     netif,           /* 任务入口函数参数 */
                     NETIF_IN_TASK_STACK_SIZE,/* 任务栈大小 */
                     NETIF_IN_TASK_PRIORITY); /* 任务的优先级 */
	 
	/* Note: TCP, UDP, ICMP checksum checking for received frame are enabled in DMA config */
	
	/* Enable MAC and DMA transmission and reception */
	ETH_Start();

}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become availale since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  err_t errval;
  struct pbuf *q;
  u8 *buffer =  (u8 *)(DMATxDescToSet->Buffer1Addr);
  __IO ETH_DMADESCTypeDef *DmaTxDesc;
  uint16_t framelength = 0;
  uint32_t bufferoffset = 0;
  uint32_t byteslefttocopy = 0;
  uint32_t payloadoffset = 0;

  DmaTxDesc = DMATxDescToSet;
  bufferoffset = 0;

  /* copy frame from pbufs to driver buffers */
  for(q = p; q != NULL; q = q->next)
    {
      /* Is this buffer available? If not, goto error */
      if((DmaTxDesc->Status & ETH_DMATxDesc_OWN) != (u32)RESET)
      {
        errval = ERR_BUF;
        goto error;
      }

      /* Get bytes in current lwIP buffer */
      byteslefttocopy = q->len;
      payloadoffset = 0;

      /* Check if the length of data to copy is bigger than Tx buffer size*/
      while( (byteslefttocopy + bufferoffset) > ETH_TX_BUF_SIZE )
      {
        /* Copy data to Tx buffer*/
        memcpy( (u8_t*)((u8_t*)buffer + bufferoffset), (u8_t*)((u8_t*)q->payload + payloadoffset), (ETH_TX_BUF_SIZE - bufferoffset) );

        /* Point to next descriptor */
        DmaTxDesc = (ETH_DMADESCTypeDef *)(DmaTxDesc->Buffer2NextDescAddr);

        /* Check if the buffer is available */
        if((DmaTxDesc->Status & ETH_DMATxDesc_OWN) != (u32)RESET)
        {
          errval = ERR_USE;
          goto error;
        }

        buffer = (u8 *)(DmaTxDesc->Buffer1Addr);

        byteslefttocopy = byteslefttocopy - (ETH_TX_BUF_SIZE - bufferoffset);
        payloadoffset = payloadoffset + (ETH_TX_BUF_SIZE - bufferoffset);
        framelength = framelength + (ETH_TX_BUF_SIZE - bufferoffset);
        bufferoffset = 0;
      }

      /* Copy the remaining bytes */
      memcpy( (u8_t*)((u8_t*)buffer + bufferoffset), (u8_t*)((u8_t*)q->payload + payloadoffset), byteslefttocopy );
      bufferoffset = bufferoffset + byteslefttocopy;
      framelength = framelength + byteslefttocopy;
    }
  
  /* Note: padding and CRC for transmitted frame 
     are automatically inserted by DMA */

  /* Prepare transmit descriptors to give to DMA*/ 
  ETH_Prepare_Transmit_Descriptors(framelength);

  errval = ERR_OK;

error:
  
  /* When Transmit Underflow flag is set, clear it and issue a Transmit Poll Demand to resume transmission */
  if ((ETH->DMASR & ETH_DMASR_TUS) != (uint32_t)RESET)
  {
    /* Clear TUS ETHERNET DMA flag */
    ETH->DMASR = ETH_DMASR_TUS;

    /* Resume DMA transmission*/
    ETH->DMATPDR = 0;
  }
  return errval;
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
   */
static struct pbuf * low_level_input(struct netif *netif)
{
  struct pbuf *p, *q;
  uint32_t len;
  FrameTypeDef frame;
  u8 *buffer;
  __IO ETH_DMADESCTypeDef *DMARxDesc;
  uint32_t bufferoffset = 0;
  uint32_t payloadoffset = 0;
  uint32_t byteslefttocopy = 0;
  uint32_t i=0;  
  
  /* get received frame */
  frame = ETH_Get_Received_Frame();
  
  /* Obtain the size of the packet and put it into the "len" variable. */
  len = frame.length;
  buffer = (u8 *)frame.buffer;
  
  /* We allocate a pbuf chain of pbufs from the Lwip buffer pool */
  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  
  if (p != NULL)
  {
    DMARxDesc = frame.descriptor;
    bufferoffset = 0;
    for(q = p; q != NULL; q = q->next)
    {
      byteslefttocopy = q->len;
      payloadoffset = 0;
      
      /* Check if the length of bytes to copy in current pbuf is bigger than Rx buffer size*/
      while( (byteslefttocopy + bufferoffset) > ETH_RX_BUF_SIZE )
      {
        /* Copy data to pbuf*/
        memcpy( (u8_t*)((u8_t*)q->payload + payloadoffset), (u8_t*)((u8_t*)buffer + bufferoffset), (ETH_RX_BUF_SIZE - bufferoffset));
        
        /* Point to next descriptor */
        DMARxDesc = (ETH_DMADESCTypeDef *)(DMARxDesc->Buffer2NextDescAddr);
        buffer = (unsigned char *)(DMARxDesc->Buffer1Addr);
        
        byteslefttocopy = byteslefttocopy - (ETH_RX_BUF_SIZE - bufferoffset);
        payloadoffset = payloadoffset + (ETH_RX_BUF_SIZE - bufferoffset);
        bufferoffset = 0;
      }
      /* Copy remaining data in pbuf */
      memcpy( (u8_t*)((u8_t*)q->payload + payloadoffset), (u8_t*)((u8_t*)buffer + bufferoffset), byteslefttocopy);
      bufferoffset = bufferoffset + byteslefttocopy;
    }
  }
  
  /* Release descriptors to DMA */
  DMARxDesc =frame.descriptor;

  /* Set Own bit in Rx descriptors: gives the buffers back to DMA */
  for (i=0; i<DMA_RX_FRAME_infos->Seg_Count; i++)
  {  
    DMARxDesc->Status = ETH_DMARxDesc_OWN;
    DMARxDesc = (ETH_DMADESCTypeDef *)(DMARxDesc->Buffer2NextDescAddr);
  }
  
  /* Clear Segment_Count */
  DMA_RX_FRAME_infos->Seg_Count =0;
  
  /* When Rx Buffer unavailable flag is set: clear it and resume reception */
  if ((ETH->DMASR & ETH_DMASR_RBUS) != (u32)RESET)  
  {
    /* Clear RBUS ETHERNET DMA flag */
    ETH->DMASR = ETH_DMASR_RBUS;
    /* Resume DMA reception */
    ETH->DMARPDR = 0;
  }
  return p;
}


/**
 * This function should be called when a packet is ready to be read from the interface. It uses the function netif->input() to forward the packet to the lwIP stack.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
void ethernetif_input(struct netif *netif)
{
  struct ethernetif *ethernetif;
  struct pbuf *p;

  ethernetif = netif->state;
#if NO_SYS
  /* move received packet into a new pbuf */
  p = low_level_input(netif);

  /* if no packet could be read, silently ignore this */
  if (p == NULL) return;

  /* entry point to the LwIP stack */
  if (netif->input(p, netif) != ERR_OK) {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
    pbuf_free(p);
    p = NULL;
  }

  LINK_STATS_INC(link.recv);
#else

	while (1)
	{
		
		if (xSemaphoreTake( s_xSemaphore, portMAX_DELAY ) == pdTRUE)
		{
			/* DMA 描述符操作短暂关临界区即可；禁止在临界区里调 tcpip_input */
			taskENTER_CRITICAL();
			p = low_level_input(netif);
			taskEXIT_CRITICAL();
			/* points to packet payload, which starts with an Ethernet header */
			if (p != NULL)
			{
				/* tcpip_input 会投递邮箱，绝不能放在 taskENTER_CRITICAL 内 */
				if (netif->input(p, netif) != ERR_OK)
				{
				LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
					pbuf_free(p);
					p = NULL;
				}
			}
			else
			{
				/* 常见原因：PBUF_POOL 耗尽或内存区不可用 */
				static uint32_t drop;
				if ((++drop % 32U) == 1U) {
					printf("ETH RX drop: pbuf_alloc failed (cnt=%lu)\n", (unsigned long)drop);
				}
			}
		}
	}
#endif
}

/**
 * This function should be called periodically. It could be used to check the link status, process received packets, etc.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
static void
ethernetif_poll(struct netif *netif)
{
  /* TODO: Implement code to poll your hardware for incoming packets */

  /* For example, you might call ethernetif_input(netif) here */

  sys_timeout(POLL_INTERVAL, (sys_timeout_handler)ethernetif_poll, netif);
}

/**
 * Initializes the ethernet interface.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif)
{
  struct ethernetif *ethernetif;

  LWIP_ASSERT("netif != NULL", (netif != NULL));

  ethernetif = (struct ethernetif *)mem_malloc(sizeof(struct ethernetif));
  if (ethernetif == NULL) {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: out of memory\n"));
    return ERR_MEM;
  }

  netif->state = ethernetif;
  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /* We directly use etharp_output() here to save a function call. */
  netif->output = etharp_output;
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */
  netif->linkoutput = low_level_output;

  ethernetif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

  /* initialize the hardware */
  low_level_init(netif);

  /* set a polling interval */
  //sys_timeout(POLL_INTERVAL, (sys_timeout_handler)ethernetif_poll, netif);

  return ERR_OK;
}

































