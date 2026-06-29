#ifndef __WIFI_TASK_H__
#define __WIFI_TASK_H__
void Wifi_Disconnect(void);
void wifi_init(void); 
uint16_t wifi_scan_start(void);
void Wifi_Connect(uint8_t* target_ssid, uint8_t* target_password);
#endif

