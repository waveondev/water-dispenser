#ifndef __WIFI_UTIL_H__
#define __WIFI_UTIL_H__

#include "esp_private/esp_wifi_types_private.h"

#define SSID_STR_MAX 32
#define PASS_STR_MAX 64
#define HOST_IP_MAX 30

typedef struct 
{
    uint32_t used;
    uint8_t ssid[SSID_STR_MAX];           /**string*/
    uint8_t password[PASS_STR_MAX];       /**string*/
    uint8_t host_ip[HOST_IP_MAX];			/**string*/
    unsigned short host_port;

}wifi_info_t;

void wifi_info_print(void);
void wifi_info_init(void);
uint16_t wifi_info_get_hostport(void);
int wifi_info_set_hostport(uint16_t hostport);
const uint8_t* wifi_info_get_hostip(void);
int wifi_info_set_hostip(uint8_t* hostip);
const uint8_t* wifi_info_get_passward(void);
int wifi_info_set_passward(uint8_t* passward);
const uint8_t* wifi_info_get_ssid(void);
int wifi_info_set_ssid(uint8_t* ssid);
uint32_t wifi_info_get_used(void);
int wifi_info_set_used(uint32_t state);
int wifi_info_write(void);
int wifi_info_read(void);
const wifi_info_t* wifi_Info_get(void);

#endif
