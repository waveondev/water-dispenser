#ifndef __APP_TOF_H__
#define __APP_TOF_H__

#include "esp_log.h"
int VL53L0X_Detect(bool all_state);
bool TOF_VL53L0X_init(void);
void VL53L0X_Sensing(void);
void currunt_ir_value(int* left, int* right);
#define TRACKER_ACCESS (1<<1)
#define IR_ACCESS (1<<0)

#endif
