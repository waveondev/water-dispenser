#include "ble/ble_util.h"
#include "spiffs_util.h"
#include "string.h"
#include "esp_system.h"
#include "FreeRTOS_CLI.h"
#include "esp_log.h"
static ble_info_t ble_user_info;
static const char *TAG = __FILE__;
void ble_info_print(void)
{
    uint8_t mac[6];
    ble_mac_read(mac);
    ble_info_read();
    APP_String_printf("[%s] = ",TAG);
    APP_String_printf("mac = ");
    for(int i=0;i < BLE_MAC_MAX;i++)
        APP_String_printf("%02x",mac[i]);
    APP_String_printf("\r\n");
    APP_String_printf("[%s] = ",TAG);
    APP_String_printf("fail_time = ");
    for(int i=0;i<sizeof(ble_user_info.fail_time);i++)
        APP_String_printf("%02x",ble_user_info.fail_time[i]);
    APP_String_printf("\r\n");
}
int ble_mac_read(uint8_t* mac)
{
    if(mac == NULL)
       return -1;
    return spiffs_byte_read(BLE_MAC_ADDR_PATH, (char*)mac, BLE_MAC_MAX);
}

int ble_mac_write(uint8_t* mac)
{
    if(mac == NULL)
        return -1;
    return spiffs_byte_write(BLE_MAC_ADDR_PATH, (char*)mac, BLE_MAC_MAX);
}

void ble_mac_clear(void)
{
    spiffs_delete(BLE_MAC_ADDR_PATH);
}

int ble_info_read(void)
{
   return spiffs_byte_read(BLE_INFO_PATH, (char*)&ble_user_info, sizeof(ble_info_t));
}

int ble_info_write(void)
{
    return spiffs_byte_write(BLE_INFO_PATH, (char*)&ble_user_info, sizeof(ble_info_t));
}

const uint8_t* ble_info_get_fail_time(void)
{
    ble_info_read();
    return ble_user_info.fail_time;
}

int ble_info_set_fail_time(uint8_t* failtime)
{
    memcpy(ble_user_info.fail_time,failtime,sizeof(ble_user_info.fail_time));
    return ble_info_write();
}


void ble_info_init(void)
{
    if(ble_info_read() == -1)
        ble_info_write();
}





