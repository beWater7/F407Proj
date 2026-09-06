#ifndef _DRV_ESP8266_H
#define _DRV_ESP8266_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP8266_SSID_MAX
#define ESP8266_SSID_MAX 32
#endif
#ifndef ESP8266_PSK_MAX
#define ESP8266_PSK_MAX  63
#endif

typedef enum {
    WIFI_APPLY_IDLE = 0,
    WIFI_APPLY_PENDING,
    WIFI_APPLY_CONNECTING,
    WIFI_APPLY_OK,
    WIFI_APPLY_FAIL
} wifi_apply_state_t;

void ESP8266_Init(void);
void ESP8266_ProcessPendingWifiReconfig(void);
void ESP8266_CheckRecvDataTest(void);
void ESP8266_WeatherPoll(void);
void ESP8266_WeatherStats(uint32_t *ok, uint32_t *fail, uint32_t *last_ok_ms);
void ESP8266_WifiScan(void);
void ESP8266_WifiStatus(void);
void ESP8266_RequestWifiReconfig(void);
int ESP8266_WifiCredSave(void);
char *getEsp8266Ssid(void);
char *getEsp8266Psk(void);
void setEsp8266Ssid(char *ssid);
void setEsp8266Psk(char *psk);
void getWeather(char *data, uint8_t len);
int getTemperature(void);
const char *ESP8266_WifiApplyStateStr(void);
const char *ESP8266_WifiApplyError(void);

#ifdef __cplusplus
}
#endif

#endif /* _DRV_ESP8266_H */
