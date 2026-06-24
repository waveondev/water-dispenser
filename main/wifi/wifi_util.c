#include "wifi_util.h"
#include "spiffs_util.h"
#include "string.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "FreeRTOS_CLI.h"
static wifi_info_t wifi_user_info;

static const char *TAG = __FILE__;
void wifi_info_print(void)
{
    wifi_info_read();
    APP_String_printf("[%s] = ",TAG);
    APP_String_printf("used = %ld\r\n" , wifi_user_info.used);
    APP_String_printf("[%s] = ",TAG);    
    APP_String_printf("ssid = %s\r\n" , wifi_user_info.ssid);
    APP_String_printf("[%s] = ",TAG);    
    APP_String_printf("password = %s\r\n" , wifi_user_info.password);
    APP_String_printf("[%s] = ",TAG);    
    APP_String_printf("host_ip = %s\r\n" , wifi_user_info.host_ip);
    APP_String_printf("[%s] = ",TAG);    
    APP_String_printf("host_port = %d\r\n" , wifi_user_info.host_port);
}


const wifi_info_t* wifi_Info_get(void)
{
    return &wifi_user_info;
}

int wifi_info_read(void)
{
   return spiffs_byte_read(WIFI_INFO_PATH, (char*)&wifi_user_info, sizeof(wifi_info_t));
}

int wifi_info_write(void)
{
    return spiffs_byte_write(WIFI_INFO_PATH, (char*)&wifi_user_info, sizeof(wifi_info_t));
}

int wifi_info_set_used(uint32_t state)
{
    wifi_user_info.used = state;
    return wifi_info_write();
}

uint32_t wifi_info_get_used(void)
{
    return wifi_user_info.used;
}

int wifi_info_set_ssid(uint8_t* ssid)
{
    memcpy(wifi_user_info.ssid,ssid,SSID_STR_MAX);
    return wifi_info_write();
}

const uint8_t* wifi_info_get_ssid(void)
{
    return wifi_user_info.ssid;
}

int wifi_info_set_passward(uint8_t* passward)
{
    memcpy(wifi_user_info.password,passward,PASS_STR_MAX);
    return wifi_info_write();
}

const uint8_t* wifi_info_get_passward(void)
{
    return wifi_user_info.password;
}

int wifi_info_set_hostip(uint8_t* hostip)
{
    memcpy(wifi_user_info.host_ip,hostip,HOST_IP_MAX);
    return wifi_info_write();
}

const uint8_t* wifi_info_get_hostip(void)
{
    return (const uint8_t*)wifi_user_info.host_ip;
}

int wifi_info_set_hostport(uint16_t hostport)
{
    wifi_user_info.host_port = hostport;
    return wifi_info_write();
}

uint16_t wifi_info_get_hostport(void)
{
    return wifi_user_info.host_port;
}

void wifi_info_init(void)
{
    if(wifi_info_read() == -1)
        wifi_info_write();
}

















