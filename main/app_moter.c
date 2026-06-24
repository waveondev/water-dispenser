
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
#define LEDC_FREQUENCY       (1000)            // 20kHz 설정

#define LEDC_CH0_MOTOR_IN1   LEDC_CHANNEL_0


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
static const char *TAG = __FILE__;

// 모터 제어 함수 (percentage: 0 ~ 100)
void set_motor_speed_percent(int percentage) {
    // 1. 값의 범위를 0% ~ 100% 사이로 안전하게 제한
    if (percentage < 0)   percentage = 0;
    if (percentage > 100)  percentage = 100;

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



