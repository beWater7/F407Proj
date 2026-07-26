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

/* 野火旧工程宏；FreeRTOS 下常用 Group_4 */
#ifndef macNVIC_PriorityGroup_x
#define macNVIC_PriorityGroup_x  NVIC_PriorityGroup_4
#endif

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
static char gs_esp8266_ssid[32];
static char gs_esp8266_psk[32];

char *getEsp8266Ssid(void)
{
    return gs_esp8266_ssid;
}

char *getEsp8266Psk(void)
{
    return gs_esp8266_psk;
}

void setEsp8266Ssid(char *ssid)
{
    CUSTOM_ASSERT(!ssid, return);
    memset_s(gs_esp8266_ssid, sizeof(gs_esp8266_ssid), 0,  sizeof(gs_esp8266_ssid));
    memcpy_s(gs_esp8266_ssid, sizeof(gs_esp8266_ssid), ssid, strlen(ssid));
}

void setEsp8266Psk(char *psk)
{
    CUSTOM_ASSERT(!psk, return);
    memset_s(gs_esp8266_psk, sizeof(gs_esp8266_psk), 0, sizeof(gs_esp8266_psk));
    memcpy_s(gs_esp8266_psk, sizeof(gs_esp8266_psk), psk, strlen(psk));
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
	
	
	/* �ж����� */
	USART_ITConfig ( macESP8266_USARTx, USART_IT_RXNE, ENABLE ); //ʹ�ܴ��ڽ����ж� 
	USART_ITConfig ( macESP8266_USARTx, USART_IT_IDLE, ENABLE ); //ʹ�ܴ������߿����ж� 	

	ESP8266_USART_NVIC_Configuration ();
	
	
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
	
	
	/* Configure the NVIC Preemption Priority Bits */  
	NVIC_PriorityGroupConfig ( macNVIC_PriorityGroup_x );

	/* Enable the USART2 Interrupt */
	NVIC_InitStructure.NVIC_IRQChannel = macESP8266_USART_IRQ;	 
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

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
bool ESP8266_JoinAP ( char * pSSID, char * pPassWord )
{
	char cCmd [120] = {0};

	sprintf ( cCmd, "AT+CWJAP=\"%s\",\"%s\"", pSSID, pPassWord );
	
	return ESP8266_Cmd ( cCmd, "OK", NULL, 5000 );

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
		
		default:
			break;
  }

  if ( id < 5 )
    sprintf ( cCmd, "AT+CIPSTART=%d,%s", id, cStr);

  else
	  sprintf ( cCmd, "AT+CIPSTART=%s", cStr );

	return ESP8266_Cmd ( cCmd, "OK", "ALREAY CONNECT", 4000 );
	
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
	char uc;
	
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
	char uc;
	
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
			sprintf ( cStr, "AT+CIPSEND=%d,%d", ucId, ulStrLength + 2 );

		else
			sprintf ( cStr, "AT+CIPSEND=%d", ulStrLength + 2 );
		
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



