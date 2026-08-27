#ifndef __APP_LED_H__
#define __APP_LED_H__
#include "esp_log.h"

#define PAIRING_BIT         (1<<15)
#define OTA_START_BIT       (1<<14)
#define HARDWARE_ERR_BIT    (1<<13)
#define TOF_DETECT_BIT      (1<<12)
#define CLEAN_MODE_BIT      (1<<11)
#define WATER_LOW_BIT       (1<<10)
#define WATER_EMPTY_BIT     (1<<9)
#define LOADCELL_ERR_BIT    (1<<8)
#define FILTER_WATER_BIT    (1<<7)
#define FILTER_DEBRIS_BIT   (1<<6)
#define PUMP_ERR_BIT        (1<<5)

void LED_Bright_Set(uint8_t value);
bool TOF_enable(void);
bool led_bit_status(uint16_t status);
void led_bit_disable(uint16_t disable);
void led_bit_enable(uint16_t enable);
void init_led_strip(void);
void set_led_clear(void) ;
void set_rgb_len_no_Breathing(uint8_t R, uint8_t G, uint8_t B, uint8_t W);
void Breathing_Setup(uint8_t enable, uint8_t step, 
                            uint8_t min_bright,  // 💡 최소 밝기 (0~255)
                            uint8_t max_bright,  // 💡 최대 밝기 (0~255)
                            uint8_t target_r,
                            uint8_t target_g,
                            uint8_t target_b,
                            uint8_t target_w);
void Breathing_Setup_Debug(uint8_t enable, uint8_t step, 
                            uint8_t min_bright,  // 💡 최소 밝기 (0~255)
                            uint8_t max_bright,  // 💡 최대 밝기 (0~255)
                            uint8_t target_r,
                            uint8_t target_g,
                            uint8_t target_b,
                            uint8_t target_w);


void LED_task_init(void);
bool ota_enable(void);
bool hardware_error_enable(void);
bool Clean_enable(void);
bool Water_low_enable(void);
bool Water_empty_enable(void);
bool Loadcell_error_enable(void);
bool Filter_water_enable(void);
bool Filter_debris_enable(void);
bool Pump_error_enable(void);
void wifi_connect_success(void);
#endif
