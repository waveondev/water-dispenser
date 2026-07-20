#ifndef __AWS_IOT_TASK_H__
#define __AWS_IOT_TASK_H__
#include "tx_mqtt.h"
/**
 * @brief AWS IoT 및 Fleet Provisioning을 수행하는 백그라운드 태스크를 생성합니다.
 */
void aws_iot_task_init(void);
void mqtt_queue_send(messege_tx_mqtt_cmd_e cmd);
#endif /* __AWS_IOT_TASK_H__ */