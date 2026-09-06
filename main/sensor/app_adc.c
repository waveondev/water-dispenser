#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "ADC_MIXED";

#define ADC_SAMPLE_NUM      256

// 1. ADC1 DMA용 채널 설정: GPIO3 (CH3), GPIO4 (CH4)
static adc_channel_t adc1_dma_channels[2] = {ADC_CHANNEL_3, ADC_CHANNEL_4};

// 2. ADC2 Oneshot용 핸들 및 채널: GPIO5 (ADC2_CH0)
static adc_oneshot_unit_handle_t adc2_oneshot_handle = NULL;
static adc_continuous_handle_t adc_handle = NULL;

#define ADC2_GPIO5_CHANNEL  ADC_CHANNEL_0

// ==========================================
// [Part 1] ADC2 (GPIO 5) Oneshot 초기화
// ==========================================
static void init_adc2_oneshot(void)
{
    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config2, &adc2_oneshot_handle));

    adc_oneshot_chan_cfg_t config2 = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // 0~3.3V 측정 범위
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_oneshot_handle, ADC2_GPIO5_CHANNEL, &config2));
    
    ESP_LOGI(TAG, "ADC2 Oneshot (GPIO5) Initialized.");
}

// ==========================================
// [Part 2] ADC1 (GPIO 3, 4) DMA Continuous 초기화
// ==========================================
static adc_continuous_handle_t init_adc1_dma(void)
{
    adc_continuous_handle_t adc_handle = NULL;

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

    adc_digi_pattern_config_t adc_pattern[2] = {0};
    dig_cfg.pattern_num = 2; // CH3, CH4 2개 채널

    for (int i = 0; i < 2; i++) {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = adc1_dma_channels[i] & 0x7;
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    dig_cfg.adc_pattern = adc_pattern;

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    ESP_LOGI(TAG, "ADC1 DMA (GPIO3, GPIO4) Initialized & Started.");
    return adc_handle;
}

void ADC_Sensing(void)
{
        uint8_t dma_result[ADC_SAMPLE_NUM * SOC_ADC_DIGI_DATA_BYTES_PER_CONV] = {0};
    uint32_t ret_num = 0;
// --- A. ADC1 (GPIO 3, GPIO 4) DMA 버퍼 읽기 ---
        esp_err_t ret = adc_continuous_read(adc_handle, dma_result, sizeof(dma_result), &ret_num, pdMS_TO_TICKS(100));
        
        uint32_t val_ch3 = 0, val_ch4 = 0;
        uint32_t cnt_ch3 = 0, cnt_ch4 = 0;

        if (ret == ESP_OK) {
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_DATA_BYTES_PER_CONV) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&dma_result[i];
                uint32_t chan = p->type2.channel;
                uint32_t data = p->type2.data;

                if (chan == ADC_CHANNEL_3) {
                    val_ch3 += data;
                    cnt_ch3++;
                } else if (chan == ADC_CHANNEL_4) {
                    val_ch4 += data;
                    cnt_ch4++;
                }
            }

            if (cnt_ch3) val_ch3 /= cnt_ch3;
            if (cnt_ch4) val_ch4 /= cnt_ch4;
        }

        // --- B. ADC2 (GPIO 5) Single Read 읽기 ---
        int val_gpio5 = 0;
        esp_err_t err = adc_oneshot_read(adc2_oneshot_handle, ADC2_GPIO5_CHANNEL, &val_gpio5);
        if (err != ESP_OK) {
            val_gpio5 = -1; // Wi-Fi 사용 중 충돌 등의 문제 발생 시
        }

        // --- C. 출력 ---
        //ESP_LOGI(TAG, "[DMA] CH3(IO3): %lu | CH4(IO4): %lu <---> [Oneshot] CH0(IO5): %d", val_ch3, val_ch4, val_gpio5);

}
void adc_init(void) {
#if 1
    // 1. 초기화 진행
    init_adc2_oneshot();
    adc_handle = init_adc1_dma();
    #endif
}


