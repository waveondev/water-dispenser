#include "opmode_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_config_flash.h"
#include "esp_timer.h"
#include "app_moter.h"
#include "app_TOF.h"
static QueueHandle_t opModeQueue = NULL;

static const char* TAG = __FILE__;
#define OPMODE_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)
static uint32_t current_opmode = OP_MODE_NORMAL;
static esp_timer_handle_t opmode_timer = NULL;

// 1초 뒤 타이머가 만료되면 실행될 콜백 함수
static void opmode_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "3초 동안 추가 입력이 없어 현재 모드로 확정합니다: %d", current_opmode);
    save_app_configuration();
    // TODO: 여기에 모드가 최종 확정되었을 때 실행할 동작(예: 화면 갱신, 실제 하드웨어 제어 등)을 넣으세요.
}

void Opmode_test_mode(void)
{
    current_opmode = OP_MODE_TEST;
}
void Opmode_Set(void)
{
        app_config_t* app_config = get_app_config();
        start_motor_with_boost(0,0);
        switch(current_opmode)
        {
            case OP_MODE_NORMAL:
                current_opmode = OP_MODE_NIGHT;
            break;
            case OP_MODE_NIGHT:
                current_opmode = OP_MODE_SMART;
            break;
            case OP_MODE_SMART:
                current_opmode = OP_MODE_SLEEP;
            break;
            case OP_MODE_SLEEP:
                current_opmode = OP_MODE_NORMAL;
            break;
            default:
            break;
        }
        app_config->op_mode = current_opmode;
    {
        // 2. 타이머가 처음 호출된 거라면 타이머를 생성
        if (opmode_timer == NULL) {
            const esp_timer_create_args_t timer_args = {
                .callback = &opmode_timer_callback,
                .name = "opmode_delay_timer"
            };
            esp_timer_create(&timer_args, &opmode_timer);
        }
        else {
            // 💡 이미 타이머가 존재한다는 뜻은, 이전에 버튼을 누른 적이 있다는 것!
            // 즉, 1초 이내에 다시 들어왔을 확률이 높으므로 기존 타이머를 멈춤.
            esp_timer_stop(opmode_timer);
        }
    // 4. 타이머를 1초(1,000,000 마이크로초)로 다시 시작
        esp_timer_start_once(opmode_timer, 3000000);

        ESP_LOGI(TAG, "모드 변경됨 -> %d (3초 타이머 시작/리셋)", current_opmode);
    }

}



static void Opmode_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting Opmode_task");
    while (1) {
        switch(current_opmode)
        {
            case OP_MODE_NORMAL:
                start_motor_with_boost(40,0);
            break;
            case OP_MODE_NIGHT:
                start_motor_with_boost(100,0);
            break;
            case OP_MODE_SMART:
                if(VL53L0X_Detect())
                {
                    start_motor_with_boost(40,0);
                }
                else
                {
                    start_motor_with_boost(0,0);
                }
            break;
            case OP_MODE_SLEEP:
               // start_motor_with_boost(0,0);
            break;
            default:
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
}






void opmode_task_init(void)
{
    TaskHandle_t xHandle = NULL;
    static uint8_t ucParameterToPass;
    app_config_t* app_config = get_app_config();
    current_opmode = app_config->op_mode;
    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreatePinnedToCore(
            Opmode_task,                  // 태스크 함수
            "opmode_task",                // 태스크 이름
            OPMODE_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 2,      // 우선순위
            &xHandle,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating Button_task on Core 1");
    }
}


