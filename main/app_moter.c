
#include <stdio.h>
#include "app_moter.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_util.h"
#include "esp_log.h"
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
    if (percentage > 100)  percentage = 100;
        static int last_percentage = -1; // 이전 값을 기억할 변수
    if (percentage == last_percentage) {
        return; 
    }
    last_percentage = percentage; // 새로운 속도 저장

    // 2. 퍼센트(%)를 10비트 듀티비(0~1023) 값으로 변환
    // 공식: (Percentage * 1023) / 100
    uint32_t duty = 0;
    ESP_LOGI(TAG, "percentage = %d ", percentage);
// -------------------------------------------------------------
    // [경우 1] 100% 출력: PWM을 끄고 순수 GPIO HIGH로 꽂아버리기
    // -------------------------------------------------------------
   if (percentage == 100) {
        // 1. ledc_stop으로 타이머를 안전하게 멈춤 (HIGH 상태로)
        ledc_stop(LEDC_MODE, LEDC_CH0_MOTOR_IN1, 1);

        // 2. ⭐ 질문자님이 제안하신 안전한 구조체 초기화 방식!
        gpio_config_t io_conf_motor = {
            .pin_bit_mask = (1ULL << MOTOR_IN1_GPIO),
            .mode = GPIO_MODE_OUTPUT,              // 👈 반드시 OUTPUT이어야 합니다!
            .pull_up_en = GPIO_PULLUP_DISABLE,     // 풀다운/풀업 모두 꺼서 
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // gpio_reset_pin처럼 전압이 툭 떨어지는 걸 방지
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf_motor); // 핀 설정을 일반 GPIO 출력 모드로 완전 덮어쓰기

        // 3. 완벽한 3.3V 직선 출력 고정
        gpio_set_level(MOTOR_IN1_GPIO, 1);
    } 
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
        gpio_set_level(MOTOR_IN1_GPIO, 0);
    } 
    else {
        // 1% ~ 99% 구간: 다시 LEDC 기능으로 핀을 연결
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
    vTaskDelay(pdMS_TO_TICKS(20000)); 

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
        ESP_LOGI(TAG, "[PUMP] 🛑 모터 즉시 정지 (0%%)");
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

