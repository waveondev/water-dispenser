#ifndef __TX_MQTT_H__
#define __TX_MQTT_H__

#include "esp_log.h"


typedef enum{
    MESSEGE_REGISTRATION = 0,
    MESSEGE_BOOT,
    MESSEGE_ACCESS,
    MESSEGE_DRINK,
    MESSEGE_DIAGNOSTICS,
    MESSEGE_HEALTH,
}messege_tx_mqtt_cmd_e;

void Send_cJSON_Messege(messege_tx_mqtt_cmd_e cmd);



#endif

