#include "opmode_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_config_flash.h"
#include "esp_timer.h"
#include "app_moter.h"
#include "app_TOF.h"
#include "app_led.h"
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

#if 0

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
#else

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
            {
                bool sensor_detected = VL53L0X_Detect();
                uint32_t current_tick = xTaskGetTickCount();
                
                // 💡 20초 최소 보장 마감 시한을 기록할 static 변수
                static uint32_t motor_must_run_until = 0;
                static bool is_running = false;

                // 1. 센서가 새로 감지되었고, 현재 모터가 꺼져 있는 상태라면 -> 20초 타이머 시작!
                if (sensor_detected && !is_running) 
                {
                    // 지금 시점으로부터 딱 20초(20000ms) 뒤의 틱 값을 마감 시한으로 설정
                    motor_must_run_until = current_tick + (20000 / portTICK_PERIOD_MS);
                    is_running = true;
                    ESP_LOGI(TAG, "SMART Mode: Motor started. Minimum 20 seconds guaranteed.");
                }

                // 2. 현재 모터가 켜져 있는 상태일 때
                if (is_running) 
                {
                    // 틱 카운트 오버플로우를 고려하여 안전하게 잔여 시간 계산
                    int32_t remaining_ticks = (int32_t)(motor_must_run_until - current_tick);

                    // 20초가 지난 시점이라면 (잔여 틱이 0 이하)
                    if (remaining_ticks <= 0) 
                    {
                        // ⭐️ 20초가 지났을 때의 센서 상태를 체크합니다.
                        if (sensor_detected) 
                        {
                            // 센서가 여전히 들어와 있다면 꺼지지 않고 계속 유지 (타이머는 끝남)
                            start_motor_with_boost(40, 0);
                        } 
                        else 
                        {
                            // 20초도 지났고 센서도 꺼져 있다면 그제서야 모터 정지!
                            start_motor_with_boost(0, 0);
                            is_running = false; // 다시 처음 상태로 복귀
                            ESP_LOGI(TAG, "SMART Mode: 20s passed & No sensor. Motor stopped.");
                        }
                    } 
                    else 
                    {
                        // 아직 20초가 지나지 않았다면 센서가 있든 없든 무조건 True(구동)
                        start_motor_with_boost(40, 0);
                    }
                } 
                else 
                {
                    // 아예 처음부터 아무것도 감지 안 된 상태
                    start_motor_with_boost(0, 0);
                }
                break;
            }
            case OP_MODE_SLEEP:
               // start_motor_with_boost(0,0);
            break;
            default:
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
}
#endif





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


