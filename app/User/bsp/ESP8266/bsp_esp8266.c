/**
  ******************************************************************************
  * @file    esp8266.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   esp8266���ܺ���
  ******************************************************************************
  * @attention
  *
  * ʵ��ƽ̨:Ұ��  STM32 F407 ������
  * ��̳    :http://www.firebbs.cn
  * �Ա�    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */
#include "bsp_esp8266.h"
#include "bsp_esp8266_test.h"
#include "common.h"
#include <stdio.h>  
#include <string.h>  
#include <stdbool.h>
#include <stdarg.h>
#include "os_mutex.h"
#include "os_debug.h"
#include "bsp_systick.h"
#include "core_delay.h"
#include "safe_utils.h"
#include "flash_manage.h"
#include "crc.h"
#include "devConfig.h"

/* USART3 优先级必须低于 SysTick/PendSV，且不要在调度器起来后改 NVIC 分组 */

void ESP8266_Usart_Printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;
    int i;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
    {
        return;
    }
    if (n >= (int)sizeof(buf))
    {
        n = (int)sizeof(buf) - 1;
    }
    for (i = 0; i < n; i++)
    {
        while (USART_GetFlagStatus(macESP8266_USARTx, USART_FLAG_TXE) == RESET)
        {
        }
        USART_SendData(macESP8266_USARTx, (uint16_t)buf[i]);
    }
}


static void                   ESP8266_GPIO_Config                 ( void );
static void                   ESP8266_USART_Config                ( void );
static void                   ESP8266_USART_NVIC_Configuration    ( void );


struct  STRUCT_USARTx_Fram strEsp8266_Fram_Record = { 0 };
struct  STRUCT_USARTx_Fram strUSART_Fram_Record = { 0 };
static char gs_esp8266_ssid[ESP8266_SSID_MAX + 1];
static char gs_esp8266_psk[ESP8266_PSK_MAX + 1];
static volatile wifi_apply_state_t gs_wifi_apply = WIFI_APPLY_IDLE;
static char gs_wifi_apply_err[48];

#define WIFI_CRED_MAGIC  0x57494631u /* 'WIF1' */

typedef struct {
    uint32_t magic;
    uint32_t crc;
    char ssid[ESP8266_SSID_MAX + 1];
    char psk[ESP8266_PSK_MAX + 1];
} wifi_cred_nv_t;

char *getEsp8266Ssid(void)
{
    return gs_esp8266_ssid;
}

char *getEsp8266Psk(void)
{
    return gs_esp8266_psk;
}

static void esp8266_copy_cred(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    if (!dst || dstsz == 0) {
        return;
    }
    memset(dst, 0, dstsz);
    if (!src) {
        return;
    }
    n = strlen(src);
    if (n >= dstsz) {
        n = dstsz - 1U;
    }
    memcpy(dst, src, n);
}

void setEsp8266Ssid(char *ssid)
{
    CUSTOM_ASSERT(!ssid, return);
    esp8266_copy_cred(gs_esp8266_ssid, sizeof(gs_esp8266_ssid), ssid);
}

void setEsp8266Psk(char *psk)
{
    CUSTOM_ASSERT(!psk, return);
    esp8266_copy_cred(gs_esp8266_psk, sizeof(gs_esp8266_psk), psk);
}

static const char *wifi_apply_name(wifi_apply_state_t st)
{
    switch (st) {
    case WIFI_APPLY_PENDING:     return "pending";
    case WIFI_APPLY_CONNECTING:  return "connecting";
    case WIFI_APPLY_OK:          return "ok";
    case WIFI_APPLY_FAIL:        return "fail";
    case WIFI_APPLY_IDLE:
    default:                     return "idle";
    }
}

const char *ESP8266_WifiApplyStateStr(void)
{
    return wifi_apply_name(gs_wifi_apply);
}

wifi_apply_state_t ESP8266_WifiApplyState(void)
{
    return gs_wifi_apply;
}

const char *ESP8266_WifiApplyError(void)
{
    return gs_wifi_apply_err;
}

void ESP8266_WifiApplySet(wifi_apply_state_t st, const char *err)
{
    gs_wifi_apply = st;
    memset(gs_wifi_apply_err, 0, sizeof(gs_wifi_apply_err));
    if (err) {
        esp8266_copy_cred(gs_wifi_apply_err, sizeof(gs_wifi_apply_err), err);
    }
}

int ESP8266_WifiCredSave(void)
{
    if (setWifiStaParam(gs_esp8266_ssid, gs_esp8266_psk) != RET_OK) {
        os_debug("wifi cred save to param fail\r\n");
        return -1;
    }
    /* 只置位，由 devParamMng_task 写 Flash。SaveNow 会在 httpd/tcpip 里擦扇区，ping/网页会一起断 */
    devParamSave();
    return 0;
}

static int wifi_cred_load_legacy(void)
{
    wifi_cred_nv_t nv;
    uint32_t crc;

    memset(&nv, 0, sizeof(nv));
    if (SPI_FLASH_READ(PART_CUSTOM, 0, (uint8_t *)&nv, sizeof(nv)) != 0) {
        return -1;
    }
    if (nv.magic != WIFI_CRED_MAGIC) {
        return -1;
    }
    crc = crc32_checksum((const unsigned char *)nv.ssid,
                         sizeof(nv.ssid) + sizeof(nv.psk));
    if (crc != nv.crc) {
        os_debug("wifi cred crc mismatch\r\n");
        return -1;
    }
    nv.ssid[ESP8266_SSID_MAX] = '\0';
    nv.psk[ESP8266_PSK_MAX] = '\0';
    if (nv.ssid[0] == '\0') {
        return -1;
    }
    setEsp8266Ssid(nv.ssid);
    setEsp8266Psk(nv.psk);
    return 0;
}

int ESP8266_WifiCredLoad(void)
{
    char ssid[WIFI_SSID_MAX + 1];
    char psk[WIFI_PSK_MAX + 1];

    memset(ssid, 0, sizeof(ssid));
    memset(psk, 0, sizeof(psk));
    if (getWifiStaParam(ssid, sizeof(ssid), psk, sizeof(psk)) == RET_OK && ssid[0] != '\0') {
        setEsp8266Ssid(ssid);
        setEsp8266Psk(psk);
        os_printf("wifi cred loaded from param, ssid_len=%u\r\n", (unsigned)strlen(ssid));
        return 0;
    }
    if (wifi_cred_load_legacy() == 0) {
        (void)ESP8266_WifiCredSave();
        os_printf("wifi cred migrated to param, ssid_len=%u\r\n",
                  (unsigned)strlen(gs_esp8266_ssid));
        return 0;
    }
    return -1;
}

/**
  * @brief  ESP8266��ʼ������
  * @param  ��
  * @retval ��
  */
void ESP8266_Init ( void )
{
	ESP8266_GPIO_Config (); 
	
	ESP8266_USART_Config (); 

	macESP8266_RST_HIGH_LEVEL();

	macESP8266_CH_DISABLE();

	setEsp8266Ssid(macUser_ESP8266_ApSsid);

	setEsp8266Psk(macUser_ESP8266_ApPwd);

	if (ESP8266_WifiCredLoad() == 0) {
		os_printf("ESP8266: cred ok, auto-connect after recv task starts\r\n");
	} else {
		os_printf("ESP8266: no saved cred, use default ssid=%s\r\n",
		          getEsp8266Ssid());
	}
	/* 只把凭据装进 RAM、CH_PD 仍为低。真正 AT+CWJAP 在 recv 任务里异步做，
	 * 避免在 app_main 里忙等把 httpd 卡死。 */
	if (getEsp8266Ssid() && getEsp8266Ssid()[0] != '\0') {
		ESP8266_RequestWifiReconfig();
	} else {
		ESP8266_WifiApplySet(WIFI_APPLY_IDLE, NULL);
	}
}


/**
  * @brief  ��ʼ��ESP8266�õ���GPIO����
  * @param  ��
  * @retval ��
  */
static void ESP8266_GPIO_Config ( void )
{
	/*����һ��GPIO_InitTypeDef���͵Ľṹ��*/
	GPIO_InitTypeDef GPIO_InitStructure;

	/* ���� CH_PD ����*/
	macESP8266_CH_PD_APBxClock_FUN ( macESP8266_CH_PD_CLK, ENABLE ); 
								   
	GPIO_InitStructure.GPIO_Pin = macESP8266_CH_PD_PIN;	

	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT; 

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; 

	GPIO_Init ( macESP8266_CH_PD_PORT, & GPIO_InitStructure );	 

	/* ���� RST ����*/
	macESP8266_RST_APBxClock_FUN ( macESP8266_RST_CLK, ENABLE ); 
			   
	GPIO_InitStructure.GPIO_Pin = macESP8266_RST_PIN;	

	GPIO_Init ( macESP8266_RST_PORT, & GPIO_InitStructure );	 

}


/**
  * @brief  ��ʼ��ESP8266�õ��� USART
  * @param  ��
  * @retval ��
  */
static void ESP8266_USART_Config ( void )
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	
	
	/* config USART clock */
	macESP8266_USART_APBxClock_FUN ( macESP8266_USART_CLK, ENABLE );
	macESP8266_USART_GPIO_APBxClock_FUN ( macESP8266_USART_GPIO_CLK, ENABLE );
	
	/* USART GPIO config */
	/* Configure USART Tx as alternate function push-pull */
	GPIO_InitStructure.GPIO_Pin =  macESP8266_USART_TX_PIN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(macESP8266_USART_TX_PORT, &GPIO_InitStructure);  
  
	/* Configure USART Rx as input floating */
	GPIO_InitStructure.GPIO_Pin = macESP8266_USART_RX_PIN;
	GPIO_Init(macESP8266_USART_RX_PORT, &GPIO_InitStructure);
  
	/* ���� PXx �� USARTx_Tx*/
	GPIO_PinAFConfig(macESP8266_USART_TX_PORT,macESP8266_USART_TX_SOURCE, macESP8266_USART_TX_AF);

	/*  ���� PXx �� USARTx__Rx*/
	GPIO_PinAFConfig(macESP8266_USART_RX_PORT,macESP8266_USART_RX_SOURCE,macESP8266_USART_RX_AF);

	/* USART1 mode config */
	USART_InitStructure.USART_BaudRate = macESP8266_USART_BAUD_RATE;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No ;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(macESP8266_USARTx, &USART_InitStructure);

	/* 先配好 NVIC，RX 中断等 CH_PD 拉高再开，避免模组 TX 浮空/噪声打满 ISR */
	USART_ITConfig(macESP8266_USARTx, USART_IT_RXNE, DISABLE);
	USART_ITConfig(macESP8266_USARTx, USART_IT_IDLE, DISABLE);

	ESP8266_USART_NVIC_Configuration();

	USART_Cmd(macESP8266_USARTx, ENABLE);
}


/**
  * @brief  ���� ESP8266 USART �� NVIC �ж�
  * @param  ��
  * @retval ��
  */
static void ESP8266_USART_NVIC_Configuration ( void )
{
	NVIC_InitTypeDef NVIC_InitStructure;

	/* 不要在这里改 PriorityGroup：Debug USART 已设 Group_2，调度后再改会打乱 SysTick/ETH */
	NVIC_InitStructure.NVIC_IRQChannel = macESP8266_USART_IRQ;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

void ESP8266_USART_RxIrqCtrl(int enable)
{
	if (enable) {
		(void)macESP8266_USARTx->SR;
		(void)macESP8266_USARTx->DR;
		USART_ITConfig(macESP8266_USARTx, USART_IT_RXNE, ENABLE);
		USART_ITConfig(macESP8266_USARTx, USART_IT_IDLE, ENABLE);
	} else {
		USART_ITConfig(macESP8266_USARTx, USART_IT_RXNE, DISABLE);
		USART_ITConfig(macESP8266_USARTx, USART_IT_IDLE, DISABLE);
		(void)macESP8266_USARTx->SR;
		(void)macESP8266_USARTx->DR;
	}
}

/*
 * USART3 RX：启动文件里 USART3_IRQHandler 是弱符号 Default_Handler。
 * CH_PD 拉高后模组会立刻吐启动日志，没有本 ISR 就会卡死在 enable ESP8266 之后。
 * 一次读 SR+DR，同时清 RXNE/IDLE/ORE，避免标志清不掉造成中断风暴饿死 ETH 轮询。
 */
void macESP8266_USART_INT_FUN(void)
{
	uint32_t sr = macESP8266_USARTx->SR;
	uint8_t ucCh = (uint8_t)macESP8266_USARTx->DR;

	if (sr & USART_FLAG_RXNE) {
		if (strEsp8266_Fram_Record.InfBit.FramLength < (RX_BUF_MAX_LEN - 1U)) {
			strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength++] = (char)ucCh;
		}
	}
	if (sr & USART_FLAG_IDLE) {
		strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
	}
}


/*
 * ��������ESP8266_Rst
 * ����  ������WF-ESP8266ģ��
 * ����  ����
 * ����  : ��
 * ����  ���� ESP8266_AT_Test ����
 */
void ESP8266_Rst ( void )
{
	#if 0
	 ESP8266_Cmd ( "AT+RST", "OK", "ready", 2500 );   	
	
	#else
	 macESP8266_RST_LOW_LEVEL();
	 Delay_ms ( 500 ); 
	 macESP8266_RST_HIGH_LEVEL();
	 
	#endif

}

bool ESP8266_DHCP_CUR ( )
{
	char cCmd [40] = {0};

	sprintf ( cCmd, "AT+CWDHCP=1,1");
	
	return ESP8266_Cmd ( cCmd, "OK", NULL, 500 );
	
}


/*
 * ��������ESP8266_Cmd
 * ����  ����WF-ESP8266ģ�鷢��ATָ��
 * ����  ��cmd�������͵�ָ��
 *         reply1��reply2���ڴ�����Ӧ��ΪNULL��������Ӧ������Ϊ���߼���ϵ
 *         waittime���ȴ���Ӧ��ʱ��
 * ����  : 1��ָ��ͳɹ�
 *         0��ָ���ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_Cmd ( char * cmd, char * reply1, char * reply2, u32 waittime )
{    
	strEsp8266_Fram_Record .InfBit .FramLength = 0;               //���¿�ʼ�����µ����ݰ�
	/* send cmd to esp8266 */
	macESP8266_Usart ( "%s\r\n", cmd );

	if ( ( reply1 == 0 ) && ( reply2 == 0 ) )                      //����Ҫ��������
		return true;
	
	Delay_ms ( waittime );                 //��ʱ
	
	/* resp from esp8266 */
	if (strEsp8266_Fram_Record.InfBit.FramLength >= RX_BUF_MAX_LEN) {
		strEsp8266_Fram_Record.InfBit.FramLength = RX_BUF_MAX_LEN - 1U;
	}
	strEsp8266_Fram_Record .Data_RX_BUF [ strEsp8266_Fram_Record .InfBit .FramLength ]  = '\0';

	macPC_Usart ( "%s", strEsp8266_Fram_Record .Data_RX_BUF );

	if ( ( reply1 != 0 ) && ( reply2 != 0 ) )
		return ( ( bool ) strstr ( strEsp8266_Fram_Record .Data_RX_BUF, reply1 ) || 
						 ( bool ) strstr ( strEsp8266_Fram_Record .Data_RX_BUF, reply2 ) ); 
 	
	else if ( reply1 != 0 )
		return ( ( bool ) strstr ( strEsp8266_Fram_Record .Data_RX_BUF, reply1 ) );
	
	else
		return ( ( bool ) strstr ( strEsp8266_Fram_Record .Data_RX_BUF, reply2 ) );
}


bool ESP8266_get_cmd_reply ( char * cmd, char * reply1, uint32 len, u32 waittime )
{
	/* send cmd to esp8266 */
	macESP8266_Usart ( "%s\r\n", cmd );

	if (( reply1 == 0 ))                      //
		return true;

	Delay_ms ( waittime );                 //
	printf("1len :%d \r\n", strEsp8266_Fram_Record.InfBit.FramLength);
	/* resp from esp8266 */
	strEsp8266_Fram_Record.Data_RX_BUF[ strEsp8266_Fram_Record .InfBit .FramLength ]  = '\0';

	//macPC_Usart ( "%s", strEsp8266_Fram_Record.Data_RX_BUF );

	if(len < strEsp8266_Fram_Record.InfBit.FramLength)
	{
		return false;
	}
	printf("2len :%d \r\n", strEsp8266_Fram_Record.InfBit.FramLength);
	memcpy(reply1, strEsp8266_Fram_Record.Data_RX_BUF, strEsp8266_Fram_Record .InfBit .FramLength);

	strEsp8266_Fram_Record .InfBit .FramLength = 0;            
	strEsp8266_Fram_Record.Data_RX_BUF[ strEsp8266_Fram_Record .InfBit .FramLength ]  = '\0';
	return true;
}


/*
 * ��������ESP8266_AT_Test
 * ����  ����WF-ESP8266ģ�����AT��������
 * ����  ����
 * ����  : ��
 * ����  �����ⲿ����
 */
//void ESP8266_AT_Test ( void )
//{
//	macESP8266_RST_HIGH_LEVEL();
//	
//	Delay_ms ( 1000 ); 
//	
//	while ( ! ESP8266_Cmd ( "AT", "OK", NULL, 200 ) ) ESP8266_Rst ();  	

//}
bool ESP8266_AT_Test ( void )
{
	char count=0;

	macESP8266_RST_HIGH_LEVEL();	
  	printf("\r\n111AT����.....\r\n");
	Delay_ms ( 2000 );
	//os_sleep_ms(2000);
	while ( count < 10 )
	{
		printf("\r\nAT���Դ��� %d......\r\n", count);
		if( ESP8266_Cmd ( "AT", "OK", NULL, 500 ) )
		{
			printf("\r\nAT���������ɹ� %d......\r\n", count);
			return 1;
		}
		ESP8266_Rst();
		++ count;
	}
	return 0;
}


/*
 * ��������ESP8266_Net_Mode_Choose
 * ����  ��ѡ��WF-ESP8266ģ��Ĺ���ģʽ
 * ����  ��enumMode������ģʽ
 * ����  : 1��ѡ��ɹ�
 *         0��ѡ��ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_Net_Mode_Choose ( ENUM_Net_ModeTypeDef enumMode )
{
	switch ( enumMode )
	{
		case STA:
			return ESP8266_Cmd ( "AT+CWMODE=1", "OK", "no change", 2500 ); 
		
	  case AP:
		  return ESP8266_Cmd ( "AT+CWMODE=2", "OK", "no change", 2500 ); 
		
		case STA_AP:
		  return ESP8266_Cmd ( "AT+CWMODE=3", "OK", "no change", 2500 ); 

	  default:
		  return false;
  }
	
}


/*
 * ��������ESP8266_JoinAP
 * ����  ��WF-ESP8266ģ�������ⲿWiFi
 * ����  ��pSSID��WiFi�����ַ���
 *       ��pPassWord��WiFi�����ַ���
 * ����  : 1�����ӳɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
/* AT 字符串转义：\ -> \\ , " -> \" */
static int esp8266_at_escape(char *dst, size_t dstsz, const char *src)
{
    size_t o = 0;

    if (!dst || dstsz == 0 || !src) {
        return -1;
    }
    while (*src) {
        if (*src == '\\' || *src == '"') {
            if (o + 2 >= dstsz) {
                return -1;
            }
            dst[o++] = '\\';
            dst[o++] = *src++;
        } else {
            if (o + 1 >= dstsz) {
                return -1;
            }
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
    return 0;
}

bool ESP8266_JoinAP ( char * pSSID, char * pPassWord )
{
	char cCmd [160] = {0};
	char ssid_esc[ESP8266_SSID_MAX * 2 + 1];
	char psk_esc[ESP8266_PSK_MAX * 2 + 1];

	CUSTOM_ASSERT(!pSSID || !pPassWord, return false);
	if (esp8266_at_escape(ssid_esc, sizeof(ssid_esc), pSSID) != 0) {
		return false;
	}
	if (esp8266_at_escape(psk_esc, sizeof(psk_esc), pPassWord) != 0) {
		return false;
	}
	if (snprintf_s(cCmd, sizeof(cCmd), sizeof(cCmd) - 1,
	               "AT+CWJAP=\"%s\",\"%s\"", ssid_esc, psk_esc) < 0) {
		return false;
	}

	return ESP8266_Cmd ( cCmd, "WIFI GOT IP", "OK", 15000 );

}

bool ESP8266_leave_AP ( void )
{
	char cCmd [120] = {0};

	// 构造断开连接的 AT 指令
	sprintf(cCmd, "AT+CWQAP");

	return ESP8266_Cmd(cCmd, "OK", NULL, 5000);

}


/*
 * ��������ESP8266_BuildAP
 * ����  ��WF-ESP8266ģ�鴴��WiFi�ȵ�
 * ����  ��pSSID��WiFi�����ַ���
 *       ��pPassWord��WiFi�����ַ���
 *       ��enunPsdMode��WiFi���ܷ�ʽ�����ַ���
 * ����  : 1�������ɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_BuildAP ( char * pSSID, char * pPassWord, ENUM_AP_PsdMode_TypeDef enunPsdMode )
{
	char cCmd [120] = {0};

	sprintf ( cCmd, "AT+CWSAP=\"%s\",\"%s\",1,%d", pSSID, pPassWord, enunPsdMode );

	return ESP8266_Cmd ( cCmd, "OK", 0, 1000 );
}


/*
 * ��������ESP8266_Enable_MultipleId
 * ����  ��WF-ESP8266ģ������������
 * ����  ��enumEnUnvarnishTx�������Ƿ������
 * ����  : 1�����óɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_Enable_MultipleId ( FunctionalState enumEnUnvarnishTx )
{
	char cStr [20] = {0};

	sprintf ( cStr, "AT+CIPMUX=%d", ( enumEnUnvarnishTx ? 1 : 0 ) );

	return ESP8266_Cmd ( cStr, "OK", 0, 500 );

}


/*
 * ��������ESP8266_Link_Server
 * ����  ��WF-ESP8266ģ�������ⲿ������
 * ����  ��enumE������Э��
 *       ��ip��������IP�ַ���
 *       ��ComNum���������˿��ַ���
 *       ��id��ģ�����ӷ�������ID
 * ����  : 1�����ӳɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_Link_Server ( ENUM_NetPro_TypeDef enumE, char * ip, char * ComNum, ENUM_ID_NO_TypeDef id)
{
	char cStr [100] = { 0 };
	char cCmd [120] = { 0 };

  switch (  enumE )
  {
		case enumTCP:
		  sprintf ( cStr, "\"%s\",\"%s\",%s", "TCP", ip, ComNum );
		  break;
		
		case enumUDP:
		  sprintf ( cStr, "\"%s\",\"%s\",%s", "UDP", ip, ComNum );
		  break;

		case enumSSL:
		  sprintf ( cStr, "\"%s\",\"%s\",%s", "SSL", ip, ComNum );
		  break;
		
		default:
			break;
  }

  if ( id < 5 )
    sprintf ( cCmd, "AT+CIPSTART=%d,%s", id, cStr);

  else
	  sprintf ( cCmd, "AT+CIPSTART=%s", cStr );

	/* SSL 握手耗时较长，给足时间 */
	return ESP8266_Cmd ( cCmd, "OK", "ALREAY CONNECT", (enumE == enumSSL) ? 10000 : 4000 );
	
}


/*
 * ��������ESP8266_StartOrShutServer
 * ����  ��WF-ESP8266ģ�鿪����رշ�����ģʽ
 * ����  ��enumMode������/�ر�
 *       ��pPortNum���������˿ں��ַ���
 *       ��pTimeOver����������ʱʱ���ַ�������λ����
 * ����  : 1�������ɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_StartOrShutServer ( FunctionalState enumMode, char * pPortNum, char * pTimeOver )
{
	char cCmd1 [120] = {0};
	char cCmd2 [120] = {0};

	if ( enumMode )
	{
		sprintf ( cCmd1, "AT+CIPSERVER=%d,%s", 1, pPortNum );
		
		sprintf ( cCmd2, "AT+CIPSTO=%s", pTimeOver );

		return ( ESP8266_Cmd ( cCmd1, "OK", 0, 500 ) &&
						 ESP8266_Cmd ( cCmd2, "OK", 0, 500 ) );
	}
	
	else
	{
		sprintf ( cCmd1, "AT+CIPSERVER=%d,%s", 0, pPortNum );

		return ESP8266_Cmd ( cCmd1, "OK", 0, 500 );
	}
	
}


/*
 * ��������ESP8266_Get_LinkStatus
 * ����  ����ȡ WF-ESP8266 ������״̬�����ʺϵ��˿�ʱʹ��
 * ����  ����
 * ����  : 2�����ip
 *         3����������
 *         3��ʧȥ����
 *         0����ȡ״̬ʧ��
 * ����  �����ⲿ����
 */
uint8_t ESP8266_Get_LinkStatus ( void )
{
	if ( ESP8266_Cmd ( "AT+CIPSTATUS", "OK", 0, 500 ) )
	{
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "STATUS:2\r\n" ) )
			return 2;
		
		else if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "STATUS:3\r\n" ) )
			return 3;
		
		else if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "STATUS:4\r\n" ) )
			return 4;		

	}
	
	return 0;
	
}


/*
 * ��������ESP8266_Get_IdLinkStatus
 * ����  ����ȡ WF-ESP8266 �Ķ˿ڣ�Id������״̬�����ʺ϶�˿�ʱʹ��
 * ����  ����
 * ����  : �˿ڣ�Id��������״̬����5λΪ��Чλ���ֱ��ӦId5~0��ĳλ����1����Id���������ӣ�������0����Idδ��������
 * ����  �����ⲿ����
 */
uint8_t ESP8266_Get_IdLinkStatus ( void )
{
	uint8_t ucIdLinkStatus = 0x00;
	
	
	if ( ESP8266_Cmd ( "AT+CIPSTATUS", "OK", 0, 500 ) )
	{
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+CIPSTATUS:0," ) )
			ucIdLinkStatus |= 0x01;
		else 
			ucIdLinkStatus &= ~ 0x01;
		
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+CIPSTATUS:1," ) )
			ucIdLinkStatus |= 0x02;
		else 
			ucIdLinkStatus &= ~ 0x02;
		
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+CIPSTATUS:2," ) )
			ucIdLinkStatus |= 0x04;
		else 
			ucIdLinkStatus &= ~ 0x04;
		
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+CIPSTATUS:3," ) )
			ucIdLinkStatus |= 0x08;
		else 
			ucIdLinkStatus &= ~ 0x08;
		
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+CIPSTATUS:4," ) )
			ucIdLinkStatus |= 0x10;
		else 
			ucIdLinkStatus &= ~ 0x10;	

	}
	
	return ucIdLinkStatus;
	
}


/*
 * ��������ESP8266_Inquire_ApIp
 * ����  ����ȡ F-ESP8266 �� AP IP
 * ����  ��pApIp����� AP IP ��������׵�ַ
 *         ucArrayLength����� AP IP ������ĳ���
 * ����  : 0����ȡʧ��
 *         1����ȡ�ɹ�
 * ����  �����ⲿ����
 */
uint8_t ESP8266_Inquire_ApIp ( char * pApIp, uint8_t ucArrayLength )
{
	uint8_t uc;
	
	char * pCh;
	
	
  ESP8266_Cmd ( "AT+CIFSR", "OK", 0, 500 );
	
	pCh = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "APIP,\"" );
	
	if ( pCh )
		pCh += 6;
	
	else
		return 0;
	
	for ( uc = 0; uc < ucArrayLength; uc ++ )
	{
		pApIp [ uc ] = * ( pCh + uc);
		
		if ( pApIp [ uc ] == '\"' )
		{
			pApIp [ uc ] = '\0';
			break;
		}
		
	}
	
	return 1;
	
}


/*
 * ��������ESP8266_Inquire_StaIp
 * ����  ����ȡ F-ESP8266 �� STA IP
 * ����  ��pStaIp����� STA IP ��������׵�ַ
 *         ucArrayLength����� STA IP ������ĳ���
 * ����  : 0����ȡʧ��
 *         1����ȡ�ɹ�
 * ����  �����ⲿ����
 */
uint8_t ESP8266_Inquire_StaIp ( char * pApIp, uint8_t ucArrayLength )
{
	uint8_t uc;
	
	char * pCh;
	
	
  ESP8266_Cmd ( "AT+CIFSR", "OK", 0, 500 );
	
	pCh = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "STAIP,\"" );
	
	if ( pCh )
		pCh += 7;
	
	else
		return 0;
	
	for ( uc = 0; uc < ucArrayLength; uc ++ )
	{
		pApIp [ uc ] = * ( pCh + uc);
		
		if ( pApIp [ uc ] == '\"' )
		{
			pApIp [ uc ] = '\0';
			break;
		}
		
	}
	
	return 1;
	
}


/*
 * ��������ESP8266_UnvarnishSend
 * ����  ������WF-ESP8266ģ�����͸������
 * ����  ����
 * ����  : 1�����óɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_UnvarnishSend ( void )
{
	if ( ! ESP8266_Cmd ( "AT+CIPMODE=1", "OK", 0, 500 ) )
		return false;
	
	return 
	  ESP8266_Cmd ( "AT+CIPSEND", "OK", ">", 500 );
	
}


/*
 * ��������ESP8266_ExitUnvarnishSend
 * ����  ������WF-ESP8266ģ���˳�͸��ģʽ
 * ����  ����
 * ����  : ��
 * ����  �����ⲿ����
 */
void ESP8266_ExitUnvarnishSend ( void )
{
	Delay_ms ( 1000 );
	
	macESP8266_Usart ( "+++" );
	
	Delay_ms ( 500 ); 
	
}


/*
 * ��������ESP8266_SendString
 * ����  ��WF-ESP8266ģ�鷢���ַ���
 * ����  ��enumEnUnvarnishTx�������Ƿ���ʹ����͸��ģʽ
 *       ��pStr��Ҫ���͵��ַ���
 *       ��ulStrLength��Ҫ���͵��ַ������ֽ���
 *       ��ucId���ĸ�ID���͵��ַ���
 * ����  : 1�����ͳɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
bool ESP8266_SendString ( FunctionalState enumEnUnvarnishTx, char * pStr, u32 ulStrLength, ENUM_ID_NO_TypeDef ucId )
{
	char cStr [20];
	bool bRet = false;
	
		
	if ( enumEnUnvarnishTx )
	{
		macESP8266_Usart ( "%s", pStr );
		
		bRet = true;
		
	}

	else
	{
		if ( ucId < 5 )
			sprintf ( cStr, "AT+CIPSEND=%d,%u", ucId, (unsigned)(ulStrLength + 2) );

		else
			sprintf ( cStr, "AT+CIPSEND=%u", (unsigned)(ulStrLength + 2) );
		
		ESP8266_Cmd ( cStr, "> ", 0, 100 );

		bRet = ESP8266_Cmd ( pStr, "SEND OK", 0, 500 );
  }
	
	return bRet;

}


/*
 * ��������ESP8266_ReceiveString
 * ����  ��WF-ESP8266ģ������ַ���
 * ����  ��enumEnUnvarnishTx�������Ƿ���ʹ����͸��ģʽ
 * ����  : ���յ����ַ����׵�ַ
 * ����  �����ⲿ����
 */
char * ESP8266_ReceiveString ( FunctionalState enumEnUnvarnishTx )
{
	char * pRecStr = 0;
	
	
	strEsp8266_Fram_Record .InfBit .FramLength = 0;
	strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
	
	while ( ! strEsp8266_Fram_Record .InfBit .FramFinishFlag );
	strEsp8266_Fram_Record .Data_RX_BUF [ strEsp8266_Fram_Record .InfBit .FramLength ] = '\0';
	
	if ( enumEnUnvarnishTx )
		pRecStr = strEsp8266_Fram_Record .Data_RX_BUF;
	
	else 
	{
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+IPD" ) )
			pRecStr = strEsp8266_Fram_Record .Data_RX_BUF;

	}

	return pRecStr;
	
}


/*
 * ��������ESP8266_CWLIF
 * ����  ����ѯ�ѽ����豸��IP
 * ����  ��pStaIp������ѽ����豸��IP
 * ����  : 1���н����豸
 *         0���޽����豸
 * ����  �����ⲿ����
 */
uint8_t ESP8266_CWLIF ( char * pStaIp )
{
	uint8_t uc, ucLen;
	
	char * pCh, * pCh1;
	
	
  ESP8266_Cmd ( "AT+CWLIF", "OK", 0, 100 );
	
	pCh = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "," );
	
	if ( pCh )
	{
		pCh1 = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "AT+CWLIF\r\r\n" ) + 11;
	  ucLen = pCh - pCh1;
	}

	else
		return 0;
	
	for ( uc = 0; uc < ucLen; uc ++ )
		pStaIp [ uc ] = * ( pCh1 + uc);
	
	pStaIp [ ucLen ] = '\0';
	
	return 1;
	
}


/*
 * ��������ESP8266_CIPAP
 * ����  ������ģ��� AP IP
 * ����  ��pApIp��ģ��� AP IP
 * ����  : 1�����óɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
uint8_t ESP8266_CIPAP ( char * pApIp )
{
	char cCmd [ 30 ];
		
	
	sprintf ( cCmd, "AT+CIPAP=\"%s\"", pApIp );
	
  if ( ESP8266_Cmd ( cCmd, "OK", 0, 5000 ) )
		return 1;
 
	else 
		return 0;
	
}


/*
 * ��������ESP8266_CIPSTA
 * ����  ������ģ��� STA IP
 * ����  ��pStaIp��ģ��� STA IP
 * ����  : 1�����óɹ�
 *         0������ʧ��
 * ����  �����ⲿ����
 */
uint8_t ESP8266_CIPSTA ( char * pStaIp )
{
	char cCmd [ 30 ];
		
	
	sprintf ( cCmd, "AT+CIPSTA=\"%s\"", pStaIp );
	
  if ( ESP8266_Cmd ( cCmd, "OK", 0, 5000 ) )
		return 1;
 
	else 
		return 0;
	
}



