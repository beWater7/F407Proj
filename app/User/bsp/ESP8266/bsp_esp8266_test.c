#include "bsp_esp8266.h"
#include "bsp_esp8266_test.h"
#include "core_delay.h"
#include <stdio.h>  
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "os_mutex.h"
#include "os_debug.h"
#include "lwip/def.h"
#include "bsp_led.h"
#include "bsp_usart.h"
#include "safe_utils.h"
#include "sntp_api.h"
/* beep 板级驱动未合入时用空宏占位（用法为 BEEP_ON; / BEEP_OFF;） */
#ifndef BEEP_ON
#define BEEP_ON
#define BEEP_OFF
#endif

#define ESP_JSON_LEN  1024
// 定义一个类型，代表接收函数指针和相关参数的回调类型
typedef bool (*FuncPtr)(void);  // 返回bool类型，表示是否成功的函数指针类型

static os_mutex_t gs_esp8266Data_mutex; 
uint8_t ucId, ucLen;
uint8_t ucLed1Status = 0, ucLed2Status = 0, ucLed3Status = 0, ucBuzzerStatus = 0;
char cStr [ 100 ] = { 0 }, cCh;
char * pCh, * pCh1;
//DHT11_Data_TypeDef DHT11_Data;

extern struct  STRUCT_USARTx_Fram strEsp8266_Fram_Record;

static char gs_weather[16];
static int gs_dwTemperature;
static char gs_byUptime[32];

void getWeather(char *data, uint8 len)
{
    CUSTOM_ASSERT(!data, return);
    CUSTOM_ASSERT(!len, return);

    memcpy_s(data, len, gs_weather, strlen(gs_weather));
    return;
}


int getTemperature(void)
{
    return gs_dwTemperature;
}


void getSysTime(char *data, uint8 len)
{
    CUSTOM_ASSERT(!data, return);
    CUSTOM_ASSERT(!len, return);

    memcpy_s(data, len, gs_byUptime, strlen(gs_byUptime));
    return;
}


#ifndef BUILTAP_TEST
/**
  * @brief  ESP8266 StaTcpServer Unvarnish ���ò��Ժ���
  * @param  ��
  * @retval ��
  */
void ESP8266_StaTcpServer_ConfigTest(void)
{
  printf( "\r\n�������� ESP8266 ......\r\n" );
  printf( "\r\nʹ�� ESP8266 ......\r\n" );
	macESP8266_CH_ENABLE();
	while( ! ESP8266_AT_Test() );
  while( ! ESP8266_DHCP_CUR () );
  printf( "\r\n�������ù���ģʽ STA ......\r\n" );
	while( ! ESP8266_Net_Mode_Choose ( STA ) );

  printf( "\r\n�������� WiFi ......\r\n" );
  while ( ! ESP8266_CIPSTA ( macUser_ESP8266_TcpServer_IP ) ); //����ģ��� AP IP
  while( ! ESP8266_JoinAP ( macUser_ESP8266_ApSsid, macUser_ESP8266_ApPwd ) );	

  printf( "\r\n���������� ......\r\n" );
	while( ! ESP8266_Enable_MultipleId ( ENABLE ) );
  
  printf( "\r\n����������ģʽ ......\r\n" );
	while ( !	ESP8266_StartOrShutServer ( ENABLE, macUser_ESP8266_TcpServer_Port, macUser_ESP8266_TcpServer_OverTime ) );

  ESP8266_Inquire_StaIp ( cStr, 20 );
	printf ( "\n��ģ��������WIFIΪ %s��\nSTA IP Ϊ��%s�������Ķ˿�Ϊ��%s\n�ֻ������������Ӹ� IP �Ͷ˿ڣ���������5���ͻ���\n",
           macUser_ESP8266_ApSsid, cStr, macUser_ESP8266_TcpServer_Port );
	
	
	strEsp8266_Fram_Record .InfBit .FramLength = 0;
	strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
	
	printf( "\r\n���� ESP8266 ���\r\n" );
  
}


#else


/**
  * @brief  ESP8266 ApTcpServer ���ò��Ժ���
  * @param  ��
  * @retval ��
  */
void ESP8266_ApTcpServer_ConfigTest(void)
{  
  printf( "\r\n�������� ESP8266 ......\r\n" );
  printf( "\r\nʹ�� ESP8266 ......\r\n" );
	macESP8266_CH_ENABLE();
	while( ! ESP8266_AT_Test() );
  
  printf( "\r\n�������ù���ģʽΪ AP ......\r\n" );
	while( ! ESP8266_Net_Mode_Choose ( AP ) );

  printf( "\r\n���ڴ���WiFi�ȵ� ......\r\n" );
  while ( ! ESP8266_CIPAP ( macUser_ESP8266_TcpServer_IP ) ); //����ģ��� AP IP
  while ( ! ESP8266_BuildAP ( macUser_ESP8266_BulitApSsid, macUser_ESP8266_BulitApPwd, macUser_ESP8266_BulitApEcn ) );
	
  printf( "\r\n���������� ......\r\n" );
	while( ! ESP8266_Enable_MultipleId ( ENABLE ) );
  
  printf( "\r\n����������ģʽ ......\r\n" );
	while ( !	ESP8266_StartOrShutServer ( ENABLE, macUser_ESP8266_TcpServer_Port, macUser_ESP8266_TcpServer_OverTime ) );

	
  ESP8266_Inquire_ApIp ( cStr, 20 );
	printf ( "\n��ģ��WIFIΪ %s�����뿪��\nAP IP Ϊ��%s�������Ķ˿�Ϊ��%s\n�ֻ������������Ӹ� IP �Ͷ˿ڣ���������5���ͻ���\n",
           macUser_ESP8266_BulitApSsid, cStr, macUser_ESP8266_TcpServer_Port );
	
	
	strEsp8266_Fram_Record .InfBit .FramLength = 0;
	strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
	
	printf( "\r\n���� ESP8266 ���\r\n" );
  
}


#endif


#if 0
/**
  * @brief  ESP8266 ��������Ϣ���������ݲ��Ժ���
  * @param  ��
  * @retval ��
  */
void ESP8266_CheckRecv_SendDataTest(void)
{
  
  if ( strEsp8266_Fram_Record .InfBit .FramFinishFlag )
  {
    USART_ITConfig ( macESP8266_USARTx, USART_IT_RXNE, DISABLE ); //���ô��ڽ����ж�
    
    strEsp8266_Fram_Record .Data_RX_BUF [ strEsp8266_Fram_Record .InfBit .FramLength ]  = '\0';
    printf("ucCh =%s\n",strEsp8266_Fram_Record .Data_RX_BUF);
//    printf ( "\r\n%s\r\n", strEsp8266_Fram_Record .Data_RX_BUF );//
    
    if ( ( pCh = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "CMD_LED_" ) ) != 0 )    //LED����
    {
      cCh = * ( pCh + 8 );
      printf("%c\n",cCh);
      switch ( cCh )
      {
        case '1':
          cCh = * ( pCh + 10 );
          switch ( cCh )
          {
            case '0':
              LED1_OFF;
              ucLed1Status = 0;
              break;
            case '1':
              LED1_ON;
              ucLed1Status = 1;
              break;
            default :
              break;
          }
          break;
          
        case '2':
          cCh = * ( pCh + 10 );
          switch ( cCh )
          {
            case '0':
              LED2_OFF;
              ucLed2Status = 0;
              break;
            case '1':
              LED2_ON;
              ucLed2Status = 1;
              break;
            default :
              break;
          }
          break;

        case '3':
          cCh = * ( pCh + 10 );
          switch ( cCh )
          {
            case '0':
              LED3_OFF;
              ucLed3Status = 0;
              break;
            case '1':
              LED3_ON;
              ucLed3Status = 1;
              break;
            default :
              break;
          }
          break;
          
        default :
          break;					
          
      }
      
      sprintf ( cStr, "CMD_LED_%d_%d_%d_ENDLED_END", ucLed1Status, ucLed2Status, ucLed3Status );
      
    }
    
    else if ( ( pCh = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "CMD_BUZZER_" ) ) != 0 )  //����������
    {
      cCh = * ( pCh + 11 );
      
      switch ( cCh )
      {
        case '0':
          BEEP_OFF;
          ucBuzzerStatus = 0;
          break;
        case '1':
          BEEP_ON;
          ucBuzzerStatus = 1;
          break;
        default:
          break;
      }
      
      sprintf ( cStr, "CMD_BUZZER_%d_ENDBUZZER_END", ucBuzzerStatus );
      
    }
      
    else if ( ( ( pCh  = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "CMD_UART_" ) ) != 0 ) && 
              ( ( pCh1 = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "_ENDUART_END" ) )  != 0 ) ) 
    {
      if ( pCh < pCh1)
      {
        ucLen = pCh1 - pCh + 12;
        memcpy ( cStr, pCh, ucLen );
        cStr [ ucLen ] = '\0';
      }
    }

    else if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "CMD_READ_ALL_END" ) != 0 )     //��ȡ״̬����
    {
      Read_DHT11 ( & DHT11_Data );
      sprintf ( cStr, "CMD_LED_%d_%d_%d_ENDLED_DHT11_%d.%d_%d.%d_ENDDHT11_BUZZER_%d_ENDBUZZER_END", 
                ucLed1Status, ucLed2Status, ucLed3Status, DHT11_Data .temp_int, 
                DHT11_Data .temp_deci, DHT11_Data .humi_int, DHT11_Data .humi_deci,
                ucBuzzerStatus );
    }
    
      
    if ( ( pCh = strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+IPD," ) ) != 0 ) 
    {
      ucId = * ( pCh + strlen ( "+IPD," ) ) - '0';
      ESP8266_SendString ( DISABLE, cStr, strlen ( cStr ), ( ENUM_ID_NO_TypeDef ) ucId );
    }
    
    strEsp8266_Fram_Record .InfBit .FramLength = 0;
    strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;	
    
    USART_ITConfig ( macESP8266_USARTx, USART_IT_RXNE, ENABLE ); //ʹ�ܴ��ڽ����ж�
    
  }
  
}
#endif


#define LED_CMD_NUMBER   8
char *ledCmd[ LED_CMD_NUMBER ] = { "LED_RED","LED_GREEN","LED_BLUE","LED_YELLOW","LED_PURPLE","LED_CYAN","LED_WHITE","LED_RGBOFF" };
       
volatile uint8_t ucTcpClosedFlag = 0;


int extract_json(const char *src, char *json_out, int out_size)
{
    const char *start = strstr(src, "{");      // 找到第一个 '{'
    const char *end   = strrchr(src, '}');     // 找最后一个 '}'

    if(!start || !end || end <= start) return -1;   // 防止异常

    int json_len = end - start + 1;            // JSON长度

    if(json_len >= out_size) return -2;        // 防止越界

    strncpy(json_out, start, json_len);
    json_out[json_len] = '\0';                 // 补结束符

    return 0;
}


/**
  * @brief  ��ȡ����������ֺʹ��ڵ������ַ�������Ϣ
  * @param  ��
  * @retval ��
  */
void Get_ESP82666_Cmd( char * cmd)
{
	uint8_t i;
	for(i = 0;i < LED_CMD_NUMBER; i++)
	{
     if(( bool ) strstr ( cmd, ledCmd[i] ))
		 break;
	}
	switch(i)
    {
      case 0:
        LED_RED;
      break;
      case 1:
        LED_GREEN;
      break;
      case 2:
        LED_BLUE;
      break;
      case 3:
        LED_YELLOW;
      break;
      case 4:
        LED_PURPLE;
      break;
      case 5:
        LED_CYAN;
      break;
      case 6:
        LED_WHITE;
      break;
      case 7:
        LED_RGBOFF;
      break;
      default:
        
        break;      
    } 
}


void ESP8266_cmd(char * cmd)
{
    cJSON *root = NULL;
    cJSON *now = NULL;
    cJSON *result = NULL;
    cJSON *temp = NULL;
    cJSON *text = NULL;
    cJSON *upt = NULL;
    int temperature = 0;
    char *pJson = NULL;

    pJson = (char *)os_malloc(ESP_JSON_LEN);
    CUSTOM_ASSERT(!pJson, return);
    memset(pJson, 0, ESP_JSON_LEN);

    if(extract_json(cmd, pJson, ESP_JSON_LEN))
    {
        goto end;
    }

    root = cJSON_Parse(pJson);
    CUSTOM_ASSERT(!root, goto end);

    // result节点
    result = cJSON_GetObjectItem(root, "result");
    CUSTOM_ASSERT(!result, goto end);

    // now节点
    now = cJSON_GetObjectItem(result, "now");
    CUSTOM_ASSERT(!now, goto end);

    // 解析温度(temp)
    temp = cJSON_GetObjectItem(now, "temp");
    temperature = temp ? temp->valueint : -999;
    gs_dwTemperature = temperature;

    // 解析天气(text)
    text = cJSON_GetObjectItem(now, "text");
    char *weather = text ? text->valuestring : "unknown";
    memcpy_s(gs_weather, sizeof(gs_weather), weather, strlen(weather));

    // 解析更新时间(uptime)
    upt = cJSON_GetObjectItem(now, "uptime");
    char *uptime = upt ? upt->valuestring : "unknown";
    memcpy_s(gs_byUptime, sizeof(gs_byUptime), uptime, strlen(uptime));

    //printf("天气: %s\n", weather);
    printf("temp: %d°C\n", temperature);
    printf("uptime: %s\n", uptime);

    RTC_Set_From_Uptime(uptime);
end:
    os_free(pJson);
    cJSON_Delete(root);
    return;
}


typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} DateTime;

void get_ntp_time(DateTime *dt) {
    // 发送获取时间命令
    //uart_send("AT+CIPSNTPTIME?\r\n");
    
    // 等待响应，解析类似：
    // +CIPSNTPTIME:Thu Jan  1 08:00:00 1970
    // 或者
    // +CIPSNTPTIME:Fri Oct 27 14:30:25 2023
}


uint8_t month_to_number(char *month) {
    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec"};
    for(int i = 0; i < 12; i++) {
        if(strncmp(month, months[i], 3) == 0) {
            return i + 1;
        }
    }
    return 1;
}

#if 0
void parse_ntp_response(char *response, DateTime *dt) {
    // 解析格式: +CIPSNTPTIME:Fri Oct 27 14:30:25 2023
    char *ptr = strstr(response, "+CIPSNTPTIME:");
    if(ptr) {
        ptr += 13; // 跳过"+CIPSNTPTIME:"
        
        // 跳过星期
        ptr = strchr(ptr, ' ');
        if(ptr) ptr++;
        
        // 解析月份
        char month[4];
        sscanf(ptr, "%3s", month);
        ptr += 4;
        
        // 解析日期、时间、年份
        sscanf(ptr, "%hhd %hhd:%hhd:%hhd %hd", 
               &dt->day, &dt->hour, &dt->minute, &dt->second, &dt->year);
        
        // 月份转换
        dt->month = month_to_number(month);
    }
}
#endif

// NTP协议数据包结构 (RFC 5905)
typedef struct {
    uint8_t li_vn_mode;      // 跳跃指示器, 版本号, 模式
    uint8_t stratum;         // 层级
    uint8_t poll;            // 轮询间隔
    uint8_t precision;       // 精度
    uint32_t root_delay;     // 根延迟
    uint32_t root_dispersion; // 根分散
    uint32_t reference_id;   // 参考ID
    uint32_t ref_ts_sec;     // 参考时间戳秒
    uint32_t ref_ts_frac;    // 参考时间戳分数
    uint32_t orig_ts_sec;    // 起源时间戳秒
    uint32_t orig_ts_frac;   // 起源时间戳分数
    uint32_t recv_ts_sec;    // 接收时间戳秒
    uint32_t recv_ts_frac;   // 接收时间戳分数
    uint32_t trans_ts_sec;   // 传输时间戳秒
    uint32_t trans_ts_frac;  // 传输时间戳分数
} ntp_packet;


void prepare_ntp_packet(ntp_packet *packet) {
    memset(packet, 0, sizeof(ntp_packet));
    
    // 设置LI=0, VN=3 (NTP版本3), Mode=3 (客户端)
    packet->li_vn_mode = 0x1B; // 二进制: 00 011 011
    
    // 其他字段保持为0
}


int send_ntp_request(void) {
    ntp_packet packet = {0};
    // 发送数据
    char send_cmd[50] = {0};
    sprintf(send_cmd, "AT+CIPSEND=%d\r\n", sizeof(ntp_packet));
    //macESP8266_Usart(send_cmd);
    ESP8266_Cmd ( send_cmd, "OK", ">", 500 );

    prepare_ntp_packet(&packet);
    os_sleep_ms(500);

    // 等待 ">" 提示符
    // 然后发送NTP数据包
    #if 1
    // 假设 macESP8266_Usart 是一个函数，负责将数据发送给 ESP8266
    uint8_t *data = (uint8_t *)&packet;  // 将结构体转换为字节流
    size_t packet_size = sizeof(ntp_packet);

    // 发送字节流数据到 ESP8266
    for (size_t i = 0; i < packet_size; i++) 
    {
        macESP8266_Usart((char*)&data[i], 1);  // 逐字节发送
    }
    #endif
    os_printf("NTP packet sent, waiting for response...\n");
    return 1;
}

#define NTP_TIMESTAMP_DELTA 2208988800UL // 1900年到1970年的秒数

uint32_t ntp_time_to_unix(uint32_t ntp_seconds) {
    return ntp_seconds - NTP_TIMESTAMP_DELTA;
}


void convert_unix_to_datetime(uint32_t unix_seconds, DateTime *dt) {
    // 简单的Unix时间戳转换 (简化版)
    uint32_t days = unix_seconds / 86400;
    uint32_t seconds_in_day = unix_seconds % 86400;
    
    dt->year = 1970 + (days / 365); // 简化计算
    dt->month = 1 + ((days % 365) / 30);
    dt->day = 1 + (days % 30);
    dt->hour = seconds_in_day / 3600;
    dt->minute = (seconds_in_day % 3600) / 60;
    dt->second = seconds_in_day % 60;

    //printf();
}


int parse_ntp_response(char *response, DateTime *dt) {
    ntp_packet *packet = (ntp_packet*)response;

    // 检查数据包有效性
    if((packet->li_vn_mode & 0x07) != 4) { // 模式应为4 (服务器)
        return 0;
    }

    if(packet->stratum == 0) { // stratum=0表示不可用
        return 0;
    }
    
    // 获取传输时间戳 (网络字节序需要转换)
    uint32_t ntp_seconds = ntohl(packet->trans_ts_sec);
    uint32_t unix_seconds = ntp_time_to_unix(ntp_seconds);
    
    // 转换为日期时间
    convert_unix_to_datetime(unix_seconds, dt);
    
    return 1;
}

void ESP8266_ConnectToHTTPS(const char *host) {
    char cmd[100] = {0};
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"SSL\",\"%s\",443\r\n", host);  // 建立 HTTPS 连接
    //macESP8266_Usart(cmd);
    ESP8266_Cmd (cmd, "OK", "ALREAY CONNECT", 4000 );
}

void ESP8266_SendHTTPSRequest(char *request) {
    char cmd[32] = {0};
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", strlen(request));  // 设置发送数据长度
    macESP8266_Usart(cmd);
    os_sleep_ms(500);

    macESP8266_Usart(request);  // 发送 HTTPS 请求
}

void ESP8266_SendNtpRequest(void) 
{
    char http_request[256] = {0};  // 确保有足够的空间
    char byCmd[32] = {0};
    uint32_t dwLen = 0;

    /* GET /weather/v1/?district_id=330108&data_type=now&ak=q5WTx3ZpRur6HQ6tphYScfCGu0GtK1Mm HTTP/1.1 */
    snprintf(http_request, sizeof(http_request),
             "GET /api/timezone/Etc/UTC HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: STM32F407\r\n"
             "Connection: close\r\n"
             "\r\n",
             SNTP_SERVER);
    //ESP8266_SendHTTPSRequest(http_request);

    dwLen = strlen(http_request);
    // 调试：打印生成的请求
    os_printf("HTTP Request:%s\n", http_request);

    os_printf("Request length: %d\n", dwLen);
    // 发送 AT 命令启动数据发送
    snprintf(byCmd, sizeof(byCmd), "AT+CIPSEND=%d,%d\r\n",Multiple_ID_1, dwLen);
    macESP8266_Usart(byCmd);  // 发送数据长度
    os_sleep_ms(500);
    macESP8266_Usart(http_request);  // 发送拼接后的请求

    return;
}

// 
void ESP8266_RequestWeather(void) {
    // connect to wttr.in request weather info
    /* https://wttr.in/London?format=%C+%t+%h+%w 
     * %C --  weather
     * %t --  temperature
     * %h --  Humidity
     * %w --  wind speed
     */
    char http_request[256] = {0};  // 确保有足够的空间
    char byCmd[32] = {0};
    uint32_t dwLen = 0;

    /* GET /weather/v1/?district_id=330108&data_type=now&ak=q5WTx3ZpRur6HQ6tphYScfCGu0GtK1Mm HTTP/1.1 */
    snprintf(http_request, sizeof(http_request),
             "GET /weather/v1/?district_id=%d&data_type=%s&ak=%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: STM32F407\r\n"
             "Connection: close\r\n"
             "\r\n",
             DISTRICT_ID, "now", BAIDU_WEATHER_APIKEY, BAIDU_WEATHER_SERVER);

    dwLen = strlen(http_request);
    // 调试：打印生成的请求
    os_printf("HTTP Request:%s\n", http_request);

    os_printf("Request length: %d\n", dwLen);
    // 发送 AT 命令启动数据发送
    snprintf(byCmd, sizeof(byCmd), "AT+CIPSEND=%d\r\n", dwLen);
    macESP8266_Usart(byCmd);  // 发送数据长度
    os_sleep_ms(500);
    macESP8266_Usart(http_request);  // 发送拼接后的请求
}

uint8 ESP8266_TimeoutHandler(FuncPtr func, uint32 time)
{
    bool bResult = 0;
    uint32 cnt = 0;
    FOREVER
    {
        bResult = func();
        if(bResult)
        {
            return 1;
        }
        Delay_ms(200);
        cnt++;
        if(cnt == time)
        {
            return 0;
        }
    }
}

// 连接Wi-Fi的原始函数
static bool __ESP8266_JoinAP(void) {
    return ESP8266_JoinAP(getEsp8266Ssid(), getEsp8266Psk());  // 假设这个函数返回bool类型
}

// 示例：你可以传入 ESP8266_JoinAP 函数，控制连接 Wi-Fi 的超时
uint8_t ESP8266_JoinAP_timeout(uint32_t time) {
    return ESP8266_TimeoutHandler(__ESP8266_JoinAP, time);  // 将 ESP8266_JoinAP 函数传递给超时处理函数
}

// 连接Wi-Fi的原始函数
static bool __ESP8266_link_server(void) {
    return ESP8266_Link_Server(enumTCP, BAIDU_WEATHER_SERVER, WEATHER_SERVER_PORT, Single_ID_0);  // 假设这个函数返回bool类型
}

// 示例：你可以传入 ESP8266_JoinAP 函数，控制连接 Wi-Fi 的超时
uint8_t ESP8266_linkServer_timeout(uint32_t time) {
    return ESP8266_TimeoutHandler(__ESP8266_link_server, time);  // 将 ESP8266_JoinAP 函数传递给超时处理函数
}

// 检查是否已经处于期望的 DHCP 状态
int checkEsp8266DhcpStatus() 
{
    char response[48] = {0};
    os_mutex_lock(gs_esp8266Data_mutex, OS_WAIT_FOREVER);
    ESP8266_get_cmd_reply("AT+CWDHCP?", response, sizeof(response), 500);  // 查询当前 DHCP 状态
    os_mutex_unlock(gs_esp8266Data_mutex);
    //printf("1response:%s\n", response);
    // 解析 response 是否已经是 "1,1"，如果是就无需重新执行
    if (strstr(response, "3") != NULL) 
    {
        return 1;  // 已经是启用 DHCP
    }
    return 0;  // DHCP 未启用
}

// 检查 ESP8266 当前的工作模式
int checkEsp8266WorkMode() 
{
    char response[48] = {0};
    os_mutex_lock(gs_esp8266Data_mutex, OS_WAIT_FOREVER);
    ESP8266_get_cmd_reply("AT+CWMODE?", response, sizeof(response), 500);  // 查询当前工作模式
    os_mutex_unlock(gs_esp8266Data_mutex);
    //printf("2response:%s\n", response);
    // 解析返回的 response 来判断当前的工作模式
    if (strstr(response, "1") != NULL) {
        return 1;  // Station 模式（STA）
    } else if (strstr(response, "2") != NULL) {
        return 2;  // Access Point 模式（AP）
    } else if (strstr(response, "3") != NULL) {
        return 3;  // Station + Access Point 模式（STA+AP）
    }

    return 0;  // 未知模式
}

/**
  * @brief  ESP8266 StaTcpClient Unvarnish ���ò��Ժ���
  * @param  ��
  * @retval ��
  */
void ESP8266_StaTcpClient_Unvarnish_ConfigTest(void)
{
    uint8 byRet = 0;

    os_printf( "enable ESP8266 ......\r\n" );
    macESP8266_CH_ENABLE();

    if(!gs_esp8266Data_mutex)
    {
       os_mutex_init(gs_esp8266Data_mutex);
    }

    //macESP8266_Usart("AT+GMR");
    //while( ! ESP8266_AT_Test() );
    ESP8266_leave_AP();
    if(!checkEsp8266DhcpStatus())
    {
        while( ! ESP8266_DHCP_CUR () );
    }

    byRet = checkEsp8266WorkMode();
    printf("byRet:%d \r\n", byRet);
    if(1 != byRet)
    {
        printf( "\r\nset workScene: STA ......\r\n" );
        while( ! ESP8266_Net_Mode_Choose ( STA ) );
    }

    os_printf("contect WiFi:%s......\r\n", getEsp8266Ssid());
    //while( ! ESP8266_JoinAP ( macUser_ESP8266_ApSsid, macUser_ESP8266_ApPwd ) );
    //while( ! ESP8266_JoinAP ( getEsp8266Ssid(), getEsp8266Psk() ) );
    byRet = ESP8266_JoinAP_timeout(3);
    if(!byRet)
    {
        os_debug("ESP8266_JoinAP_timeout !\r\n");
        return;
    }
    //printf( "\r\nforbidden MultipleId......\r\n" );
    while( ! ESP8266_Enable_MultipleId ( DISABLE ) );

    os_printf("connect Server:%s ......\r\n", BAIDU_WEATHER_SERVER);
    //while( !	ESP8266_Link_Server ( enumTCP, macUser_ESP8266_TcpServer_IP, macUser_ESP8266_TcpServer_Port, Single_ID_0 ) );

    //while( !	ESP8266_Link_Server ( enumTCP, BAIDU_WEATHER_SERVER, WEATHER_SERVER_PORT, Single_ID_0 ) );
    byRet = ESP8266_linkServer_timeout(3);
    if(!byRet)
    {
        os_debug("ESP8266_linkServer_timeout !\r\n");
        return;
    }
    //while( !	ESP8266_Link_Server ( enumTCP, SNTP_SERVER, SNTP_SERVER_PORT, Multiple_ID_1 ) );
    os_sleep_ms(500);

	  // /* send cmd to esp8266 */
    // ESP8266_Cmd ("AT+CIPSNTPCFG=1,\"pool.ntp.org\",8\r\n", "OK", ">", 500);

    // // 查询时间
    // ESP8266_Cmd ("AT+CIPSNTPTIME?\r\n", "OK", ">", 500);
	  //macESP8266_Usart ("AT+CIPSNTPTIME?\r\n");
    //printf( "\r\nEnter pass-through mode ......\r\n" );

    ESP8266_RequestWeather();
    // os_sleep_ms(500);
    // ESP8266_SendNtpRequest();

    #if 0
    /* long connect mode such as mqtt */
    while( ! ESP8266_UnvarnishSend () );

    printf( "\r\nconfig ESP8266 success!\r\n" );
    printf ( "\r\npass-through......\r\n" );
    #endif
    return;
}


/**
  * @brief  ESP8266 ����Ƿ���յ������ݣ�������Ӻ͵�������
  * @param  ��
  * @retval ��
  */
void ESP8266_CheckRecvDataTest(void)
{
  uint8_t ucStatus;
  uint16_t i;
  
  /* ������յ��˴��ڵ������ֵ����� */
  if(strUSART_Fram_Record.InfBit.FramFinishFlag == 1)
  {
    for(i = 0;i < strUSART_Fram_Record.InfBit.FramLength; i++)
    {
       USART_SendData( macESP8266_USARTx ,strUSART_Fram_Record.Data_RX_BUF[i]); //ת����ESP82636
       while(USART_GetFlagStatus(macESP8266_USARTx,USART_FLAG_TC)==RESET){}      //�ȴ��������
    }
    strUSART_Fram_Record .InfBit .FramLength = 0;                                //�������ݳ�������
    strUSART_Fram_Record .InfBit .FramFinishFlag = 0;                            //���ձ�־����
    Get_ESP82666_Cmd(strUSART_Fram_Record .Data_RX_BUF);                         //���һ���ǲ��ǵ������
  }

  /* ������յ���ESP8266������ */
  if(strEsp8266_Fram_Record.InfBit.FramFinishFlag)
  {                                                      
    for(i = 0;i < strEsp8266_Fram_Record .InfBit .FramLength; i++)               
    {
       /* esp8266 data send to usart1 */
       USART_SendData( DEBUG_USART ,strEsp8266_Fram_Record .Data_RX_BUF[i]);    //ת����ESP8266
       while(USART_GetFlagStatus(DEBUG_USART,USART_FLAG_TC)==RESET){}
    }
    if(1)
    {
        os_mutex_lock(gs_esp8266Data_mutex, OS_WAIT_FOREVER);
        strEsp8266_Fram_Record .InfBit .FramLength = 0;                             //�������ݳ�������
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;                           //���ձ�־����
        //Get_ESP82666_Cmd(strEsp8266_Fram_Record .Data_RX_BUF);                      //���һ���ǲ��ǵ������
        ESP8266_cmd(strEsp8266_Fram_Record.Data_RX_BUF);
        os_mutex_unlock(gs_esp8266Data_mutex);
    }
  }
  #if 0
  if ( ucTcpClosedFlag )                                             //����Ƿ�ʧȥ����
  {
    ESP8266_ExitUnvarnishSend ();                                    //�˳�͸��ģʽ
    
    do ucStatus = ESP8266_Get_LinkStatus ();                         //��ȡ����״̬
    while ( ! ucStatus );
    
    if ( ucStatus == 4 )                                             //ȷ��ʧȥ���Ӻ�����
    {
      printf ( "\r\n���������ȵ�ͷ����� ......\r\n" );
      
      while ( ! ESP8266_JoinAP ( macUser_ESP8266_ApSsid, macUser_ESP8266_ApPwd ) );
      
      while ( !	ESP8266_Link_Server ( enumTCP, macUser_ESP8266_TcpServer_IP, macUser_ESP8266_TcpServer_Port, Single_ID_0 ) );
      
      printf ( "\r\n�����ȵ�ͷ������ɹ�\r\n" );

    }

    while ( ! ESP8266_UnvarnishSend () );
  }
  #endif  
}
