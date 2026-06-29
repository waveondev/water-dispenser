#include "motion_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "app_config_flash.h"
#include "ble_parse.h"
#include "ble_task.h"
static const char *TAG = __FILE__;

static esp_timer_handle_t motion_timer;

static void motion_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "타이머 실행 중...");
  
    app_config_t* app_config = get_app_config();
    motion_msg_send(MOTION_START_REQUEST);
    esp_timer_stop(motion_timer);


    // 3. 변경된 시간으로 타이머를 다시 시작합니다.
    if (app_config->motion_data_time > 0) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(motion_timer, (app_config->motion_data_time) * 1000000ULL));
        ESP_LOGI(TAG, "모션 타이머 주기 수정 완료: %d초", app_config->motion_data_time);
    } else {
        // 설정값이 0 이하로 잘못 들어왔을 경우 기본값 30분(1800초) 적용
        ESP_ERROR_CHECK(esp_timer_start_periodic(motion_timer, 1800ULL * 1000000ULL));
        ESP_LOGI(TAG, "모션 타이머 주기 수정 완료: 기본값 1800초");
    }
}

void MotionGetTimer(bool status)
{
    app_config_t* app_config = get_app_config();
    if(status)
    {
        esp_timer_stop(motion_timer);
        ESP_ERROR_CHECK(esp_timer_start_periodic(motion_timer, 5000000));
    }
    else
        esp_timer_stop(motion_timer);
}



void MotionTaskInit(void)
{

    const esp_timer_create_args_t motion_timer_args = {
        .callback = &motion_timer_callback,
        .name = "motion_1sec_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&motion_timer_args, &motion_timer));
}