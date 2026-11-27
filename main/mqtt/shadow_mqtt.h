#ifndef __SHADOW_MQTT_H__
#define __SHADOW_MQTT_H__
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_operations.h"


typedef enum{
    CURRUNT_MODE,
    FILTER_ALERT_ENABLE,
    SLEEP_MODE,
    TARE_REQUEST,
    WATER_FILETER_RESET,
    DEBRIS_FILTER_RESET,
    REBOOT_REQUEST,
}shadow_set_e;
void shadow_mqtt_update_delta_rx_messege(MQTTPublishInfo_t * pPublishInfo);
void shadow_mqtt_get_accepted_rx_messege(MQTTPublishInfo_t * pPublishInfo);
bool Get_Accepted_Init_state(void);
void shadow_mqtt_send(shadow_set_e state, void* data, bool server);
#endif