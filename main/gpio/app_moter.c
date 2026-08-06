
#include <stdio.h>
#include "app_moter.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_util.h"
#include "esp_log.h"
#include "app_led.h"
#include "debug_cli.h"
#include "app_config_flash.h"
#include "tx_mqtt.h"
#include "aws_iot_task.h"
#if 0
#define MOTOR_IN1_GPIO       (PIN_PUMP_PWM)


#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LEDC_TIMER           LEDC_TIMER_0
#define LEDC_DUTY_RES        LEDC_TIMER_10_BIT  // 10비트 해상도 (0 ~ 1023)
#define LEDC_FREQUENCY       (20000)            // 20kHz 설정

#define LEDC_CH0_MOTOR_IN1   LEDC_CHANNEL_0
static const char *TAG = __FILE__;

void init_ledc(){
    printf("Hello");
}

void init_motor_ledc(void) {
    #if 1
    // 1. 타이머 설정 (20kHz, 10비트 해상도)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. 채널 0 설정 (MOTOR_IN1)
    ledc_channel_config_t ledc_ch0 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CH0_MOTOR_IN1,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_IN1_GPIO,
        .duty           = 0, // 초기값 0
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch0));
    #else
        // 2. PIN_HX711_SCK 설정 (아웃풋 모드)
    gpio_config_t io_conf_sck = {
        .pin_bit_mask = (1ULL << PIN_PUMP_PWM),
        .mode = GPIO_MODE_OUTPUT,               // ⚠️ 아웃풋 모드로 변경
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_sck);

    // 3. digitalWrite(PIN_HX711_SCK, LOW); 대체
    // 초기 시작 시 클럭 핀을 0(LOW) 상태로 안전하게 내려둡니다.
    gpio_set_level(PIN_PUMP_PWM, 1);
    #endif
}

// 모터 제어 함수 (speed: -1023 ~ 1023)
void set_motor_speed(int speed) {
    uint32_t duty = (speed < 0) ? -speed : speed;
    if (duty > 1023) duty = 1023; // Max Duty 제한
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH0_MOTOR_IN1, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH0_MOTOR_IN1));
}

// 모터 제어 함수 (percentage: 0 ~ 100)
void set_motor_speed_percent(int percentage) 
{
    // 1. 값의 범위를 0% ~ 100% 사이로 안전하게 제한
    if (percentage < 0)   percentage = 0;
    if (percentage > 100) percentage = 100;
    
    static int last_percentage = -1; // 이전 값을 기억할 변수
    if (percentage == last_percentage) {
        return; 
    }
    last_percentage = percentage; // 새로운 속도 저장

    ESP_LOGI(TAG, "percentage = %d", percentage);

    // -------------------------------------------------------------
    // [경우 1] 100% 출력: PWM을 끄고 순수 GPIO HIGH로 고정
    // -------------------------------------------------------------
    if (percentage == 100) {
        // 1. 진행 중이던 PWM 타이머를 멈춤
        ledc_stop(LEDC_MODE, LEDC_CH0_MOTOR_IN1, 1);
        
        // 2. 핀을 완벽한 일반 출력(OUTPUT) 모드로 덮어쓰기
        gpio_config_t io_conf_motor = {
            .pin_bit_mask = (1ULL << MOTOR_IN1_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf_motor);
        
        // 3. 3.3V 직선 출력 고정
        gpio_set_level(MOTOR_IN1_GPIO, 1);
        
        // ❌ 주의: 여기서 절대 ledc_set_duty나 ledc_update_duty를 부르면 안 됩니다!
    }
    // -------------------------------------------------------------
    // [경우 2] 0% 출력: PWM을 끄고 순수 GPIO LOW로 고정
    // -------------------------------------------------------------
    else if (percentage == 0) {
        ledc_stop(LEDC_MODE, LEDC_CH0_MOTOR_IN1, 0);
        
        gpio_config_t io_conf_motor = {
            .pin_bit_mask = (1ULL << MOTOR_IN1_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf_motor);
        
        gpio_set_level(MOTOR_IN1_GPIO, 0); // 완벽한 0V 접지
        
        // ❌ 주의: 여기서도 ledc_ 관련 함수 호출 금지!
    }
    // -------------------------------------------------------------
    // [경우 3] 1% ~ 99% 구간: 다시 LEDC(PWM) 기능 활성화
    // -------------------------------------------------------------
    else {
        // 0%나 100%에서 GPIO로 뺏어갔던 핀의 소유권을 다시 LEDC로 돌려주기 위해
        // 채널을 재초기화 해줍니다. (필수)
        ledc_channel_config_t ledc_ch0 = {
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CH0_MOTOR_IN1,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = MOTOR_IN1_GPIO,
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_ch0);

        // 10비트(0~1023) 해상도에 맞게 듀티 계산 후 적용
        uint32_t duty = (percentage * 1023) / 100;
        ledc_set_duty(LEDC_MODE, LEDC_CH0_MOTOR_IN1, duty);
        ledc_update_duty(LEDC_MODE, LEDC_CH0_MOTOR_IN1);
    }
}


// 백그라운드 태스크 및 진행 상태 감시용 전역 변수
static TaskHandle_t xMotorBoostTaskHandle = NULL;
static int duration_sec_buf = 0; // 0이 아니면 현재 "시간 제한 모드"가 작동 중임을 의미

typedef struct {
    int target_percentage;
    int duration_sec; 
} motor_boost_args_t;

// [set_motor_speed_percent 등 기존 하드웨어 제어 함수는 동일]

// ---------------------------------------------------------------------------
// ⏳ 백그라운드에서 부스트 및 총 가동 시간을 계산하는 일꾼 태스크
// ---------------------------------------------------------------------------
static void motor_boost_task(void *pvParameters)
{
    motor_boost_args_t *args = (motor_boost_args_t *)pvParameters;
    int target_speed = args->target_percentage;
    int duration = args->duration_sec;
    free(args); 

    // 1. 🚀 무조건 첫 10초는 100% 초기 부스트 구동
    ESP_LOGI(TAG, "[PUMP] 🚀 100%% 초기 부스트 스타트! (10초 대기)");

    set_motor_speed_percent(100);
    vTaskDelay(pdMS_TO_TICKS(3000));    

    // 2. 부스트가 끝났으니 목표 속도로 전환
    ESP_LOGI(TAG, "[PUMP] ⏱️ 초기 부스트 완료. 목표 속도 %d%%로 전환", target_speed);
    set_motor_speed_percent(target_speed);

// 3. ⏳ 가동 시간(duration)에 따른 분기 처리 (반복문 적용 버전)
    if (duration == 0) {
        ESP_LOGI(TAG, "[PUMP] ♾️ 가동 시간 제한 없음 모드. 상시 가동합니다.");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000)); 
        }
    } 
    else {
        int remaining_sec = duration - 10; // 초기 부스트 10초를 뺀 남은 시간 계산

        // ⭐️ 1초씩 쪼개서 대기하며 실시간으로 카운트다운을 찍는 반복문
        while (remaining_sec > 0) {
            ESP_LOGI(TAG, "[PUMP] ⏳ 설정된 남은 가동 시간 %d초 유지 중...", remaining_sec);
            
            vTaskDelay(pdMS_TO_TICKS(1000)); // 정확히 1초(1000ms)만 대기
            remaining_sec--;                 // 1초 차감
            
            // 💡 [꿀팁] 만약 가동 도중에 외부에서 모터 정지(0%) 명령을 내리면 
            // 170초를 멍하니 기다리지 않고 즉시 탈출하는 코드를 넣고 싶다면 아래 주석을 푸세요!
            // if (current_target_percentage == 0) { break; }
        }

        // 4. 🏁 약속된 시간을 무사히 다 채웠으므로 자동 종료
        ESP_LOGI(TAG, "[PUMP] 🏁 약속된 가동 시간이 만료되어 모터를 자동으로 끕니다.");
        set_motor_speed_percent(0);
    }
    // 🔥 [중요] 지옥의 레이스(지정 시간 가동)가 끝났으므로 자물쇠를 풀고 초기화합니다.
    duration_sec_buf = 0; 
    xMotorBoostTaskHandle = NULL;
    vTaskDelete(NULL); 
}

static int current_target_percentage = 0; 

void start_motor_with_boost(int target_percentage, int duration_sec)
{
    // 1. [락 로직] 현재 시간 제한 모드(duration_sec_buf가 양수)가 돌고 있다면 무조건 무시!
    if (duration_sec_buf > 0) {
        //ESP_LOGW(TAG, "[PUMP] ❌ 거부: 시간 제한 가동 중 (%d초 대기 필요)", duration_sec_buf);
        return; 
    }

    // 2. 예외 처리: 목표 속도가 0% 이거나, 가동 시간이 10초 이하인 경우 예외 정지 처리
    if (target_percentage <= 0 || (duration_sec > 0 && duration_sec <= 10)) {
       // ESP_LOGI(TAG, "[PUMP] 🛑 모터 즉시 정지 (0%%)");
        set_motor_speed_percent(0);
        
        // 정지했으므로 상태 변수들도 초기화
        current_target_percentage = 0;
        if (xMotorBoostTaskHandle != NULL) {
            vTaskDelete(xMotorBoostTaskHandle);
            xMotorBoostTaskHandle = NULL;
        }
        return; 
    }

    // ⭐️ [핵심 수정] 무한 모드(duration_sec == 0)일 때, 똑같은 속도 명령이 또 들어온 거라면?
    // 이미 잘 돌고 있으므로 태스크를 지우고 새로 만들 필요가 전혀 없습니다! 그냥 통과!
    if (duration_sec == 0 && target_percentage == current_target_percentage && xMotorBoostTaskHandle != NULL) {
        // 100ms마다 들어오는 동일한 명령은 여기서 전부 안전하게 걸러집니다.
        return; 
    }

    // 만약 "무한 모드 도중에 속도가 바뀌었거나", "새로운 시간 제한 명령"이 들어온 경우라면?
    // 기존 태스크를 끊고 새 명령을 수행해야 합니다.
    if (xMotorBoostTaskHandle != NULL) {
        ESP_LOGW(TAG, "[PUMP] ⚠️ 목표 속도 변경 또는 모드 변경으로 기존 태스크를 교체합니다. (이전:%d%% -> 새:%d%%)", 
                 current_target_percentage, target_percentage);
        vTaskDelete(xMotorBoostTaskHandle);
        xMotorBoostTaskHandle = NULL;
    }

    // 상태 업데이트
    duration_sec_buf = duration_sec;
    current_target_percentage = target_percentage; // 현재 속도 타겟 저장

    // 태스크 생성 인수 할당
    motor_boost_args_t *args = malloc(sizeof(motor_boost_args_t));
    if (args == NULL) return;
    args->target_percentage = target_percentage;
    args->duration_sec = duration_sec;

    xTaskCreate(motor_boost_task, "motor_boost_task", 2048, (void *)args, 5, &xMotorBoostTaskHandle);
}

#else
#include "driver/rmt_tx.h"

rmt_channel_handle_t pwm_chan = NULL;
rmt_encoder_handle_t copy_encoder = NULL;

#define MOTOR_IN1_GPIO       (PIN_PUMP_PWM)
#define LEDC_FREQUENCY       (20000000)            // 20kHz 설정

#define LEDC_CH0_MOTOR_IN1   LEDC_CHANNEL_0
static const char *TAG = __FILE__;
static int current_target_percentage = 0; 
static uint32_t* Motor_Used_Time;
static esp_timer_handle_t Motor_used_timer;

// 모터 제어 함수 (percentage: 0 ~ 100)
void set_motor_speed_percent(int percentage) {
    static rmt_symbol_word_t pwm_symbol;
    
    // 1. 안전한 범위 제한
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    uint32_t total_ticks = 1000; // 1000틱 = 50us = 20kHz
    uint32_t high_ticks = (total_ticks * percentage) / 100;
    uint32_t low_ticks = total_ticks - high_ticks;

    if (esp_timer_is_active(Motor_used_timer)) {
        esp_timer_stop(Motor_used_timer);
    }
    if(percentage)
        ESP_ERROR_CHECK(esp_timer_start_periodic(Motor_used_timer, 1000000));    

    // 2. v5.x 방식: 구조체에 직접 레벨(Level)과 지속시간(Duration) 대입
    if (percentage == 100) {
        // 100% 듀티: 계속 High (0 방지용으로 반반 쪼개기)
        pwm_symbol.level0 = 1;
        pwm_symbol.duration0 = total_ticks / 2;
        pwm_symbol.level1 = 1;
        pwm_symbol.duration1 = total_ticks - (total_ticks / 2);
    } else if (percentage == 0) {
        // 0% 듀티: 계속 Low
        pwm_symbol.level0 = 0;
        pwm_symbol.duration0 = total_ticks / 2;
        pwm_symbol.level1 = 0;
        pwm_symbol.duration1 = total_ticks - (total_ticks / 2);
    } else {
        // 일반 PWM: High 이후 Low
        pwm_symbol.level0 = 1;
        pwm_symbol.duration0 = high_ticks;
        pwm_symbol.level1 = 0;
        pwm_symbol.duration1 = low_ticks;
    }

    // 3. 무한 루프 송출 설정 (-1)
    rmt_transmit_config_t tx_config = {
        .loop_count = -1, 
    };

    rmt_disable(pwm_chan); 
    rmt_enable(pwm_chan);

    // 4. DMA를 통해 심볼 전송 시작 (기존 송출을 덮어씀)
    ESP_ERROR_CHECK(rmt_transmit(pwm_chan, copy_encoder, &pwm_symbol, sizeof(pwm_symbol), &tx_config));

}


// 백그라운드 태스크 및 진행 상태 감시용 전역 변수
static TaskHandle_t xMotorBoostTaskHandle = NULL;
static int duration_sec_buf = 0; // 0이 아니면 현재 "시간 제한 모드"가 작동 중임을 의미

// 큐 핸들 변수 선언
SemaphoreHandle_t motor_semaphore = NULL;
static uint32_t* Motor_Used_Time;
static uint32_t* Filter_Used_Time;
#define SECONDS_IN_DAYS    (24UL * 60UL * 60UL)
static volatile bool filter_send_flag = false; // volatile 추가
static volatile bool moter_send_flag = false; // volatile 추가

void motor_change(void)
{
    (*Motor_Used_Time) = 0;
    motor_nvs_save_set();
    water_fault_disable(WATER_FILTER_WATER_EX);
    moter_send_flag = false;
}
void filter_change(void)
{
    (*Filter_Used_Time) = 0;
    filter_nvs_save_set();
    water_fault_disable(WATER_FILTER_DEBRIS_EX);
    filter_send_flag = false;
}
static void motor_used_timer_callback(void* arg)
{

    app_config_t* app_config = get_app_config();
    (*Motor_Used_Time)++;
    if(*Motor_Used_Time >= (app_config->moter_life_days *SECONDS_IN_DAYS) )
    {
        water_fault_enable(WATER_FILTER_WATER_EX);
        if(moter_send_flag == false)
        {
            moter_send_flag = true;
            motor_nvs_save_set();
        }
    }

    (*Filter_Used_Time)++;
    if(*Filter_Used_Time >= (app_config->filter_life_days *SECONDS_IN_DAYS) )
    {
        water_fault_enable(WATER_FILTER_DEBRIS_EX);
        if(filter_send_flag == false)
        {
            filter_send_flag = true;
            filter_nvs_save_set();
        }
    }
}

void Clean_Mode_Disable(void)
{
    duration_sec_buf = 0;
    led_bit_disable(CLEAN_MODE_BIT); 
}

static void motor_boost_task(void *pvParameters)
{
    while(1)
    {
        if (xSemaphoreTake(motor_semaphore, portMAX_DELAY) == pdTRUE) 
        {
            ESP_LOGW("RECEIVER", "큐 수신 완료! -> [모터 구동] %d, time = %d",current_target_percentage, duration_sec_buf);
            
            // 💡 이곳에 이전에 만든 RMT 모터 제어 함수를 넣으면 됩니다!
           // for(int i=0;i<100;i++)
            {
           //     set_motor_speed_percent(i);
            //    vTaskDelay(pdMS_TO_TICKS(20)); // 정확히 1초(1000ms)만 대기
            }
            if(current_target_percentage != 100)
            {
                set_motor_speed_percent(100);
                vTaskDelay(pdMS_TO_TICKS(2000)); // 정확히 1초(1000ms)만 대기
            }

            set_motor_speed_percent(current_target_percentage);
            while(duration_sec_buf)
            {
                duration_sec_buf--;
                ESP_LOGI("SENDER", "Clean %d ",duration_sec_buf);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            Clean_Mode_Disable();
        }
    }
}


void start_motor_with_boost(int target_percentage, int duration_sec)
{

    if(duration_sec_buf > 0)
    {
        if(duration_sec != 0 && target_percentage)
        {
            duration_sec_buf = 0;
            set_motor_speed_percent(0);
            current_target_percentage = 0;
        }
        return;
    }


    if (target_percentage <= 0) {
       // ESP_LOGI(TAG, "[PUMP] 🛑 모터 즉시 정지 (0%%)");
        set_motor_speed_percent(0);
        current_target_percentage = 0;
        return; 
    }

    if (duration_sec == 0 && target_percentage == current_target_percentage) {
        return; 
    }
    duration_sec_buf = duration_sec;
    current_target_percentage = target_percentage;

    if (motor_semaphore != NULL) {
        xSemaphoreGive(motor_semaphore); // 세마포어에 신호(1) 채우기
        ESP_LOGI("SENDER", "모터 동작 세마포어 송신!");
    }
}
#define MOTOR_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)

void init_motor_ledc(void) {
    // 1. RMT TX 채널 설정 (DMA 활성화)
    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = MOTOR_IN1_GPIO,             // 👈 출력할 GPIO 15번 핀
        .mem_block_symbols = 64,    // DMA 내부 블록 사이즈
        .resolution_hz = LEDC_FREQUENCY,  // 🌟 20MHz 해상도 (1틱 = 50ns)
        .trans_queue_depth = 4,
        .flags.with_dma = true,     // 🌟 핵심: DMA 통신 켜기
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_config, &pwm_chan));

    // 2. 심볼(Symbol) 데이터를 그대로 복사해주는 기본 인코더 사용
    rmt_copy_encoder_config_t encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &copy_encoder));

    // 3. RMT 채널 하드웨어 켜기
    ESP_ERROR_CHECK(rmt_enable(pwm_chan));

    Motor_Used_Time = get_motor_time();
    Filter_Used_Time = get_filter_time();

    const esp_timer_create_args_t Motor_Used_timer_args = {
        .callback = &motor_used_timer_callback,
        .name = "Motor_used_timer"
    };

    // 타이머 생성
    ESP_ERROR_CHECK(esp_timer_create(&Motor_Used_timer_args, &Motor_used_timer));

// 2. 이진 세마포어 생성 (초기 상태: 0 - 비어있음)
    motor_semaphore = xSemaphoreCreateBinary();
    
    if (motor_semaphore == NULL) {
        ESP_LOGE("MAIN", "세마포어 생성 실패!");
    }

    if (xTaskCreatePinnedToCore(
            motor_boost_task,                  // 태스크 함수
            "motor_boost_task",                // 태스크 이름
            MOTOR_TASK_STACK_SIZE,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 1,      // 우선순위
            NULL,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating motor_boost_task on Core 1");
    }

}
#endif

