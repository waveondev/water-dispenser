#include "app_sensor.h"
#include "esp_system.h"
#include "esp_err.h"

#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// 구버전 ESP-IDF용 I2C 헤더 경로

#include "app_HX711.h"
#include "app_TOF.h"
#include "app_adc.h"
#include "app_led.h"

#define SENSOR_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
#define TASK_DELAY_MS(x) (x/portTICK_PERIOD_MS)
static const char *TAG = __FILE__;
#ifndef PACKED
#define PACKED __attribute__((packed))
#endif



void Sensor_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting sensor task");
    bool ret = true; 
    int error_count = 0 ;
    adc_init();
    
    ret = TOF_VL53L0X_init();
    #if 1
    if(ret == false)
    {
        ESP_LOGE(TAG, "TOF Error\r\n");
       // return ret;
    }
    #endif
    while (1) {
        ADC_Sensing();
        #if 1
        #endif
        VL53L0X_Sensing();

        //ESP_LOGI(TAG, "gpio_set_level(IR_OUT0) = %d\r\n",gpio_get_level(IR_OUT0));
        //ESP_LOGI(TAG, "gpio_set_level(IR_OUT1) = %d\r\n",gpio_get_level(IR_OUT1));
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
}

bool sensor_init(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    HX711_task_init();
    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreate(
            Sensor_task,                  // 태스크 함수
            "sensor_task",                // 태스크 이름
            SENSOR_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 4,      // 우선순위
            &xHandle
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        ESP_LOGE(TAG, "Error creating Sensor_task on Core 1");
    }

    return true;
}