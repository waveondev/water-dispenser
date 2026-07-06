#ifndef __WIFI_TASK_H__
#define __WIFI_TASK_H__

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// 이벤트 그룹과 비트 정의를 헤더로 이동시켜 다른 파일에서도 볼 수 있게 공유
extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

void Wifi_Disconnect(void);
void wifi_init(void); 
uint16_t wifi_scan_start(void);
void Wifi_Connect(const char* target_ssid, const char* target_password);
#endif

