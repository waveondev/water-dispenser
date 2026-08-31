#include "opmode_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_config_flash.h"

#include "app_moter.h"
#include "app_TOF.h"
#include "app_led.h"
#include "app_HX711.h"
#include "ble_tracker_id.h"
#include "debug_cli.h"
#include <math.h>
#include "aws_iot_task.h"
static QueueHandle_t opModeQueue = NULL;

static const char* TAG = __FILE__;
#define OPMODE_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
static uint32_t current_opmode = OP_MODE_NORMAL;
static esp_timer_handle_t opmode_timer = NULL;
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
// 1초 뒤 타이머가 만료되면 실행될 콜백 함수
static void opmode_timer_callback(void* arg)
{
    //ESP_LOGI(TAG, "3초 동안 추가 입력이 없어 현재 모드로 확정합니다: %d", current_opmode);
    app_nvs_save_set();
        water_fault_enable(WATER_MODECHANGE);
    water_fault_disable(WATER_MODECHANGE);
    // TODO: 여기에 모드가 최종 확정되었을 때 실행할 동작(예: 화면 갱신, 실제 하드웨어 제어 등)을 넣으세요.
}

void Opmode_test_mode(void)
{
    current_opmode = OP_MODE_TEST;
}
void Opmode_Set(void)
{
    app_config_t* app_config = get_app_config();
    uint8_t LED_Value = 0;

    switch(current_opmode)
    {
        case OP_MODE_NORMAL:
            current_opmode = OP_MODE_NIGHT;
            LED_Value = 127;
        break;
        case OP_MODE_NIGHT:
            current_opmode = OP_MODE_SMART;
            LED_Value = 255;
        break;
        case OP_MODE_SMART:
            current_opmode = OP_MODE_SLEEP;
            LED_Value = 255;
        break;
        default:
            current_opmode = OP_MODE_NORMAL;
            LED_Value = 255;
        break;
    }
    app_config->op_mode = current_opmode;
    LED_Bright_Set(LED_Value);
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
        esp_timer_start_once(opmode_timer, 5000000);

        ESP_LOGI(TAG, "모드 변경됨 -> %d (10초 타이머 시작/리셋)", current_opmode);
    }

}
#if 0
static esp_timer_handle_t Motion_Timeout_timer = NULL;
// 1초 뒤 타이머가 만료되면 실행될 콜백 함수
static void Motion_Timeout_callback(void* arg)
{
    //ESP_LOGI(TAG, "3초 동안 추가 입력이 없어 현재 모드로 확정합니다: %d", current_opmode);

    motion_msg_send(MOTION_START_REQUEST,2);
    // TODO: 여기에 모드가 최종 확정되었을 때 실행할 동작(예: 화면 갱신, 실제 하드웨어 제어 등)을 넣으세요.
}
void Motion_Timer_Set(bool state)
{
                // 2. 타이머가 처음 호출된 거라면 타이머를 생성
    if (Motion_Timeout_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &Motion_Timeout_callback,
            .name = "opmode_delay_timer"
        };
        esp_timer_create(&timer_args, &Motion_Timeout_timer);
    }

    // 💡 이미 타이머가 존재한다는 뜻은, 이전에 버튼을 누른 적이 있다는 것!
    // 즉, 1초 이내에 다시 들어왔을 확률이 높으므로 기존 타이머를 멈춤.
    if (esp_timer_is_active(Motion_Timeout_timer)) {
        esp_timer_stop(Motion_Timeout_timer);
    }
    
    if(state == true)
        esp_timer_start_once(Motion_Timeout_timer, 5000000);
}
#endif
static smart_state_t smart_state = SMART_IDLE;
smart_state_t Time_ratio_state(void)
{
    return smart_state;
}

#if 1

void Smart_Water(void)
{
    static uint32_t smart_timer_target = 0; // 각 상태별 마감 시한 틱 저장
    static float start_weight = 0;
    uint8_t splash_count = 0;
    bool sensor_detected = VL53L0X_Detect(false);
    uint32_t current_tick = xTaskGetTickCount();
    app_config_t* app_config = get_app_config();
    DRINK_Packet_t DRINK_Packet = {0};
    switch (smart_state)
    {
        case SMART_IDLE:
        if (sensor_detected) 
        {
            splash_count = 0;
            water_fault_disable(WATER_SPLASHING_FAULT);
            // 💡 1. 센서 감지 즉시 시작 무게 저장
            start_weight = loadcell_data_get();
            
            smart_timer_target = current_tick + ((app_config->EFFECTIVE_DWELL_TIME*1000) / portTICK_PERIOD_MS);
            smart_state = SMART_RUN_VERIFY;
            ESP_LOGI(TAG, "음수 시작 Verifying 5s... start_weight = %.2fg", start_weight);
        }
        break;

    case SMART_RUN_VERIFY:
        // 5초가 가기 전에 센서가 끊기면 칼같이 끄고 대기 상태로 복귀
        if (!sensor_detected) 
        {
            smart_state = SMART_IDLE;
            ESP_LOGI(TAG, "SMART: Sensor lost before 5s. Motor STOPPED.");
            break;
        }

        // 💡 2. [실시간 튐 감지] 현재 로드셀 무게 읽기
        float current_w = loadcell_data_get();

        // 💡 시작 무게(또는 직전 데이터)와 비교해 급격하게 50g 이상 위아래로 튀었는지 검사
        // (fabs를 써서 +50g 스파이크나 -50g 드롭을 모두 잡아냅니다)
            
        if (fabsf(current_w - start_weight) >= app_config->splash_delta_g) 
        {
            splash_count++;

            ESP_LOGE(TAG, "SMART: Abnormal weight spike detected! (Start: %.2fg, Current: %.2fg). %d Motor SHUTDOWN.", start_weight, current_w, splash_count);
            start_weight = current_w;

            if(splash_count > 2)
            {
                smart_state = SMART_IDLE; // 즉시 대기 상태로 복귀
                water_fault_enable(WATER_SPLASHING_FAULT);
                break;
            }  

        }

        // 3. 5초 동안 센서가 짱짱하게 잘 버텼는지 확인
        if ((int32_t)(smart_timer_target - current_tick) <= 0) 
        {
            // 5초 동안 급격하게 튀지 않고 무사히 통과 완료!
            mqtt_queue_send(MESSEGE_ACCESS,&start_weight,sizeof(start_weight));
            smart_state = SMART_RUN_STABLE;
            ESP_LOGI(TAG, "SMART: 5-second verification SUCCESS. Stable running...");
        }
        break;

        case SMART_RUN_STABLE:

            // 💡 2. 5초 이상 유지된 이후 센서 감지가 사라지면 "바로 끄기"
            if (!sensor_detected) 
            {
                // 💡 3. 꺼진 후 3초 동안 계속 감지 안 되는지 체크 시작
                smart_timer_target = current_tick + (3000 / portTICK_PERIOD_MS);
                smart_state = SMART_STOP_CHECK;
                ESP_LOGI(TAG, "SMART: Sensor lost. Motor STOPPED immediately. Checking 3s stable off...");
            }
            break;

        case SMART_STOP_CHECK:
            // ⚠️ 3초 버티는 도중에 센서가 다시 감지되었다면? -> 끄기 취소하고 다시 안정 구동으로!
            if (sensor_detected) 
            {
                smart_state = SMART_RUN_STABLE;
                ESP_LOGI(TAG, "SMART: Sensor came back during 3s check! Motor ON again.");
                break;
            }

            // 💡 3초 동안 센서가 단 한 번도 들어오지 않고 완벽하게 꺼짐이 유지된 경우
            if ((int32_t)(smart_timer_target - current_tick) <= 0) 
            {
                smart_state = SMART_IDLE; // 완전히 끝내고 대기 상태로 복귀
                float diff_weight = start_weight - loadcell_data_get();
                if(diff_weight > 1)
                {
                        Tracker_waterintake_end((uint32_t)(diff_weight));
                        DRINK_Packet.start_weight = start_weight;
                        DRINK_Packet.end_weight = loadcell_data_get();
                        DRINK_Packet.total_intake_ml = max(diff_weight,0);
                        DRINK_Packet.duration_sec = 0;
                        mqtt_queue_send(MESSEGE_DRINK,&DRINK_Packet,sizeof(DRINK_Packet_t));
                }
                ESP_LOGI(TAG, "음수 종료 end_weight = %.2fg, diff_weight = %.2fg",loadcell_data_get() ,diff_weight );
            }
            break;
    }
}

static void Opmode_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting Opmode_task");
    app_config_t* app_config = get_app_config();

    while (1) {
        DBG_Resister_t* DBG_Resister = Debug_Get();
        if(DBG_Resister->motor)
        {

        }
        else
        {
            if(ota_enable() || hardware_error_enable() || Water_empty_enable())
            {
                start_motor_with_boost(0, 0);
            }
            else
            {
                    switch(current_opmode)
                    {
                        case OP_MODE_SMART:
                        {
                            Smart_Water();
                            if(VL53L0X_Detect(true))
                                start_motor_with_boost(85, 0);
                            else
                                start_motor_with_boost(0, 0);
                        }
                        break;
                        // 타 모드는 기본 구조 유지
                        case OP_MODE_NORMAL:
                            Smart_Water();
                            start_motor_with_boost(85, 0);
                            break;
                        case OP_MODE_NIGHT:
                            Smart_Water();
                            start_motor_with_boost(45, 0);
                            break;
                        case OP_MODE_SLEEP:
                            start_motor_with_boost(0, 0);
                            break;
                        default:
                            break;
                    }
            }
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
#else

#endif





void opmode_task_init(void)
{
    static uint8_t ucParameterToPass;
    app_config_t* app_config = get_app_config();
    current_opmode = app_config->op_mode;
    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreate(
            Opmode_task,                  // 태스크 함수
            "opmode_task",                // 태스크 이름
            OPMODE_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 2,      // 우선순위
            NULL
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating Button_task on Core 1");
    }
}


