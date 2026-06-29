#ifndef __APP_HX711_H__
#define __APP_HX711_H__

#include "esp_log.h"
void HX711_cal_init(void);
bool HX711_init(void);
void HX711_Sensing(void);
#endif

