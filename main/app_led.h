#ifndef __APP_LED_H__
#define __APP_LED_H__
#include "stdint.h"
void init_led_strip(void);
void set_led_clear(void) ;
void set_rgb_led(uint8_t R, uint8_t G, uint8_t B, uint8_t W);
#endif
