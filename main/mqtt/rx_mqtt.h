#ifndef __RX_MQTT_H__
#define __RX_MQTT_H__

#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_operations.h"
void mqtt_rx_messege(MQTTPublishInfo_t * pPublishInfo);

bool mqtt_subscribe_init(void);

#endif

