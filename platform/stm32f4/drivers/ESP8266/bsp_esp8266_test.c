#include "bsp_esp8266.h"
#include "bsp_esp8266_test.h"
#include "core_delay.h"
#include <stdio.h>  
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "os_mutex.h"
#include "os_debug.h"
#include "os_log.h"
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

/* 直接按字节把数据发给模块，不做 printf 格式化。
   注意：macESP8266_Usart 是 printf 风格，若数据里含 '%'（如 URL 的 %2F）
   会被 vsnprintf 当成格式符读取不存在的参数，产生乱码、字节数也不对。 */
static void esp8266_send_raw(const char *data, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        while (USART_GetFlagStatus(macESP8266_USARTx, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(macESP8266_USARTx, (uint16_t)(uint8_t)data[i]);
    }
}

static char gs_weather[16];
static int gs_dwTemperature;
static char gs_byUptime[32];
static volatile uint8_t gs_wifi_reconfig_pending;

/* ---- 天气获取调度：定时刷新 + 失败重试 + 统计 ---- */
#define WEATHER_REFRESH_INTERVAL_MS  (30UL * 60UL * 1000UL)  /* 两次成功获取之间的最小间隔 */
#define WEATHER_RESP_TIMEOUT_MS      (10UL * 1000UL)         /* SEND OK 后等待 +IPD 响应的上限 */
#define WEATHER_RETRY_BACKOFF_MS     (30UL * 1000UL)         /* 重试退避基数：30s/60s/120s… */
#define WEATHER_MAX_RETRY            5

static volatile uint32_t gs_weather_last_ok_ms;      /* 最近一次成功解析的时间戳(ms)，0=从未成功 */
static volatile uint32_t gs_weather_next_poll_ms;    /* 在此时间(ms)之前不发起请求 */
static volatile uint32_t gs_weather_sent_ms;         /* 最近一次请求发出的时间(ms) */
static volatile uint8_t  gs_weather_retry_cnt;
static volatile uint8_t  gs_weather_awaiting;        /* 请求已发出，正在等 +IPD */
static volatile uint8_t  gs_weather_busy;
static volatile uint32_t gs_weather_ok_count;
static volatile uint32_t gs_weather_fail_count;

/* 跨帧 JSON 累积：Open-Meteo 响应可能被 IDLE 拆成多帧，拼齐后再解析 */
static char     gs_json_accum[ESP_JSON_LEN];
static uint16_t gs_json_accum_len;

void ESP8266_WeatherStats(uint32_t *ok, uint32_t *fail, uint32_t *last_ok_ms)
{
    if (ok)         { *ok = gs_weather_ok_count; }
    if (fail)       { *fail = gs_weather_fail_count; }
    if (last_ok_ms) { *last_ok_ms = gs_weather_last_ok_ms; }
}

/* 前向声明：WeatherPoll 在下面定义前调用 */
uint8_t ESP8266_linkServer_timeout(uint32_t time);

void ESP8266_RequestWifiReconfig(void)
{
    gs_wifi_reconfig_pending = 1;
    ESP8266_WifiApplySet(WIFI_APPLY_PENDING, NULL);
}

uint8_t ESP8266_ConsumeWifiReconfigRequest(void)
{
    uint8_t pending = gs_wifi_reconfig_pending;
    if (pending) {
        gs_wifi_reconfig_pending = 0;
    }
    return pending;
}

static void esp8266_mutex_ensure(void)
{
    if (!gs_esp8266Data_mutex) {
        os_mutex_init(gs_esp8266Data_mutex);
    }
}

void ESP8266_ProcessPendingWifiReconfig(void)
{
    if (!ESP8266_ConsumeWifiReconfigRequest()) {
        return;
    }
    LOGI(LOG_MOD_ESP8266, "apply wifi reconfig asynchronously\r\n");
    ESP8266_WifiApplySet(WIFI_APPLY_CONNECTING, NULL);
    ESP8266_StaTcpClient_Unvarnish_ConfigTest();
}

void ESP8266_WifiStatus(void)
{
    const char *ssid = getEsp8266Ssid();
    const char *psk = getEsp8266Psk();

    os_printf("wifi  state=%s\r\n", ESP8266_WifiApplyStateStr());
    os_printf("      error=%s\r\n", ESP8266_WifiApplyError());
    os_printf("      ssid =%s\r\n", (ssid && ssid[0]) ? ssid : "(empty)");
    os_printf("      psk  =%s\r\n", (psk && psk[0]) ? psk : "(empty)");
}

void ESP8266_WifiScan(void)
{
    esp8266_mutex_ensure();
    os_printf("wifi scan: power up ESP8266, AT+CWLAP...\r\n");
    macESP8266_CH_ENABLE();
    ESP8266_USART_RxIrqCtrl(1);
    os_sleep_ms(2000);
    (void)ESP8266_Cmd("AT", "OK", NULL, 1000);
    (void)ESP8266_Cmd("AT+CWMODE=1", "OK", "no change", 2000);
    os_mutex_lock(gs_esp8266Data_mutex, OS_WAIT_FOREVER);
    (void)ESP8266_Cmd("AT+CWLAP", "OK", NULL, 8000);
    os_mutex_unlock(gs_esp8266Data_mutex);
    os_printf("wifi scan done\r\n");
}

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


/* Open-Meteo WMO 天气码 -> 文字（gs_weather 用，仅存 16 字节） */
static const char *weather_code_text(int code)
{
    switch (code) {
    case 0:  return "Clear";
    case 1:  return "MainlyClear";
    case 2:  return "PartlyCld";
    case 3:  return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67: return "Rain";
    case 71:
    case 73:
    case 75:
    case 77: return "Snow";
    case 80:
    case 81:
    case 82: return "Showers";
    case 85:
    case 86: return "SnowShwr";
    case 95: return "Thunder";
    case 96:
    case 99: return "Thun+Hail";
    default: return "Unknown";
    }
}

void ESP8266_cmd(char * cmd)
{
    cJSON *root = NULL;
    cJSON *now = NULL;
    cJSON *temp = NULL;
    cJSON *code = NULL;
    cJSON *time = NULL;
    char *pJson = NULL;
    char uptime[24] = {0};
    const char *acc;

    pJson = (char *)os_malloc(ESP_JSON_LEN);
    CUSTOM_ASSERT(!pJson, return);
    memset(pJson, 0, ESP_JSON_LEN);

    /* 跨帧累积：上一帧是残缺 JSON 时，把本帧尾巴续上（从第一个 '{' 起，去掉 +IPD 头） */
    if (gs_json_accum_len > 0) {
        acc = strchr(cmd, '{');
        if (acc == NULL) {
            acc = cmd;
        }
        while (gs_json_accum_len < sizeof(gs_json_accum) - 1U && *acc) {
            gs_json_accum[gs_json_accum_len++] = *acc++;
        }
        gs_json_accum[gs_json_accum_len] = '\0';
        cmd = gs_json_accum;
    }

    if(extract_json(cmd, pJson, ESP_JSON_LEN))
    {
        /* 有 '{' 但没有 '}'：JSON 还没收全，暂存等下一帧 */
        acc = strchr(cmd, '{');
        if (acc && strrchr(cmd, '}') == NULL) {
            gs_json_accum_len = 0;
            while (gs_json_accum_len < sizeof(gs_json_accum) - 1U && *acc) {
                gs_json_accum[gs_json_accum_len++] = *acc++;
            }
            gs_json_accum[gs_json_accum_len] = '\0';
            LOGI(LOG_MOD_ESP8266, "wifi: json partial, buffered %u bytes, wait next frame\r\n",
                 (unsigned)gs_json_accum_len);
            goto end;
        }
        gs_json_accum_len = 0;
        LOGI(LOG_MOD_ESP8266, "wifi: no complete json in rx (len=%u): %.120s\r\n",
             (unsigned)strlen(cmd), cmd);
        goto end;
    }
    gs_json_accum_len = 0;

    root = cJSON_Parse(pJson);
    CUSTOM_ASSERT(!root, goto end);

    /* Open-Meteo: {"current_weather":{"temperature":26.2,"weathercode":3,"time":"2026-08-21T14:45"}} */
    now = cJSON_GetObjectItem(root, "current_weather");
    CUSTOM_ASSERT(!now, goto end);

    // 解析温度(temperature)
    temp = cJSON_GetObjectItem(now, "temperature");
    if (temp && cJSON_IsNumber(temp)) {
        gs_dwTemperature = (int)temp->valuedouble;
    }

    // 解析天气(weathercode -> WMO 编码)
    code = cJSON_GetObjectItem(now, "weathercode");
    if (code) {
        const char *text = weather_code_text(code->valueint);
        memcpy_s(gs_weather, sizeof(gs_weather), text, strlen(text));
    }

    // 解析时间(time)："2026-08-21T14:45" -> "20260821144500"
    time = cJSON_GetObjectItem(now, "time");
    if (time && time->valuestring) {
        const char *src = time->valuestring;
        int i, j;
        for (i = 0, j = 0; src[i] && j < (int)sizeof(uptime) - 1; i++) {
            if (src[i] >= '0' && src[i] <= '9') {
                uptime[j++] = src[i];
            }
        }
        while (j < 14 && j < (int)sizeof(uptime) - 1) {
            uptime[j++] = '0';
        }
        uptime[j] = '\0';
        memcpy_s(gs_byUptime, sizeof(gs_byUptime), uptime, strlen(uptime));
    }

    LOGI(LOG_MOD_ESP8266, "temp: %d C\n", gs_dwTemperature);
    LOGI(LOG_MOD_ESP8266, "weather: %s\n", gs_weather);
    LOGI(LOG_MOD_ESP8266, "uptime: %s\n", gs_byUptime);

    if (gs_byUptime[0]) {
        RTC_Set_From_Uptime(gs_byUptime);
    }

    /* 天气数据有效：记录成功时间，并把下次调度推到刷新周期之后，
     * 否则 WeatherPoll 会立即再发起一次请求（连接刚关闭，AT+CIPSTART 必报 ERROR） */
    gs_weather_last_ok_ms = os_time();
    gs_weather_retry_cnt = 0;
    gs_weather_awaiting = 0;
    gs_weather_next_poll_ms = gs_weather_last_ok_ms + WEATHER_REFRESH_INTERVAL_MS;
    gs_weather_ok_count++;
end:
    os_free(pJson);
    cJSON_Delete(root);
    return;
}

//
/* 清空 ESP8266 接收缓冲（注意 Data_RX_BUF 无 null 终止，须同时清长度） */
static void esp8266_clear_rx_buf(void)
{
    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    strEsp8266_Fram_Record.Data_RX_BUF[0] = '\0';
}

/* 轮询模块接收缓冲，直到出现 needle；每 50ms 检查一次，loops 次后超时 */
static int esp8266_wait_rx(const char *needle, uint32_t loops)
{
    uint32_t i;
    uint16_t len;

    for (i = 0; i < loops; i++) {
        os_sleep_ms(50);
        if (strEsp8266_Fram_Record.InfBit.FramLength) {
            /* 关 RX 中断冻结帧内容，避免与 ISR 写缓冲竞态；复制量小，窗口是微秒级 */
            ESP8266_USART_RxIrqCtrl(0);
            len = strEsp8266_Fram_Record.InfBit.FramLength;
            if (len >= RX_BUF_MAX_LEN) {
                len = RX_BUF_MAX_LEN - 1U;
            }
            strEsp8266_Fram_Record.Data_RX_BUF[len] = '\0';
            ESP8266_USART_RxIrqCtrl(1);
            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, needle)) {
                return 1;
            }
        }
    }
    return 0;
}

/* 重试退避：30s/60s/120s/240s…，封顶 240s */
static uint32_t weather_backoff(uint8_t retry_cnt)
{
    uint32_t b = WEATHER_RETRY_BACKOFF_MS << (retry_cnt - 1U);
    if (retry_cnt > 3U) {
        b = 4U * WEATHER_RETRY_BACKOFF_MS;
    }
    return b;
}

/* 天气获取调度：WiFi OK 后首次立即拉取，之后按间隔刷新；失败按退避重试。
 * 由 esp8266 recv task 周期调用（同一任务，与命令应答互斥）。 */
void ESP8266_WeatherPoll(void)
{
    uint32_t now = os_time();

    if (gs_weather_busy) {
        return;  /* 上一次请求还在收尾，下一轮再试 */
    }

    /* 请求已发出，正在等 +IPD：10s 内没有解析成功就算失败，进入退避 */
    if (gs_weather_awaiting) {
        if ((now - gs_weather_sent_ms) < WEATHER_RESP_TIMEOUT_MS) {
            return;  /* 还在窗口内，继续等 */
        }
        gs_weather_awaiting = 0;
        gs_weather_retry_cnt++;
        gs_weather_fail_count++;
        LOGI(LOG_MOD_ESP8266, "wifi: weather response timeout\r\n");
        gs_weather_next_poll_ms = now + weather_backoff(gs_weather_retry_cnt);
        return;
    }

    /* 仅在 WiFi 已连上（状态机 OK）后调度 */
    if (ESP8266_WifiApplyState() != WIFI_APPLY_OK) {
        return;
    }

    if (now < gs_weather_next_poll_ms) {
        return;  /* 还没到调度时间（刷新间隔或退避中） */
    }

    if (gs_weather_retry_cnt >= WEATHER_MAX_RETRY) {
        LOGI(LOG_MOD_ESP8266, "wifi: weather retries exhausted, wait next refresh\r\n");
        gs_weather_retry_cnt = 0;
        gs_weather_next_poll_ms = now + WEATHER_REFRESH_INTERVAL_MS;
        return;
    }

    gs_weather_busy = 1;

    /* 确保到天气服务器的 TCP 还在（已连则返回 ALREAY CONNECT 也算成功） */
    if (!ESP8266_linkServer_timeout(2)) {
        LOGI(LOG_MOD_ESP8266, "wifi: weather reconnect server failed\r\n");
        gs_weather_retry_cnt++;
        gs_weather_fail_count++;
        gs_weather_next_poll_ms = now + weather_backoff(gs_weather_retry_cnt);
        gs_weather_busy = 0;
        return;
    }
    os_sleep_ms(300);

    if (ESP8266_RequestWeather() == 0) {
        /* 发送失败：立即进入退避 */
        gs_weather_retry_cnt++;
        gs_weather_fail_count++;
        gs_weather_next_poll_ms = now + weather_backoff(gs_weather_retry_cnt);
    } else {
        /* 已发出：登记等待窗口，解析成功会在 ESP8266_cmd 里清 awaiting */
        gs_weather_awaiting = 1;
        gs_weather_sent_ms = os_time();
        gs_weather_next_poll_ms = now;
    }
    gs_weather_busy = 0;
}

int ESP8266_RequestWeather(void) {
    char http_request[256] = {0};  // 确保有足够的空间
    char byCmd[32] = {0};
    uint32_t dwLen = 0;
    int got_ok = 0;
    int got_prompt = 0;

    /* Open-Meteo 纯 HTTP，timezone=Asia/Shanghai 让返回时间为本地时间 */
    snprintf(http_request, sizeof(http_request),
             "GET /v1/forecast?latitude=%s&longitude=%s&current_weather=true&timezone=Asia%%2FShanghai HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: STM32F407\r\n"
             "Connection: close\r\n"
             "\r\n",
             WEATHER_LAT, WEATHER_LON, WEATHER_SERVER);

    dwLen = strlen(http_request);
    // 调试：打印生成的请求(信息量较大，归 INFO 级)
    LOGI(LOG_MOD_ESP8266, "HTTP Request:%s\n", http_request);

    LOGI(LOG_MOD_ESP8266, "Request length: %d\n", dwLen);

    /* 发送 AT 命令启动数据发送；发前清掉旧响应（CIPSTART 的 CONNECT/OK 残留） */
    esp8266_clear_rx_buf();
    snprintf(byCmd, sizeof(byCmd), "AT+CIPSEND=%u\r\n", (unsigned)dwLen);
    macESP8266_Usart(byCmd);  // 发送数据长度

    /* 等模块就绪：标准 AT 回 '>' 提示符，老固件可能只回 OK 就直接收数据 */
    got_ok = esp8266_wait_rx("OK", 40);     // 2s
    got_prompt = esp8266_wait_rx(">", 40);  // 2s（OK+'>' 同帧时立刻命中）
    if (!got_ok && !got_prompt) {
        LOGI(LOG_MOD_ESP8266, "wifi: CIPSEND no response: %.80s\r\n",
             strEsp8266_Fram_Record.Data_RX_BUF);
        return 0;
    }
    if (!got_prompt) {
        LOGI(LOG_MOD_ESP8266, "wifi: CIPSEND no '>' prompt, sending anyway: %.80s\r\n",
             strEsp8266_Fram_Record.Data_RX_BUF);
    }
    os_sleep_ms(100);

    /* 清掉提示符，按字节发请求数据（不能走 printf，URL 的 %2F 会被当格式符） */
    esp8266_clear_rx_buf();
    esp8266_send_raw(http_request, dwLen);  // 发送拼接后的请求

    if (!esp8266_wait_rx("SEND OK", 100)) {  // 5s
        LOGI(LOG_MOD_ESP8266, "wifi: CIPSEND no SEND OK: %.120s\r\n",
             strEsp8266_Fram_Record.Data_RX_BUF);
        return 0;
    }
    LOGI(LOG_MOD_ESP8266, "wifi: request sent, waiting reply...\r\n");
    return 1;
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
        /* CWJAP 失败后模组会 busy 十几秒，200ms 立刻重发会一直 busy p... */
        os_sleep_ms(5000);
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
    return ESP8266_Link_Server(enumTCP, WEATHER_SERVER, WEATHER_SERVER_PORT, Single_ID_0);  // 假设这个函数返回bool类型
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
static bool __ESP8266_DHCP_CUR(void)
{
    return ESP8266_DHCP_CUR();
}

static bool __ESP8266_Net_Mode_STA(void)
{
    return ESP8266_Net_Mode_Choose(STA);
}

static bool __ESP8266_Disable_MultipleId(void)
{
    return ESP8266_Enable_MultipleId(DISABLE);
}

void ESP8266_StaTcpClient_Unvarnish_ConfigTest(void)
{
    uint8 byRet = 0;

    esp8266_mutex_ensure();

    LOGI(LOG_MOD_ESP8266, "enable ESP8266 ......\r\n");
    macESP8266_CH_ENABLE();
    ESP8266_USART_RxIrqCtrl(1);
    LOGI(LOG_MOD_ESP8266, "wait ready after CH_PD\r\n");
    os_sleep_ms(2000);
    if (!ESP8266_Cmd("AT", "OK", NULL, 1000)) {
        os_sleep_ms(1000);
        (void)ESP8266_Cmd("AT", "OK", NULL, 1000);
    }

    LOGI(LOG_MOD_ESP8266, "leave AP\r\n");
    ESP8266_leave_AP();
    os_sleep_ms(1500);
    if(!checkEsp8266DhcpStatus())
    {
        byRet = ESP8266_TimeoutHandler(__ESP8266_DHCP_CUR, 10);
        if(!byRet)
        {
            LOGE(LOG_MOD_ESP8266, "ESP8266_DHCP_CUR timeout\r\n");
            ESP8266_WifiApplySet(WIFI_APPLY_FAIL, "DHCP timeout");
            return;
        }
    }

    byRet = checkEsp8266WorkMode();
    if(1 != byRet)
    {
        LOGI(LOG_MOD_ESP8266, "set workScene: STA ......\r\n");
        byRet = ESP8266_TimeoutHandler(__ESP8266_Net_Mode_STA, 10);
        if(!byRet)
        {
            LOGE(LOG_MOD_ESP8266, "ESP8266_Net_Mode_Choose timeout\r\n");
            ESP8266_WifiApplySet(WIFI_APPLY_FAIL, "STA mode timeout");
            return;
        }
    }

    LOGI(LOG_MOD_ESP8266, "connect WiFi:%s......\r\n", getEsp8266Ssid());
    #if 0
    /* 先扫描：+CWJAP:3 = 找不到 AP。busy 时扫描也能把射频拉起来 */
    os_printf("ESP8266: AT+CWLAP (look for ssid)\r\n");
    (void)ESP8266_Cmd("AT+CWLAP", "OK", NULL, 8000);
    if (getEsp8266Ssid()[0] &&
        strstr(strEsp8266_Fram_Record.Data_RX_BUF, getEsp8266Ssid()) == NULL) {
        os_debug("ESP8266: ssid not in CWLAP, radio cannot see AP\r\n");
    }
    #endif
    byRet = ESP8266_JoinAP_timeout(3);
    if(!byRet)
    {
        LOGE(LOG_MOD_ESP8266, "ESP8266_JoinAP_timeout !\r\n");
        ESP8266_WifiApplySet(WIFI_APPLY_FAIL, "join AP timeout");
        return;
    }
    ESP8266_WifiApplySet(WIFI_APPLY_OK, NULL);

    byRet = ESP8266_TimeoutHandler(__ESP8266_Disable_MultipleId, 10);
    if(!byRet)
    {
        LOGE(LOG_MOD_ESP8266, "ESP8266_Enable_MultipleId timeout\r\n");
        return;
    }

    LOGI(LOG_MOD_ESP8266, "connect Server:%s ......\r\n", WEATHER_SERVER);
    byRet = ESP8266_linkServer_timeout(3);
    if(!byRet)
    {
        LOGE(LOG_MOD_ESP8266, "ESP8266_linkServer_timeout !\r\n");
        return;
    }
    /* 天气首次拉取与定时刷新统一由 ESP8266_WeatherPoll 调度，
     * 避免在这里阻塞连接流程；失败会自动退避重试。 */
    gs_weather_last_ok_ms = 0;
    gs_weather_next_poll_ms = 0;
    gs_weather_sent_ms = 0;
    gs_weather_awaiting = 0;
    gs_weather_retry_cnt = 0;
    return;
}


/**
  * @brief  ESP8266 ����Ƿ���յ������ݣ�������Ӻ͵�������
  * @param  ��
  * @retval ��
  */
void ESP8266_CheckRecvDataTest(void)
{
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
    uint16_t rxlen = strEsp8266_Fram_Record.InfBit.FramLength;
    for(i = 0;i < rxlen; i++)
    {
       /* esp8266 data send to usart1 */
       USART_SendData( DEBUG_USART ,strEsp8266_Fram_Record .Data_RX_BUF[i]);    //ת����ESP8266
       while(USART_GetFlagStatus(DEBUG_USART,USART_FLAG_TC)==RESET){}
    }
    if (rxlen >= RX_BUF_MAX_LEN) {
        rxlen = RX_BUF_MAX_LEN - 1U;
    }
    strEsp8266_Fram_Record.Data_RX_BUF[rxlen] = '\0';
    esp8266_mutex_ensure();
    if(gs_esp8266Data_mutex)
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
