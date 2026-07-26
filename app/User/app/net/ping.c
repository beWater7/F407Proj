#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/inet_chksum.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/raw.h"
#include "ping.h"


#if 0
#define PING_ID       0xAFAF  // Ping请求的标识符
#define PING_DATA_SIZE 32      // Ping请求的数据大小

static uint16_t ping_seq_num = 0; // Ping序列号

extern void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg);


// 构建ICMP Echo请求包
static void ping_prepare_echo(struct icmp_echo_hdr *iecho, uint16_t len) {
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id = PING_ID;
    iecho->seqno = lwip_htons(ping_seq_num++);

    // 填充数据部分
    for (int i = 0; i < len; i++) {
        ((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = i;
    }

    iecho->chksum = inet_chksum(iecho, len);
}

#if LWIP_SOCKET

// 发送Ping请求
static void __ping_send(int s, ip_addr_t *addr) {
    struct icmp_echo_hdr *iecho;
    struct sockaddr_storage
    struct sockaddr_in to;
    int ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;

    iecho = (struct icmp_echo_hdr *)mem_malloc(ping_size);
    if (!iecho) {
        printf("malloc failed for ping request\n");
        return;
    }

    ping_prepare_echo(iecho, ping_size);
#if LWIP_IPV4
    if(IP_IS_V4(addr))
    {
        to.sin_len = sizeof(to);
        to.sin_family = AF_INET;
        //to.s_addr = addr->addr;
        inet_addr_from_ip4addr(&to.sin_addr, ip_2_ip4(addr));
    }
#endif
    if (sendto(s, iecho, ping_size, 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
        printf("ping send failed\n");
    } else {
        printf("ping sent to %s\n", inet_ntoa(to.sin_addr));
    }

    mem_free(iecho);
}

// 接收Ping回复
static void ping_recv(int s, ip_addr_t *addr) {
    char buf[64];
    struct sockaddr_in from;
    int fromlen = sizeof(from);

    if (recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from, (socklen_t *)&fromlen) < 0) {
        printf("ping recv failed\n");
    } else {
        printf("ping reply from %s\n", inet_ntoa(from.sin_addr));
    }
}

// Ping操作
void ping(const char *target_name) {
    ip_addr_t target_addr;
    int s;
	printf("error 222222222222222222\n");

    // 解析域名或IP地址
    if (ipaddr_aton(target_name, &target_addr) == 0) {
        printf("Invalid IP address\n");
        return;
    }

    s = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (s < 0) {
        printf("Socket creation failed\n");
        return;
    }

    __ping_send(s, &target_addr);
    ping_recv(s, &target_addr);

    lwip_close(s);
}

#else 

/* 需要定义LWIP_RAW */
#if 1

typedef struct {
    struct raw_pcb *pcb;
    ip_addr_t addr;
} ping_timer_arg_t;


static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    struct icmp_echo_hdr *iecho;

    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);
	printf("333333333333333333333333333333\n");

    if (p->tot_len >= (PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr))) {
        iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + PBUF_IP_HLEN);

        if ((iecho->id == PING_ID) && (iecho->seqno == lwip_htons(ping_seq_num - 1))) {
            printf("ping reply from %s\n", ipaddr_ntoa(addr));
            pbuf_free(p);
            return 1; // ICMP echo reply received
        }
    }
	printf("444444444444444444444444444444\n");
    return 0; // Not an ICMP echo reply
}


static void __ping_send(struct raw_pcb *pcb, const ip_addr_t *addr) {
    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    size_t icmp_echo_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;
	err_t retVal = 0;

    p = pbuf_alloc(PBUF_IP, icmp_echo_size, PBUF_RAM);
    if (!p) return;

    iecho = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id = PING_ID;
    iecho->seqno = lwip_htons(ping_seq_num++);

    //iecho->chksum = inet_chksum(iecho, icmp_echo_size);

	/* 开启硬件计算checksum之后chksum数值不对，开启硬件校验CHECKSUM_BY_HARDWARE后不能再赋值chksum字段 */
	//printf("check sum %x  seq:%d\n", iecho->chksum, ping_seq_num - 1);

	retVal = raw_sendto(pcb, p, addr);
    if(ERR_OK != retVal)
    {
		printf("raw_sendto fail err:%d!\n", retVal);
   	}
	
	printf("222222222222222222222222222222222\n");
    pbuf_free(p);
}


static void ping_timer_callback(void *arg) {
    ping_timer_arg_t *ping_arg = (ping_timer_arg_t *)arg;
	printf("111111111111111111111111111111111\n");
    __ping_send(ping_arg->pcb, &ping_arg->addr);
    // 重新设置定时器
    //sys_timeout(1000, ping_timer_callback, ping_arg);
}


void ping(const char *pbyIp) {
	ip_addr_t ip_addr = {0};
    struct raw_pcb *pcb = NULL;
	err_t err = 0;
    pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) return;

    if (!pbyIp) return;

	// 在发送之前设置 TTL
	//pcb->ttl = 64;	// 这里设置 TTL 为 64

	if('0' < pbyIp[0] && pbyIp[0] < '9' )
	{
		ipaddr_aton(pbyIp, &ip_addr);
	}
	else
	{
		err = dns_gethostbyname(pbyIp, &ip_addr, dns_callback, NULL);
		if(ERR_OK != err)
		{
			printf("[%s:%d] dns_gethostbyname err!\n", __FUNCTION__,__LINE__);
		}
	}
    raw_bind(pcb, IP_ADDR_ANY);

    __ping_send(pcb, &ip_addr);

	// 设置定时器
	ping_timer_arg_t *ping_arg = (ping_timer_arg_t *)malloc(sizeof(ping_timer_arg_t));
	if (ping_arg) {
	  ping_arg->pcb = pcb;
	  memcpy(ping_arg->addr, ip_addr, sizeof(ip_addr));
	  sys_timeout(1000, ping_timer_callback, ping_arg);
	}
	else
	{
		printf("malloc failed\n");
	}
	
	raw_recv(pcb, ping_recv, NULL);

    raw_remove(pcb);
}
#endif

#if 0
static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    struct icmp_echo_hdr *iecho;

    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);

    if (p->tot_len >= (PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr))) {
        iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + PBUF_IP_HLEN);

        if ((iecho->id == PING_ID) && (iecho->seqno == lwip_htons(ping_seq_num - 1))) {
            printf("ping reply from %s\n", ipaddr_ntoa(addr));
            pbuf_free(p);
            return 0; // ICMP echo reply received, return 0 to allow further processing
        }
    }
    pbuf_free(p);
    return 0; // Not an ICMP echo reply, free pbuf and return 0
}

static void __ping_send(struct raw_pcb *pcb, const ip_addr_t *addr) {
    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    size_t icmp_echo_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;

    p = pbuf_alloc(PBUF_IP, icmp_echo_size, PBUF_RAM);
    if (!p) return;

    iecho = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id = PING_ID;
    iecho->seqno = lwip_htons(ping_seq_num++);

    iecho->chksum = inet_chksum(iecho, icmp_echo_size);

    raw_sendto(pcb, p, addr);
    pbuf_free(p);
}

static void ping_timeout(void *arg) {
    struct raw_pcb *pcb = (struct raw_pcb *)arg;
    const ip_addr_t *target_addr = &pcb->remote_ip;

    __ping_send(pcb, target_addr);

    sys_timeout(1000, ping_timeout, pcb); // Re-register timeout for next ping
}

void ping(const ip_addr_t *target_addr) {
    struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb)
    {
    	printf("error pcb is NULL\n");
		return;
    }

    raw_bind(pcb, IP_ADDR_ANY);

    raw_recv(pcb, ping_recv, NULL);

    __ping_send(pcb, target_addr);

    sys_timeout(1000, ping_timeout, pcb); // Start periodic ping sending
}
#endif

#endif
#endif
/**
 * PING_DEBUG: Enable debugging for PING.
 */
#ifndef PING_DEBUG
#define PING_DEBUG     LWIP_DBG_ON
#endif

/** ping receive timeout - in milliseconds */
#ifndef PING_RCV_TIMEO
#define PING_RCV_TIMEO 1000
#endif

/** ping delay - in milliseconds */
#ifndef PING_DELAY
#define PING_DELAY     1000
#endif

/** ping identifier - must fit on a u16_t */
#ifndef PING_ID
#define PING_ID        0xAFAF
#endif

/** ping additional data size to include in the packet */
#ifndef PING_DATA_SIZE
#define PING_DATA_SIZE 32
#endif

/** ping result action - no default action */
#ifndef PING_RESULT
#define PING_RESULT(ping_ok)
#endif

/* ping variables */
static const ip_addr_t* ping_target;
static u16_t ping_seq_num;
#ifdef LWIP_DEBUG
static u32_t ping_time;
#endif /* LWIP_DEBUG */
#if !PING_USE_SOCKETS
static struct raw_pcb *ping_pcb;
#endif /* PING_USE_SOCKETS */

/** Prepare a echo ICMP request */
static void
ping_prepare_echo( struct icmp_echo_hdr *iecho, u16_t len)
{
  size_t i;
  size_t data_len = len - sizeof(struct icmp_echo_hdr);

  ICMPH_TYPE_SET(iecho, ICMP_ECHO);
  ICMPH_CODE_SET(iecho, 0);
  iecho->chksum = 0;
  iecho->id     = PING_ID;
  iecho->seqno  = lwip_htons(++ping_seq_num);

  /* fill the additional data buffer with some data */
  for(i = 0; i < data_len; i++) {
    ((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)i;
  }

#ifndef CHECKSUM_BY_HARDWARE
  iecho->chksum = inet_chksum(iecho, len);
#endif
  //printf("iecho->chksum:%04x \n", iecho->chksum);
}


#if PING_USE_SOCKETS

/* Ping using the socket ip */
static err_t
__ping_send(int s, const ip_addr_t *addr)
{
  int err;
  struct icmp_echo_hdr *iecho;
  struct sockaddr_storage to;
  size_t ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;
  LWIP_ASSERT("ping_size is too big", ping_size <= 0xffff);

#if LWIP_IPV6
  if(IP_IS_V6(addr) && !ip6_addr_isipv4mappedipv6(ip_2_ip6(addr))) {
    /* todo: support ICMP6 echo */
    return ERR_VAL;
  }
#endif /* LWIP_IPV6 */

  iecho = (struct icmp_echo_hdr *)mem_malloc((mem_size_t)ping_size);
  if (!iecho) {
    return ERR_MEM;
  }

  ping_prepare_echo(iecho, (u16_t)ping_size);

#if LWIP_IPV4
  if(IP_IS_V4(addr)) {
    struct sockaddr_in *to4 = (struct sockaddr_in*)&to;
    to4->sin_len    = sizeof(*to4);
    to4->sin_family = AF_INET;
    inet_addr_from_ip4addr(&to4->sin_addr, ip_2_ip4(addr));
  }
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
  if(IP_IS_V6(addr)) {
    struct sockaddr_in6 *to6 = (struct sockaddr_in6*)&to;
    to6->sin6_len    = sizeof(*to6);
    to6->sin6_family = AF_INET6;
    inet6_addr_from_ip6addr(&to6->sin6_addr, ip_2_ip6(addr));
  }
#endif /* LWIP_IPV6 */

  err = lwip_sendto(s, iecho, ping_size, 0, (struct sockaddr*)&to, sizeof(to));

  mem_free(iecho);

  return (err ? ERR_OK : ERR_VAL);
}

static void
ping_recv(int s)
{
  char buf[64];
  int len;
  struct sockaddr_storage from;
  int fromlen = sizeof(from);

  while((len = lwip_recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&from, (socklen_t*)&fromlen)) > 0) {
    if (len >= (int)(sizeof(struct ip_hdr)+sizeof(struct icmp_echo_hdr))) {
      ip_addr_t fromaddr;
      memset(&fromaddr, 0, sizeof(fromaddr));

#if LWIP_IPV4
      if(from.ss_family == AF_INET) {
        struct sockaddr_in *from4 = (struct sockaddr_in*)&from;
        inet_addr_to_ip4addr(ip_2_ip4(&fromaddr), &from4->sin_addr);
        IP_SET_TYPE_VAL(fromaddr, IPADDR_TYPE_V4);
      }
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
      if(from.ss_family == AF_INET6) {
        struct sockaddr_in6 *from6 = (struct sockaddr_in6*)&from;
        inet6_addr_to_ip6addr(ip_2_ip6(&fromaddr), &from6->sin6_addr);
        IP_SET_TYPE_VAL(fromaddr, IPADDR_TYPE_V6);
      }
#endif /* LWIP_IPV6 */

      LWIP_DEBUGF( PING_DEBUG, ("ping: recv "));
      ip_addr_debug_print_val(PING_DEBUG, fromaddr);
      LWIP_DEBUGF( PING_DEBUG, (" %"U32_F" ms\n", (sys_now() - ping_time)));

      /* todo: support ICMP6 echo */
#if LWIP_IPV4
      if (IP_IS_V4_VAL(fromaddr)) {
        struct ip_hdr *iphdr;
        struct icmp_echo_hdr *iecho;

        iphdr = (struct ip_hdr *)buf;
        iecho = (struct icmp_echo_hdr *)(buf + (IPH_HL(iphdr) * 4));
        if ((iecho->id == PING_ID) && (iecho->seqno == lwip_htons(ping_seq_num))) {
          /* do some ping result processing */
          PING_RESULT((ICMPH_TYPE(iecho) == ICMP_ER));
          return;
        } else {
          LWIP_DEBUGF( PING_DEBUG, ("ping: drop\n"));
        }
      }
#endif /* LWIP_IPV4 */
    }
    fromlen = sizeof(from);
  }

  if (len == 0) {
    LWIP_DEBUGF( PING_DEBUG, ("ping: recv - %"U32_F" ms - timeout\n", (sys_now()-ping_time)));
  }

  /* do some ping result processing */
  PING_RESULT(0);
}

static void
ping_thread(void *arg)
{
  int s;
  int ret;

#if LWIP_SO_SNDRCVTIMEO_NONSTANDARD
  int timeout = PING_RCV_TIMEO;
#else
  struct timeval timeout;
  timeout.tv_sec = PING_RCV_TIMEO/1000;
  timeout.tv_usec = (PING_RCV_TIMEO%1000)*1000;
#endif
  LWIP_UNUSED_ARG(arg);

#if LWIP_IPV6
  if(IP_IS_V4(ping_target) || ip6_addr_isipv4mappedipv6(ip_2_ip6(ping_target))) {
    s = lwip_socket(AF_INET6, SOCK_RAW, IP_PROTO_ICMP);
  } else {
    s = lwip_socket(AF_INET6, SOCK_RAW, IP6_NEXTH_ICMP6);
  }
#else
  s = lwip_socket(AF_INET,  SOCK_RAW, IP_PROTO_ICMP);
#endif
  if (s < 0) {
    return;
  }

  ret = lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  LWIP_ASSERT("setting receive timeout failed", ret == 0);
  LWIP_UNUSED_ARG(ret);

  while (1) {
    if (__ping_send(s, ping_target) == ERR_OK) {
      LWIP_DEBUGF( PING_DEBUG, ("ping: send "));
      ip_addr_debug_print(PING_DEBUG, ping_target);
      LWIP_DEBUGF( PING_DEBUG, ("\n"));

#ifdef LWIP_DEBUG
      ping_time = sys_now();
#endif /* LWIP_DEBUG */
      ping_recv(s);
    } else {
      LWIP_DEBUGF( PING_DEBUG, ("ping: send "));
      ip_addr_debug_print(PING_DEBUG, ping_target);
      LWIP_DEBUGF( PING_DEBUG, (" - error\n"));
    }
    sys_msleep(PING_DELAY);
  }
}

#else /* PING_USE_SOCKETS */

/* Ping using the raw ip */
static u8_t
ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  struct icmp_echo_hdr *iecho;
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(pcb);
  LWIP_UNUSED_ARG(addr);
  LWIP_ASSERT("p != NULL", p != NULL);

  if ((p->tot_len >= (PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr))) &&
      pbuf_remove_header(p, PBUF_IP_HLEN) == 0) {
    iecho = (struct icmp_echo_hdr *)p->payload;

    if ((iecho->id == PING_ID) && (iecho->seqno == lwip_htons(ping_seq_num))) {
      LWIP_DEBUGF( PING_DEBUG, ("ping: recv "));
      ip_addr_debug_print(PING_DEBUG, addr);
      LWIP_DEBUGF( PING_DEBUG, (" %"U32_F" ms\n", (sys_now()-ping_time)));

      PING_PRINT("ping: recv ");
      IP4_ADDR_PRINT(addr);
      PING_PRINT(" %"U32_F" ms\n", (sys_now()-ping_time));

      /* do some ping result processing */
      PING_RESULT(1);
      pbuf_free(p);
      return 1; /* eat the packet */
    }
    /* not eaten, restore original packet */
    pbuf_add_header(p, PBUF_IP_HLEN);
  }

  return 0; /* don't eat the packet */
}

static void
__ping_send(struct raw_pcb *raw, const ip_addr_t *addr)
{
  struct pbuf *p;
  struct icmp_echo_hdr *iecho;
  size_t ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;

  LWIP_DEBUGF( PING_DEBUG, ("ping: send "));
  ip_addr_debug_print(PING_DEBUG, addr);
  LWIP_DEBUGF( PING_DEBUG, ("\n"));
  LWIP_ASSERT("ping_size <= 0xffff", ping_size <= 0xffff);

  p = pbuf_alloc(PBUF_IP, (u16_t)ping_size, PBUF_RAM);
  if (!p) {
    return;
  }
  if ((p->len == p->tot_len) && (p->next == NULL)) {
    iecho = (struct icmp_echo_hdr *)p->payload;

    ping_prepare_echo(iecho, (u16_t)ping_size);

    raw_sendto(raw, p, addr);
#ifdef LWIP_DEBUG
    ping_time = sys_now();
#endif /* LWIP_DEBUG */
  }
  pbuf_free(p);
}

static void
ping_timeout(void *arg)
{
  struct raw_pcb *pcb = (struct raw_pcb*)arg;

  LWIP_ASSERT("ping_timeout: no pcb given!", pcb != NULL);

  __ping_send(pcb, ping_target);

  sys_timeout(PING_DELAY, ping_timeout, pcb);
}

static void
ping_raw_init(void)
{
  ping_pcb = raw_new(IP_PROTO_ICMP);
  LWIP_ASSERT("ping_pcb != NULL", ping_pcb != NULL);

  raw_recv(ping_pcb, ping_recv, NULL);
  raw_bind(ping_pcb, IP_ADDR_ANY);
#ifdef PING_CIRCLE
  sys_timeout(PING_DELAY, ping_timeout, ping_pcb);
#endif
}

void
ping_send_now(void)
{
  LWIP_ASSERT("ping_pcb != NULL", ping_pcb != NULL);
  __ping_send(ping_pcb, ping_target);
}

#endif /* PING_USE_SOCKETS */

void ping_send(ip_addr_t *addr)
{
    __ping_send(ping_pcb, addr);
    return;
}

void
ping_init(const ip_addr_t* ping_addr)
{
  ping_target = ping_addr;

#if PING_USE_SOCKETS
  sys_thread_new("ping_thread", ping_thread, NULL, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
#else /* PING_USE_SOCKETS */
  ping_raw_init();
#endif /* PING_USE_SOCKETS */
}


