#if 0
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "app_adc.h"
#include "debug_cli.h"
static const char *TAG = "ADC_MIXED";
#include "gpio_util.h"
#define ADC_LEN 3

#define ADC_SAMPLE_NUM      256

// 1. ADC1 DMA용 채널 설정: GPIO3 (CH3), GPIO4 (CH4)
static adc_channel_t adc1_dma_channels[ADC_LEN] = {ADC_CHANNEL_3, ADC_CHANNEL_4, ADC_CHANNEL_0};

// 2. ADC2 Oneshot용 핸들 및 채널: GPIO5 (ADC2_CH0)

static adc_continuous_handle_t adc_handle = NULL;


static int ir_left_mv = 0;
static int ir_right_mv = 0;
static int motor_adc = 0;

int GetMotor_adc(void)
{
    return motor_adc;
}
int GetIR_LEFT(void)
{
    return ir_left_mv;
}
int GetIR_RIGHT(void)
{
    return ir_right_mv;
}


#define FILTER_WINDOW_SIZE 10

// 3개 채널에 대한 필터링 버퍼 및 관리 변수
static uint32_t ch3_buffer[FILTER_WINDOW_SIZE] = {0};
static uint32_t ch4_buffer[FILTER_WINDOW_SIZE] = {0};
static uint32_t ch0_buffer[FILTER_WINDOW_SIZE] = {0};

static uint8_t adc_index = 0;
static uint8_t adc_count = 0; // 초기 버퍼 채움 상태 관리 (0 ~ 10)

void ADC_Sensing(void)
{
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle is NULL!");
        return;
    }
    int current_level = gpio_get_level(PIN_PUMP_PWM);

    // Task Stack 보호
    static uint8_t dma_result[ADC_SAMPLE_NUM * SOC_ADC_DIGI_DATA_BYTES_PER_CONV];
    uint32_t ret_num = 0;
    DBG_Resister_t *DBG_Resister = Debug_Get();

    esp_err_t start_err = adc_continuous_start(adc_handle);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "ADC continuous start failed: %s", esp_err_to_name(start_err));
        return;
    }



    // 기존 딜레이 유지
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_err_t ret = adc_continuous_read(adc_handle, dma_result, sizeof(dma_result), &ret_num, pdMS_TO_TICKS(100));
    adc_continuous_stop(adc_handle);
adc_continuous_flush_pool(adc_handle);
    uint32_t raw_val_ch3 = 0, raw_val_ch4 = 0, raw_val_ch0 = 0;
    uint32_t cnt_ch3 = 0, cnt_ch4 = 0, cnt_ch0 = 0;

    if (ret == ESP_OK && ret_num > 0) {
        for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_DATA_BYTES_PER_CONV) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&dma_result[i];
            uint32_t chan = p->type2.channel;
            uint32_t data = p->type2.data;

            if (chan == ADC_CHANNEL_3) {
                raw_val_ch3 += data;
                cnt_ch3++;
            } else if (chan == ADC_CHANNEL_4) {
                raw_val_ch4 += data;
                cnt_ch4++;
            } else if (chan == ADC_CHANNEL_0) {
                raw_val_ch0 += data;
                cnt_ch0++;
            }
        }

        // DMA 샘플 1회 수집분에 대한 평균 계산 (Raw 값)
        if (cnt_ch3) raw_val_ch3 /= cnt_ch3;
        if (cnt_ch4) raw_val_ch4 /= cnt_ch4;
        if (cnt_ch0) raw_val_ch0 /= cnt_ch0; 
    }

    // ==========================================
    // [전 채널 원형 버퍼 이동 평균 필터링]
    // ==========================================
    // 1. 버퍼에 최신 Raw 데이터 저장
    ch3_buffer[adc_index] = raw_val_ch3;
    ch4_buffer[adc_index] = raw_val_ch4;
    ch0_buffer[adc_index] = raw_val_ch0;

    // 2. 인덱스 및 카운터 업데이트
    adc_index = (adc_index + 1) % FILTER_WINDOW_SIZE;
    if (adc_count < FILTER_WINDOW_SIZE) {
        adc_count++;
    }

    // 3. 최근 10개 데이터의 합산 계산
    uint32_t sum_ch3 = 0, sum_ch4 = 0, sum_ch0 = 0;
    for (int i = 0; i < adc_count; i++) {
        sum_ch3 += ch3_buffer[i];
        sum_ch4 += ch4_buffer[i];
        sum_ch0 += ch0_buffer[i];
    }

    // 4. 최종 필터링 값 산출 및 전역 변수 업데이트
    ir_left_mv  = sum_ch3 / adc_count;
    ir_right_mv = sum_ch4 / adc_count;
    if(current_level)
        motor_adc   = sum_ch0 / adc_count;


       // ESP_LOGI(TAG, "%d",current_level);
    // 디버그 로그
    if (DBG_Resister && DBG_Resister->adc) {
        ESP_LOGI(TAG, "[DMA Filtered] CH3: %lu (Raw: %lu) | CH4: %lu (Raw: %lu) | CH0: %lu (Raw: %lu)", 
                 ir_left_mv, raw_val_ch3, 
                 ir_right_mv, raw_val_ch4, 
                 motor_adc, raw_val_ch0);
    }
}



#include "driver/gpio.h"
void adc_init(void) {

    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = ADC_SAMPLE_NUM * SOC_ADC_DIGI_DATA_BYTES_PER_CONV,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &adc_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 20 * 1000,           // 20kHz
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,    // ADC1 전용
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2, // ESP32-C3 구조체 포맷
    };

    adc_digi_pattern_config_t adc_pattern[ADC_LEN] = {0};
    dig_cfg.pattern_num = ADC_LEN; // CH3, CH4 2개 채널

    for (int i = 0; i < ADC_LEN; i++) {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = adc1_dma_channels[i] & 0x7;
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    dig_cfg.adc_pattern = adc_pattern;
    
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));
   // ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    ESP_LOGI(TAG, "ADC1 DMA (GPIO3, GPIO4) Initialized & Started.");

}

#else
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h" // Oneshot 헤더 포함
#include "app_adc.h"
#include "debug_cli.h"
#include "gpio_util.h"

static const char *TAG = "ADC_MIXED";
#define ADC_SAMPLE_NUM      64

// 1. DMA용 채널 설정: 이제 CH0(모터)만 남김 (필요시 채널 수 조정)
#define ADC_LEN 1
static adc_channel_t adc1_dma_channels[ADC_LEN] = {ADC_CHANNEL_0};

// 2. ADC Oneshot용 핸들 선언 (CH3: GPIO3, CH4: GPIO4)
static adc_oneshot_unit_handle_t adc1_oneshot_handle = NULL;

static adc_continuous_handle_t adc_handle = NULL;

static int ir_left_mv = 0;
static int ir_right_mv = 0;
static int motor_adc = 0;

int GetMotor_adc(void) { return motor_adc; }
int GetIR_LEFT(void) { return ir_left_mv; }
int GetIR_RIGHT(void) { return ir_right_mv; }

#define FILTER_WINDOW_SIZE 10

// 필터링 버퍼

static uint32_t ch0_buffer[FILTER_WINDOW_SIZE] = {0};

static uint8_t adc_index = 0;
static uint8_t adc_count = 0;

#define IR_ADC_SAMPLES 32

static int IR_ADC_Average(adc_channel_t channel)
{
    int sum = 0;
    int raw = 0;

    for (int i = 0; i < IR_ADC_SAMPLES; i++) {
        adc_oneshot_read(adc1_oneshot_handle, channel, &raw);
        sum += raw;
    }

    return sum / IR_ADC_SAMPLES;
}

void IR_SenSing(void)
{
    ir_left_mv  = IR_ADC_Average(ADC_CHANNEL_3);
    ir_right_mv = IR_ADC_Average(ADC_CHANNEL_4);
}

void ADC_Sensing(void)
{
    if (adc_handle == NULL || adc1_oneshot_handle == NULL) {
        ESP_LOGE(TAG, "ADC handles are not initialized!");
        return;
    }
    int current_level = gpio_get_level(PIN_PUMP_PWM);

    // ==========================================
    // 2. CH0 채널 기존 DMA 연속 모드로 읽기
    // ==========================================
    static uint8_t dma_result[ADC_SAMPLE_NUM * SOC_ADC_DIGI_DATA_BYTES_PER_CONV];
    uint32_t ret_num = 0;
    DBG_Resister_t *DBG_Resister = Debug_Get();

    esp_err_t start_err = adc_continuous_start(adc_handle);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "ADC continuous start failed: %s", esp_err_to_name(start_err));
        return;
    }
    vTaskDelay(5);
    esp_err_t ret = adc_continuous_read(adc_handle, dma_result, sizeof(dma_result), &ret_num, pdMS_TO_TICKS(100));
    adc_continuous_stop(adc_handle);
    adc_continuous_flush_pool(adc_handle);
    uint32_t raw_val_ch0 = 0;
    uint32_t cnt_ch0 = 0;

    if (ret == ESP_OK && ret_num > 0) {
        for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_DATA_BYTES_PER_CONV) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&dma_result[i];
            uint32_t chan = p->type2.channel;
            uint32_t data = p->type2.data;

            if (chan == ADC_CHANNEL_0) {
                raw_val_ch0 += data;
                cnt_ch0++;
            }
        }
        if (cnt_ch0) raw_val_ch0 /= cnt_ch0; 
    }

    // ==========================================
    // [전 채널 원형 버퍼 이동 평균 필터링]
    // ==========================================

    ch0_buffer[adc_index] = raw_val_ch0;

    adc_index = (adc_index + 1) % FILTER_WINDOW_SIZE;
    if (adc_count < FILTER_WINDOW_SIZE) {
        adc_count++;
    }

    uint32_t sum_ch0 = 0;
    for (int i = 0; i < adc_count; i++) {

        sum_ch0 += ch0_buffer[i];
    }

    if(current_level)
        motor_adc   = sum_ch0 / adc_count;

    if (DBG_Resister && DBG_Resister->adc) {
        ESP_LOGI(TAG, "[Mixed Filtered] CH3(Oneshot): %lu | CH4(Oneshot): %lu| CH0(DMA): %lu (Raw: %lu)", 
                 ir_left_mv,  
                 ir_right_mv,  
                 motor_adc, raw_val_ch0);
    }
}

void adc_init(void) {
    // ------------------------------------------
    // 1. ADC Oneshot 초기화 (CH3, CH4 용)
    // ------------------------------------------
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_oneshot_handle));

    // CH3 (GPIO3) 감쇠(Atten) 설정
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // 기존 코드와 동일한 12dB (약 0~3.3V)
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_oneshot_handle, ADC_CHANNEL_3, &config));
    // CH4 (GPIO4) 감쇠(Atten) 설정
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_oneshot_handle, ADC_CHANNEL_4, &config));

    // ------------------------------------------
    // 2. ADC Continuous(DMA) 초기화 (CH0 용)
    // ------------------------------------------
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = ADC_SAMPLE_NUM * SOC_ADC_DIGI_DATA_BYTES_PER_CONV,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &adc_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 20 * 1000,          // 20kHz
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,    // ADC1 전용
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2, // ESP32-C3 구조체 포맷
    };

    adc_digi_pattern_config_t adc_pattern[ADC_LEN] = {0};
    dig_cfg.pattern_num = ADC_LEN; // CH0 1개 채널만 포함

    for (int i = 0; i < ADC_LEN; i++) {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = adc1_dma_channels[i] & 0x7;
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    dig_cfg.adc_pattern = adc_pattern;
    
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));

    ESP_LOGI(TAG, "ADC Init Completed: CH3, CH4 (Oneshot) / CH0 (Continuous DMA).");
}



#endif
