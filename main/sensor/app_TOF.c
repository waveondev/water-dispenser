#include "app_TOF.h"
#include "gpio_util.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"

#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "debug_cli.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform.h"
#include "app_config_flash.h"
#include "aws_iot_task.h"
#include "ble_tracker_id.h"


#include <stdio.h>
#include <string.h>
#include "esp_adc/adc_oneshot.h"
#include "app_adc.h"
#include "driver/ledc.h"
#include "opmode_task.h"
#include "app_HX711.h"

static const char *TAG = "IR_TEST";
static int ir_count = 0;
static int animal_detect = 0;




// ================= 설 정 값 =================
// TX (발광부) 설정

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_1
#define LEDC_DUTY           127             // 50% Duty Cycle (8비트 기준 127)
#define LEDC_FREQUENCY      38000           // 38kHz

// RX (수광부) 설정
#define IR_RX_ADC_UNIT      ADC_UNIT_1      
#define IR_RX_ADC_CHANNEL   ADC_CHANNEL_6   // GPIO34에 해당 (보드에 맞게 변경)
#define DETECT_THRESHOLD    300             // 물체 감지 임계값 (환경에 맞게 튜닝 필요)

// ADC 핸들 전역 변수
adc_oneshot_unit_handle_t adc_handle;
int ir_cal = 0;
uint8_t cal_index = 0;
uint32_t cal_left = 0;
uint32_t cal_right = 0;
// ================= 함수 구현 =================


bool detect_object(void);
int VL53L0X_Detect(bool all_state)
{
    return animal_detect;
}
int left_value = 0;
int right_value = 0;
void currunt_ir_value(int* left, int* right)
{
    int left_delta = left_value - cal_left;
    if(left_delta < 0)
        left_delta = 0;
    int right_delta = right_value - cal_right;
    if(right_delta < 0)
        right_delta = 0;

    //left_value = left_delta;
    //right_value = right_delta;

    *left = left_value;
    *right = right_value;
}
void detect_task(void)
{
    app_config_t* app_config = get_app_config();
    if (left_value > app_config->tof_sense_threshold_l || right_value > app_config->tof_sense_threshold_r) {
        if(ir_count < 10)
            ir_count++;
        
    } else {
        if(ir_count)
            ir_count--;
    }
    if(GetTracker_Id_active())
    {
         animal_detect |= TRACKER_ACCESS;
    }
    else
    {
        animal_detect &= ~TRACKER_ACCESS;
    }
    #if 1
    if (ir_count >= 5) 
    {
        animal_detect |= IR_ACCESS;
    }
    else
    {
        animal_detect &= ~IR_ACCESS;
    }
}


void VL53L0X_Sensing(void)
{
    ADC_Sensing();
    detect_object();
    detect_task();
}

// 1. 발광부(TX) PWM 초기화
bool TOF_VL53L0X_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = LEDC_FREQUENCY, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = IR_ENABLE,
        .duty           = 0, // 처음엔 꺼둠
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
    app_facto_config_t* facto_config = get_facto_config();
    cal_left = facto_config->tof_sense_cal_l;
    cal_right = facto_config->tof_sense_cal_r;
    return true;
}

void ir_tx_enable(bool enable) {
    if (enable) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY);
    } else {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    }
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void ir_calibration_enable(int dest)
{
    ir_cal = dest;
    cal_index = 0;
}

// 4. 핵심 로직: 물체 감지 (배경광 빼기)
bool detect_object(void) {
    int val_left_off = 0;
    int val_left_on = 0;
    int val_right_off = 0;
    int val_right_on = 0;
 
    // 단계 1: TX OFF 상태에서 주변 태양광(노이즈) 측정
    ir_tx_enable(false);
    IR_SenSing();
    

    val_left_off = GetIR_LEFT();
    val_right_off = GetIR_RIGHT();
    // 단계 2: TX ON 상태에서 (태양광 + 반사된 IR 빛) 측정
    ir_tx_enable(true);
    vTaskDelay(5);
    
    IR_SenSing();
    val_left_on = GetIR_LEFT();
    val_right_on = GetIR_RIGHT();

    // 단계 3: TX 다시 OFF
    ir_tx_enable(false);

    // 단계 4: 차이값 계산 (절댓값)
    left_value = (val_left_on - val_left_off -(val_left_off/50)*4);
    if(left_value < 0)
    left_value = 0;

    right_value = (val_right_on - val_right_off - (val_right_off/50)*4);
    if(right_value < 0)
    right_value = 0;
    DBG_Resister_t *DBG_Resister = Debug_Get();
    if (DBG_Resister && DBG_Resister->TOF) {
        int left_delta = left_value - cal_left;
        if(left_delta < 0)
            left_delta = 0;
        int right_delta = right_value - cal_right;
        if(right_delta < 0)
            right_delta = 0;
        APP_String_printf( "LEFT  - OFF: %d | ON: %d | Delta: %d\r\n", val_left_off, val_left_on, left_delta);
        APP_String_printf( "RIGHT - OFF: %d | ON: %d | Delta: %d\r\n", val_right_off, val_right_on, right_delta);
    }  
    if(ir_cal)
    {
        if(ir_cal & 0x01)
        {
            if(cal_right < right_value)
            cal_right = right_value;
        }
        if(ir_cal & 0x02)
        {
            if(cal_left < left_value)
            cal_left = left_value;
        }
        
        cal_index++;
        if(cal_index >= 10)
        {
            ir_cal = 0;
            app_facto_config_t* facto_config = get_facto_config();
            facto_config->tof_sense_cal_l = cal_left;
            facto_config->tof_sense_cal_r = cal_right;
            facto_nvs_save_set();
            APP_String_printf( "IR LEFT = %d IR RIGHT: %d\r\n", cal_left, cal_right);
        }

    }
  

    return false;
}   

#else

#endif