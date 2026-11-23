#ifndef __APP_MOTER_H__
#define __APP_MOTER_H__
#include "esp_log.h"
void init_motor_ledc(bool qc_mode) ;
// 모터 제어 함수 (speed: -1023 ~ 1023)


void start_motor_with_boost(int target_percentage, int duration_sec);
void filter_change(void);
void Clean_Mode_Disable(void);
void motor_change(void);
#endif

