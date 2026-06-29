#ifndef __APP_LED_H__
#define __APP_LED_H__
#include "esp_log.h"

#define PAIRING_BIT    (1<<15)
#define TOF_DETECT_BIT (1<<14)


bool led_moter_enable(void);
void led_bit_disable(uint16_t disable);
void led_bit_enable(uint16_t enable);
void init_led_strip(void);
void set_led_clear(void) ;
void set_rgb_led(uint8_t R, uint8_t G, uint8_t B, uint8_t W);
void LED_task_init(void);

#endif
