#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "app_adc.h"
#include "gpio_util.h"
static const char *TAG = __FILE__;

// ESP32-S3 핀 및 채널 매핑
#define CH0_ADC_UNIT            ADC_UNIT_1
#define CH0_ADC_CHANNEL         ADC_CHANNEL_5  // GPIO 6
// 12dB 감쇄 적용으로 0V ~ 3.3V 풀레인지 측정 가능
#define EXAMPLE_ADC_ATTEN       ADC_ATTEN_DB_12 

// 보정용 핸들 및 플래그
static adc_cali_handle_t cali_ch0_handle = NULL;
static bool do_cali_ch0 = false;
adc_oneshot_unit_handle_t adc0_handle;
#define ADC_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)
#define WIFI_TASK_DELAY_MS(x) (x/portTICK_PERIOD_MS)
// ADC 내부 eFuse 보정(Calibration) 초기화 함수
static bool init_adc_calibration(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#endif

    *out_handle = handle;
    return calibrated;
}

void ADC_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting adc task");

    while (1) {
         int raw_ch0 = 0;
        int mv_ch0 = 0;
        esp_err_t err_ch0;

        // --- 채널 0 (GPIO 6 - ADC1) 읽기 ---
        err_ch0 = adc_oneshot_read(adc0_handle, CH0_ADC_CHANNEL, &raw_ch0);
        if (err_ch0 == ESP_OK) {
            if (do_cali_ch0) {
                adc_cali_raw_to_voltage(cali_ch0_handle, raw_ch0, &mv_ch0);
                ESP_LOGI(TAG, "GPIO  6 (ADC1) -> Raw: %4d | Voltage: %4d mV (%.2f V)\r\n", raw_ch0, mv_ch0, (float)mv_ch0 / 1000.0f);
            } else {
                ESP_LOGI(TAG, "GPIO  6 (ADC1) -> Raw: %4d (No Calibration)\r\n", raw_ch0);
            }
        } else {
            ESP_LOGE(TAG, "Failed to read GPIO 6 (%s)", esp_err_to_name(err_ch0));
        }
       
          ESP_LOGI(TAG, "read GPIO 41 (%d)",  gpio_read(EX_POWER));
        printf("-------------------------------------------------------------------\n");
        
        // 1초(1000ms) 대기 후 다시 측정
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
}
void adc_init(void) {
    // -------------------------------------------------------------
    // 1. ADC 유닛 핸들 생성 및 초기화 (v6.0+ 문법 적용)
    // -------------------------------------------------------------

    
// 구조체 타입은 _cfg_t가 맞고, 내부 멤버는 .unit_id 입니다.
    adc_oneshot_unit_init_cfg_t init_cfg0 = { .unit_id = ADC_UNIT_1 };
 
    
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg0, &adc0_handle));


    // -------------------------------------------------------------
    // 2. 각 채널별 세부 설정 (v6.0+ 문법 적용)
    // -------------------------------------------------------------
    // 구조체명을 _cfg_t로 변경합니다. (.bitwidth 와 .atten 필드는 그대로 유지)
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, 
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc0_handle, CH0_ADC_CHANNEL, &chan_cfg));


    // -------------------------------------------------------------
    // 3. 정확한 mV 변환을 위한 보정 스키마 등록 및 이하 루프 코드는 동일...
    // -------------------------------------------------------------
    // 3. 정확한 mV 변환을 위한 보정 스키마 등록
    // -------------------------------------------------------------
    do_cali_ch0 = init_adc_calibration(CH0_ADC_UNIT, CH0_ADC_CHANNEL, EXAMPLE_ADC_ATTEN, &cali_ch0_handle);


    ESP_LOGI(TAG, "Dual ADC Initialized successfully (GPIO 6 & GPIO 41).");

    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreatePinnedToCore(
            ADC_task,                  // 태스크 함수
            "adc_task",                // 태스크 이름
            ADC_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 2,      // 우선순위
            &xHandle,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        ESP_LOGE(TAG, "Error creating adc_task on Core 1");
    }

}


