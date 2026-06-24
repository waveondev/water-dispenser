#ifndef __BLE_UTIL_H__
#define __BLE_UTIL_H__

#include "esp_private/esp_wifi_types_private.h"

#define BLE_MAC_MAX 6

typedef struct 
{
    uint8_t fail_time[8];
}ble_info_t;

void ble_info_print(void);
void ble_info_init(void);
int ble_info_set_fail_time(uint8_t* failtime);
const uint8_t* ble_info_get_fail_time(void);
int ble_info_write(void);
int ble_info_read(void);
int ble_mac_write(uint8_t* mac);
int ble_mac_read(uint8_t* mac);
void ble_mac_clear(void);

#endif
