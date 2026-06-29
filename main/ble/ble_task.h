#ifndef __BLE_TASK_H__
#define __BLE_TASK_H__

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Attributes State Machine */

bool ble_send_data_to_queue(const uint8_t *data, uint16_t len);
void ble_task_init(void);
void motion_msg_send(uint8_t cmd);
void send_motion(void);

#endif
