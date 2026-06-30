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

#ifndef EX_POWER
  #define EX_POWER 41
#endif
#define PIN_PUMP_ADC  6
#define PIN_PUMP_PWM  15
#ifndef PIN_HX711_DOUT
  #define PIN_HX711_DOUT 40
#endif
#ifndef PIN_HX711_SCK
  #define PIN_HX711_SCK 39
#endif

#define PIN_PKEY_STAT 9  // PKEY_STAT
#define PIN_MCU_PW_HOLD     8
#define PIN_DEVICE_PW_HOLD  3

#ifndef PIN_TOF0_I2C_SDA
#define PIN_TOF0_I2C_SDA 12
#endif
#ifndef PIN_TOF0_I2C_SCL
#define PIN_TOF0_I2C_SCL 11
#endif
#ifndef PIN_TOF0_INT
#define PIN_TOF0_INT 13
#endif
#ifndef PIN_TOF1_I2C_SDA
#define PIN_TOF1_I2C_SDA 47
#endif
#ifndef PIN_TOF1_I2C_SCL
#define PIN_TOF1_I2C_SCL 21
#endif
#ifndef PIN_TOF1_INT
#define PIN_TOF1_INT 48
#endif
#define PIN_LED_RGBW 14


#define PIN_SENS_B 5   //10

#define PIN_SENS_A 4   //11

void gpio_init(gpio_num_t num, gpio_mode_t mode, gpio_int_type_t int_type,gpio_isr_t func);
void gpio_toggle(gpio_num_t pin);
void gpio_setpin(gpio_num_t pin);
void gpio_resetpin(gpio_num_t pin);
int gpio_read(gpio_num_t pin) ;
#endif
