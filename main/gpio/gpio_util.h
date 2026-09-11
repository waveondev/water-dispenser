#ifndef __GPIO_UTIL_H__
#define __GPIO_UTIL_H__

#include "driver/gpio.h"

//
//  LED State
//
typedef enum {
  LCS_BLE_EN_ADV_WIFI_NOT_WORKING = 0,
  LCS_BLE_EN_ADV_WIFI_EN_WORKING,
  LCS_IDLE_BLE_CONNECTED,
  LCS_WORKING_BLE_CONNECTED,
} LED_States;
#define BLINK_GPIO   5  // 네오픽셀 데이터 선이 연결된 GPIO 핀 번호

#define PIN_PUMP_ADC  0
#define PIN_PUMP_PWM  1
#ifndef PIN_HX711_DOUT
  #define PIN_HX711_DOUT 19
#endif
#ifndef PIN_HX711_SCK
  #define PIN_HX711_SCK 18
#endif

#define PIN_PKEY_STAT 7  // PKEY_STAT

#define IR_ENABLE 10
#define IR_LEFT 3
#define IR_RIGHT 4


void gpio_init(gpio_num_t num, gpio_mode_t mode, gpio_int_type_t int_type,gpio_isr_t func);
void gpio_toggle(gpio_num_t pin);
void gpio_setpin(gpio_num_t pin);
void gpio_resetpin(gpio_num_t pin);
int gpio_read(gpio_num_t pin) ;
#endif
