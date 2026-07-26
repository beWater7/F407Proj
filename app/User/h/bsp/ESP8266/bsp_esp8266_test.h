#ifndef  __ESP8266_TEST_H
#define	 __ESP8266_TEST_H



#include "stm32f4xx.h"


//#define   BUILTAP_TEST    //���������л���ESP8266�����ã�STAģʽ�� APģʽ



/********************************** �û���Ҫ���õĲ���**********************************/
#ifndef BUILTAP_TEST
#define   macUser_ESP8266_ApSsid              "301"         //Ҫ���ӵ��ȵ������
#define   macUser_ESP8266_ApPwd               "15825524932"           //Ҫ���ӵ��ȵ����Կ
#else
#define   macUser_ESP8266_BulitApSsid         "BinghuoLink"      //Ҫ�������ȵ������
#define   macUser_ESP8266_BulitApEcn           OPEN               //Ҫ�������ȵ�ļ��ܷ�ʽ
#define   macUser_ESP8266_BulitApPwd           "wildfire"         //Ҫ�������ȵ����Կ
#endif


#define   macUser_ESP8266_TcpServer_IP         "192.168.101.85"      //������������IP��ַ
#define   macUser_ESP8266_TcpServer_Port       "8000"             //�����������Ķ˿�   

#define   SNTP_SERVER_UDP                          "time.windows.com"
#define   SNTP_SERVER_UDP_PORT                     "123"

#define   SNTP_SERVER                          "worldtimeapi.org"
#define   SNTP_SERVER_PORT                     "80"
/*
 * http://worldtimeapi.org/api/timezone/Etc/UTC
 *
 */

// baidu weather api
#define DISTRICT_ID           330108
#define BAIDU_WEATHER_APIKEY  "q5WTx3ZpRur6HQ6tphYScfCGu0GtK1Mm"  //from baidu
#define BAIDU_WEATHER_SERVER  "api.map.baidu.com"
/*
 * example:
 * http://api.map.baidu.com/weather/v1/?district_id=330108&data_type=now&ak=你的ak
 */

/* weather */
#define   WEATHER_SERVER       "wttr.in" //HTTPS only
#define   WEATHER_SERVER_PORT  "80"
#define   WEATHER_CITY         "Hangzhou"  // 你想查询的城市

#define   macUser_ESP8266_TcpServer_OverTime   "1800"             //��������ʱʱ�䣨��λ���룩


/********************************** 外部全局变量 ***************************************/
extern volatile uint8_t ucTcpClosedFlag;


/********************************** ���Ժ������� ***************************************/
void ESP8266_StaTcpServer_ConfigTest(void);
void ESP8266_ApTcpServer_ConfigTest(void);
void ESP8266_CheckRecv_SendDataTest(void);


void ESP8266_StaTcpClient_Unvarnish_ConfigTest(void);
void ESP8266_CheckRecvDataTest(void);

void getWeather(char *data, uint8_t len);
int getTemperature(void);
void getSysTime(char *data, uint8_t len);

#endif

