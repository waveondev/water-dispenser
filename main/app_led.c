#include "app_led.h"
#include "led_strip.h"
#include "esp_timer.h"
#define BLINK_GPIO   14   // 네오픽셀 데이터 선이 연결된 GPIO 핀 번호
#define LED_NUMBERS  4   // 연결된 네오픽셀 LED 총 개수 (예: 3개)
#define LED_BRIGHTNESS_MAX    250
#define LED_BRIGHTNESS_CENTER 100

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_config_flash.h"
#include "opmode_task.h"

#include "app_TOF.h"
static led_strip_handle_t led_strip;
static const char *TAG = __FILE__;
#define LED_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)

static SemaphoreHandle_t xLedStatusSemaphore = NULL;
static uint16_t led_status_resister = 0;
static uint16_t last_led_status_resister = 0; // ⭐️ 중복 실행 방지용 마스터 버퍼
static int last_op_mode = -1; 
static esp_timer_handle_t pairing_timer;
bool led_moter_enable(void)
{
    return (last_led_status_resister & TOF_DETECT_BIT);
}
void led_bit_enable(uint16_t enable)
{
    // 현재 마스터 버퍼에 해당 비트가 꺼져 있을 때만 (즉, 새로 켜지는 순간에만) 진입!
    if ((last_led_status_resister & enable) == 0) 
    {
        led_status_resister |= enable;
        
        if (xLedStatusSemaphore != NULL) {
            xSemaphoreGive(xLedStatusSemaphore); // 🚩 세마포어는 딱 1번만 던진다!
        }
       // ESP_LOGE(TAG, "led_status_resister = %08x",led_status_resister);
    }
}

void led_bit_disable(uint16_t disable)
{
    // 현재 마스터 버퍼에 해당 비트가 켜져 있을 때만 (즉, 새로 꺼지는 순간에만) 진입!
    if ((last_led_status_resister & disable) != 0) 
    {
        led_status_resister &= (~disable);
        
        if (xLedStatusSemaphore != NULL) {
            xSemaphoreGive(xLedStatusSemaphore); // 🚩 세마포어는 딱 1번만 던진다!
        }
        //ESP_LOGE(TAG, "led_status_resister = %08x",led_status_resister);
    }
}


void init_led_strip(void) {
    // 1. 네오픽셀 기본 설정 (v3.x 최신 규격)
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = LED_NUMBERS,
        // ⚠️ v3.x에서는 아래와 같이 멤버명과 상수명이 바뀌었습니다!
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW, 
        .led_model = LED_MODEL_WS2812,
    };

    // 2. RMT 하드웨어 타이머 설정 (v3.x 최신 규격)
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // ⚠️ 클럭 소스를 명시적으로 지정해 주어야 합니다.
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };

    // 3. ⚠️ v3.x 새 전용 초기화 함수 호출
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    set_led_clear();
}

void set_led_clear(void) {
    // 0번째 LED를 빨간색(R:255, G:0, B:0)으로 설정
    led_strip_set_pixel_rgbw(led_strip, 0, 0, 0, 0,0);

    // 1번째 LED를 초록색(R:0, G:255, B:0)으로 설정
    led_strip_set_pixel_rgbw(led_strip, 1, 0, 0, 0,0);

        // 0번째 LED를 빨간색(R:255, G:0, B:0)으로 설정
    led_strip_set_pixel_rgbw(led_strip, 2, 0, 0, 0,0);

    // 1번째 LED를 초록색(R:0, G:255, B:0)으로 설정
    led_strip_set_pixel_rgbw(led_strip, 3, 0, 0, 0,0);
    // ⚠️ 아두이노의 show()처럼, 실제 LED에 데이터를 쏴서 켜는 함수
    led_strip_refresh(led_strip); 
}


void set_rgb_led(uint8_t R, uint8_t G, uint8_t B, uint8_t W)
{
    // SK6812RGBW 칩은 W 소자가 따로 있으므로, 
    // W 값이 들어오면 RGB는 완전히 끄고 순수 W 소자만 켜는 것이 하드웨어 수명과 밝기에 완벽합니다.
    if (W != 0)
    {
        led_strip_set_pixel_rgbw(led_strip, 0, 0, 0, 0, W);
        led_strip_set_pixel_rgbw(led_strip, 1, 0, 0, 0, W);
        led_strip_set_pixel_rgbw(led_strip, 2, 0, 0, 0, W);
        led_strip_set_pixel_rgbw(led_strip, 3, 0, 0, 0, W);
    }
    else
    {
        led_strip_set_pixel_rgbw(led_strip, 0, R, G, B, 0);
        led_strip_set_pixel_rgbw(led_strip, 1, R, G, B, 0);
        led_strip_set_pixel_rgbw(led_strip, 2, R, G, B, 0);
        led_strip_set_pixel_rgbw(led_strip, 3, R, G, B, 0);
    }

    // 실제 SK6812 칩들로 32비트 정밀 신호 전송
    led_strip_refresh(led_strip); 
}


void app_tof_sensor_poll_100ms(void)
{
    static uint32_t tof_match_start_time = 0;
    static bool is_tof_pressing = false;

    if (VL53L0X_Detect()) 
    {
        is_tof_pressing = false;
        tof_match_start_time = 0;
        led_bit_enable(TOF_DETECT_BIT); 
    } 
    else 
    {
        if (!is_tof_pressing) {
            tof_match_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            is_tof_pressing = true;
        } else {
            uint32_t elapsed_time = (xTaskGetTickCount() * portTICK_PERIOD_MS) - tof_match_start_time;
            if (elapsed_time >= 3000) {
                // ⭐️ 3초 만족 시 호출 -> 내부 가드 덕분에 매번 호출해도 세마포어는 딱 1번만 방출됨!
                led_bit_disable(TOF_DETECT_BIT); // ⭐️ 손 치우면 즉시 꺼짐 호출
            }
        }
    }
    app_config_t* app_config = get_app_config();
    if(last_op_mode != app_config->op_mode && led_status_resister == 0)
    {       
        last_op_mode = app_config->op_mode; 
        if (xLedStatusSemaphore != NULL) {
            xSemaphoreGive(xLedStatusSemaphore); // 🚩 세마포어는 딱 1번만 던진다!
        }

    }
}
static void periodic_timer_callback(void* arg)
{
  
    ESP_LOGI(TAG, "타이머 실행 중...");

    static bool pairing_led_state = 0;
    if(last_led_status_resister & PAIRING_BIT)
    {
        if(pairing_led_state)
        {
            pairing_led_state = false;
            set_rgb_led(LED_BRIGHTNESS_MAX,0 , 0, 0); // 녹색
        }
        else
        {
            pairing_led_state = true;
            set_rgb_led(0,0 , LED_BRIGHTNESS_MAX, 0); // 녹색
        }
        return;
    }
    if (last_led_status_resister & TOF_DETECT_BIT) {
        set_rgb_led(0, LED_BRIGHTNESS_MAX, 0, 0); // 녹색
    }
    esp_timer_stop(pairing_timer);
}
static void LED_task(void *pvParameter)
{
    xLedStatusSemaphore = xSemaphoreCreateBinary();
    if (xLedStatusSemaphore == NULL) {
        ESP_LOGE(TAG, "세마포어 생성 실패!");
        vTaskDelete(NULL);
    }
    static int act_op_mode = -1;

  
    app_config_t* app_config = get_app_config();
    
    set_rgb_led(0,0,0,LED_BRIGHTNESS_MAX);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "Starting LED_task (Pure Event Driven Mode)");

    while (1) {
        app_tof_sensor_poll_100ms();
        if (xSemaphoreTake(xLedStatusSemaphore, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            
            uint16_t current_resister = led_status_resister;
  
            // [우선순위 1] 특수 비트가 하나라도 켜져 있는 상태라면
            if (current_resister != 0) {
                last_op_mode = -1; // 모드 무효화
                
                if (current_resister & PAIRING_BIT) {
                    ESP_ERROR_CHECK(esp_timer_start_periodic(pairing_timer, 500000));
                }
                else if (current_resister & TOF_DETECT_BIT) {
                    if (!esp_timer_is_active(pairing_timer)) {
                        esp_err_t err = esp_timer_start_periodic(pairing_timer, 1000);
                        if (err != ESP_OK) {
                            ESP_LOGE("LED_TIMER", "타이머 시작 실패: %s", esp_err_to_name(err));
                        }
                    } else {
                        ESP_LOGW("LED_TIMER", "타이머가 이미 실행 중이므로 start를 건너뜁니다.");
                    }
                }
                ESP_LOGE(TAG, "current_resister = %08x",current_resister);
            }
            // [우선순위 2] 비트가 다 꺼진 정상 상태라면 op_mode 적용
            else {
                switch(last_op_mode) {
                    case OP_MODE_NORMAL: set_rgb_led(0, LED_BRIGHTNESS_MAX, LED_BRIGHTNESS_MAX, 0); break;
                    case OP_MODE_NIGHT:  set_rgb_led(LED_BRIGHTNESS_MAX, 0, LED_BRIGHTNESS_MAX, 0); break;
                    case OP_MODE_SMART:  set_rgb_led(LED_BRIGHTNESS_MAX, LED_BRIGHTNESS_MAX, 0, 0); break;
                    case OP_MODE_SLEEP:  set_led_clear(); break;
                    default: set_rgb_led(0, 0, 0, LED_BRIGHTNESS_MAX); break;
                }
                ESP_LOGE(TAG, "last_op_mode = %08x",last_op_mode);
            }

            // ⭐️ [중요] 처리가 다 끝난 시점에 마스터 버퍼를 업데이트하여 다음 외부 진입을 방어합니다.
            last_led_status_resister = current_resister;
        }
    }
}






void LED_task_init(void)
{
    TaskHandle_t xHandle = NULL;
    static uint8_t ucParameterToPass;
    const esp_timer_create_args_t pairing_timer_args = {
        .callback = &periodic_timer_callback,
        .name = "periodic_1sec_timer"
    };

    // 타이머 생성
    ESP_ERROR_CHECK(esp_timer_create(&pairing_timer_args, &pairing_timer));
    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreatePinnedToCore(
            LED_task,                  // 태스크 함수
            "LED_task",                // 태스크 이름
            LED_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 1,      // 우선순위
            &xHandle,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating Button_task on Core 1");
    }
    
}




