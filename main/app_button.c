#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "gpio_util.h"
#include "esp_log.h"
#include "app_config_flash.h"
#include "driver/gpio.h"
#include "app_moter.h"
#include "opmode_task.h"

static const char *TAG = "BUTTON_CTRL";
#define BUTTON_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)
// 설정 타임아웃 (밀리초)
#define DEBOUNCE_TIME_MS      50
#define DOUBLE_CLICK_DELAY_MS 300
#define LONG_PRESS_3S_MS      3000
#define LONG_PRESS_5S_MS      5000
#define LONG_PRESS_10S_MS     10000

// 이벤트 종류 정의
typedef enum {
    EV_SHORT_CLICK,
    EV_DOUBLE_CLICK,
    EV_LONG_3S,
    EV_LONG_5S,
    EV_LONG_10S
} button_event_t;

// 상태 관리를 위한 글로벌 변수들
static int click_count = 0;
static int64_t pressed_time = 0;
static bool is_pressed = false;

static QueueHandle_t gpio_evt_queue = NULL;
static TimerHandle_t double_click_timer = NULL;
static TimerHandle_t long_press_timer = NULL;

// 롱클릭 실시간 단계를 체크하기 위한 변수
static int long_press_stage = 0; 

// 함수 선언
static void Button_task(void* arg);
static void double_click_timer_callback(TimerHandle_t xTimer);
static void long_press_timer_callback(TimerHandle_t xTimer);

// [ISR] 인터럽트 서비스 루틴 (최대한 짧고 빠르게 실행되어야 함)
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    // 인터럽트가 발생한 핀 번호를 큐로 전송하여 태스크에서 처리하도록 함
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}
void bf_SingleClickAction(void) {
    Opmode_Set();
    ESP_LOGI(TAG,"Single Click Action executed \r\n");
}

void bf_DoubleClickAction(void) {
    app_config_t* app_config = get_app_config();
    ESP_LOGI(TAG,"Double Click Action executed\r\n");
    start_motor_with_boost(40,app_config->pump_clean_duration);
}

void bf_LongPress3SecAction(void) {
    ESP_LOGI(TAG,"Long Press 3 Sec Action executed\r\n");
    // Add your long press 3 sec action code here

}
void bf_LongPress5SecAction(void)
{
    ESP_LOGI(TAG,"Long Press 5 Sec Action executed \r\n");
}

void bf_LongPress10SecAction(void) {
    ESP_LOGI(TAG,"Long Press 10 Sec Action executed \r\n");
    //delay(1000); // Wait for 1 second
    //ESP.restart();
}
// 버튼 상태 및 타이머를 처리하는 메인 태스크
static void Button_task(void* arg)
{
    uint32_t io_num;
    int64_t last_intr_time = 0;

    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int64_t now = esp_timer_get_time() / 1000; // 현재 시간 (ms)
            
            // 1. 디바운스 소프트웨어 처리
            if ((now - last_intr_time) < DEBOUNCE_TIME_MS) {
                continue; 
            }
            last_intr_time = now;

            int level = gpio_get_level(io_num);

            // 버튼 눌림 (LOW) - 풀업 기준
            if (level == 0 && !is_pressed) {
                is_pressed = true;
                pressed_time = now;
                long_press_stage = 0;

                // 3초 후 롱클릭 감지를 시작할 타이머 가동 (100ms 주기로 체크)
                xTimerChangePeriod(long_press_timer, pdMS_TO_TICKS(100), 0);
                xTimerStart(long_press_timer, 0);
            }
            // 버튼 떨어짐 (HIGH)
            else if (level == 1 && is_pressed) {
                is_pressed = false;
                xTimerStop(long_press_timer, 0); // 롱클릭 타이머 정지

                int64_t press_duration = now - pressed_time;

                // 3초 미만으로 누르고 뗀 경우에만 클릭 카운트 증가
                if (press_duration < LONG_PRESS_3S_MS) {
                    click_count++;
                    // 다음 클릭이 오는지 기다리는 타이머 구동 (300ms)
                    xTimerChangePeriod(double_click_timer, pdMS_TO_TICKS(DOUBLE_CLICK_DELAY_MS), 0);
                    xTimerStart(double_click_timer, 0);
                }
            }
        }
    }
}

// 더블 클릭 대기 타이머 콜백 (300ms 동안 추가 입력이 없으면 최종 판정)
static void double_click_timer_callback(TimerHandle_t xTimer)
{
    if (click_count == 1) {
        ESP_LOGI(TAG, "👉 [EVENT] 단일 클릭 (Short)");
        bf_SingleClickAction();
    } else if (click_count >= 2) {
        ESP_LOGI(TAG, "👉 [EVENT] 더블 클릭!");
        bf_DoubleClickAction();
    }
    click_count = 0; // 카운트 초기화
}

// 롱클릭 실시간 감지 타이머 콜백 (버튼을 누르고 있는 동안 100ms마다 실행됨)
static void long_press_timer_callback(TimerHandle_t xTimer)
{
    if (!is_pressed) {
        xTimerStop(xTimer, 0);
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;
    int64_t duration = now - pressed_time;

    // 10초 경과 감지
    if (duration >= LONG_PRESS_10S_MS && long_press_stage < 3) {
        ESP_LOGI(TAG, "👉 [EVENT] 10초 롱클릭!");
        long_press_stage = 3;
        click_count = 0; // 롱클릭 중 뗀 이후 클릭 이벤트 방지
        bf_LongPress10SecAction();
    }
    // 5초 경과 감지
    else if (duration >= LONG_PRESS_5S_MS && duration < LONG_PRESS_10S_MS && long_press_stage < 2) {
        ESP_LOGI(TAG, "👉 [EVENT] 5초 롱클릭!");
        long_press_stage = 2;
        click_count = 0;
        bf_LongPress5SecAction();
    }
    // 3초 경과 감지
    else if (duration >= LONG_PRESS_3S_MS && duration < LONG_PRESS_5S_MS && long_press_stage < 1) {
        ESP_LOGI(TAG, "👉 [EVENT] 3초 롱클릭!");
        long_press_stage = 1;
        click_count = 0;
        bf_LongPress3SecAction();
    }
}




void button_task_init(void)
{
    static uint8_t ucParameterToPass;

    // GPIO 설정 구조체 정의
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_PKEY_STAT),    // 설정할 핀 비트마스크
        .mode = GPIO_MODE_INPUT,                    // 인풋 모드
        .pull_up_en = GPIO_PULLUP_ENABLE,           // 내부 풀업 저항 활성화 (하드웨어에 맞게 설정)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,       // 풀다운 비활성화
        .intr_type = GPIO_INTR_ANYEDGE              // ⚠️ 라이징 + 폴링 둘 다 감지 (Any Edge)
    };
    // 설정 적용
    gpio_config(&io_conf);
    
    // ISR 서비스 설치 (기본 설정 플래그 0)
    gpio_install_isr_service(0);
    
    // 특정 GPIO 핀에 ISR 핸들러 등록
    gpio_isr_handler_add(PIN_PKEY_STAT, gpio_isr_handler, (void*) PIN_PKEY_STAT);
// 2. FreeRTOS 큐 및 타이머 생성
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    
    double_click_timer = xTimerCreate("double_click_timer", pdMS_TO_TICKS(DOUBLE_CLICK_DELAY_MS), 
                                      pdFALSE, (void*)0, double_click_timer_callback);
                                      
    long_press_timer = xTimerCreate("long_press_timer", pdMS_TO_TICKS(100), 
                                    pdTRUE, (void*)0, long_press_timer_callback);
    
    TaskHandle_t xHandle = NULL;

    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreatePinnedToCore(
            Button_task,                  // 태스크 함수
            "button_task",                // 태스크 이름
            BUTTON_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 2,      // 우선순위
            &xHandle,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating Button_task on Core 1");
    }
}