#include "app_led.h"
#include "led_strip.h"
#include "gpio_util.h"
#define LED_NUMBERS  6   // 연결된 네오픽셀 LED 총 개수 (예: 3개)
#define LED_BRIGHTNESS_MAX    180
#define LED_BRIGHTNESS_CENTER 100

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_config_flash.h"
#include "opmode_task.h"

#include "app_TOF.h"
#include "debug_cli.h"
static led_strip_handle_t led_strip;
static const char *TAG = __FILE__;
#define LED_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)


static uint16_t led_status_resister = 0;
static int last_op_mode = -1; 
static int wifi_conn_enable = 0;
#define LED_TASK_DELAY 10
typedef struct 
{
    uint32_t used;
    uint32_t target_r;
    uint32_t target_g;
    uint32_t target_b;
    uint32_t target_w;

    int brightness;
    int step; // 한 번에 변화할 밝기 크기 (작을수록 더 정밀하고 부드러워짐)
}Breathing_Setting_t;
static Breathing_Setting_t Breathing_Setting;
bool TOF_enable(void)
{
    return (led_status_resister & TOF_DETECT_BIT);
}
bool ota_enable(void)
{
    return (led_status_resister & OTA_START_BIT);
}
bool hardware_error_enable(void)
{
    return (led_status_resister & HARDWARE_ERR_BIT);
}
bool sense_enable(void)
{
    return (led_status_resister & SENSE_ERR_BIT);
}
bool pairing_enable(void)
{
    return (led_status_resister & PAIRING_BIT);
}
void wifi_connect_success(void)
{
    wifi_conn_enable = 100;
}
void led_bit_enable(uint16_t enable)
{
    // 현재 마스터 버퍼에 해당 비트가 꺼져 있을 때만 (즉, 새로 켜지는 순간에만) 진입!
    if ((led_status_resister & enable) == 0) 
    {
        led_status_resister |= enable;
        memset(&Breathing_Setting,0,sizeof(Breathing_Setting));
        ESP_LOGE(TAG, "led_status_resister = %08x",led_status_resister);
    }
}

void led_bit_disable(uint16_t disable)
{
    // 현재 마스터 버퍼에 해당 비트가 켜져 있을 때만 (즉, 새로 꺼지는 순간에만) 진입!
    if ((led_status_resister & disable) != 0) 
    {
        led_status_resister &= (~disable);
        memset(&Breathing_Setting,0,sizeof(Breathing_Setting));
        ESP_LOGE(TAG, "led_status_resister = %08x",led_status_resister);
    }
}


void init_led_strip(void) {
    // 1. 네오픽셀 기본 설정 (v3.x 최신 규격)
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = LED_NUMBERS,
        // ⚠️ v3.x에서는 아래와 같이 멤버명과 상수명이 바뀌었습니다!
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW, 
        .led_model = LED_MODEL_SK6812,
    };

    // 2. RMT 하드웨어 타이머 설정 (v3.x 최신 규격)
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // ⚠️ 클럭 소스를 명시적으로 지정해 주어야 합니다.
        .resolution_hz = 40 * 1000 * 1000, // 10MHz
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    // 3. ⚠️ v3.x 새 전용 초기화 함수 호출
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    set_led_clear();
}

void set_led_clear(void) {
    for(int i=0;i<LED_NUMBERS;i++)
    {
        led_strip_set_pixel_rgbw(led_strip, i, 0, 0, 0,0);
    }
    led_strip_refresh(led_strip); 
    
}


void set_rgb_led(uint8_t R, uint8_t G, uint8_t B, uint8_t W)
{
    static uint8_t R_buf, G_buf, B_buf, W_buf;
    if(R_buf == R && G_buf == G && B_buf == B && W_buf == W)
        return;
    R_buf = R;
    G_buf = G;
    B_buf = B;
    W_buf = W;

    for(int i=0;i<LED_NUMBERS;i++)
    {
         led_strip_set_pixel_rgbw(led_strip, i, R, G, B, W);
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
        led_bit_disable(TOF_DETECT_BIT); // ⭐️ 손 치우면 즉시 꺼짐 호출
        if (!is_tof_pressing) {
            tof_match_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            is_tof_pressing = true;
        } else {
            uint32_t elapsed_time = (xTaskGetTickCount() * portTICK_PERIOD_MS) - tof_match_start_time;
            if (elapsed_time >= 3000) {
                // ⭐️ 3초 만족 시 호출 -> 내부 가드 덕분에 매번 호출해도 세마포어는 딱 1번만 방출됨!
               
            }
        }
    }
    app_config_t* app_config = get_app_config();
    if(last_op_mode != app_config->op_mode && led_status_resister == 0)
    {       
        last_op_mode = app_config->op_mode; 
         ESP_LOGE(TAG, "last_op_mode = %08x",last_op_mode);
    }
}

static void Breathing_LED(void)
{
        if(Breathing_Setting.used == 0)
            return;
        // 1. [안전장치] 변수 오버플로우 방지 및 경계값 강제 제한
        if (Breathing_Setting.brightness > 255) Breathing_Setting.brightness = 255;
        if (Breathing_Setting.brightness < 0)   Breathing_Setting.brightness = 0;
        // 밝기 비율 계산 (0.00 ~ 1.00)
        float factor = (float)Breathing_Setting.brightness / 255.0f;

        // 현재 밝기가 적용된 RGBW 값 산출
        uint32_t r = (uint32_t)(Breathing_Setting.target_r * factor);
        uint32_t g = (uint32_t)(Breathing_Setting.target_g * factor);
        uint32_t b = (uint32_t)(Breathing_Setting.target_b * factor);
        uint32_t w = (uint32_t)(Breathing_Setting.target_w * factor);

        // 모든 LED에 색상 적용
      //  for (int i = 0; i < LED_NUMBERS; i++) {
       //     led_strip_set_pixel_rgbw(led_strip, i, r, g, b, w);
       // }
        set_rgb_led(r,g,b,w);
        // 데이터를 LED로 밀어내어 물리적 반영
        //led_strip_refresh(led_strip);
        static int hold_count = 0;

            if (hold_count > 0) {
                hold_count--; // 정점이나 바닥에 도달했을 때 지정된 횟수만큼 동작을 멈추고 대기
                return;
            }

            // 4. 밝기 증감 처리 및 정점 감성 제어
            Breathing_Setting.brightness += Breathing_Setting.step;

            if (Breathing_Setting.brightness >= 255 && Breathing_Setting.step > 0) {
                Breathing_Setting.brightness = 255; // 값을 확실히 255로 고정
                Breathing_Setting.step = -Breathing_Setting.step; // 방향 반전
                hold_count = 10;  // 💡 다 켜졌을 때 멈출 시간 (이 함수가 15ms 주기로 호출된다면 약 0.15초 멈춤)
            } 
            else if (Breathing_Setting.brightness <= 0 && Breathing_Setting.step < 0) {
                Breathing_Setting.brightness = 0;   // 값을 확실히 0으로 고정
                Breathing_Setting.step = -Breathing_Setting.step; // 방향 반전
                hold_count = 20;  // 💡 다 꺼졌을 때 멈출 시간 (약 0.3초 멈춤)
            }

}
static void LED_task(void *pvParameter)
{

    app_config_t* app_config = get_app_config();
    
    uint8_t toggle_time = 0;
    bool toggle_flag = false;
    static uint32_t _100ms_count = 0;
    set_rgb_led(0,0,0,LED_BRIGHTNESS_MAX);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "Starting LED_task (Pure Event Driven Mode)");
    DBG_Resister_t *DBG_Resister = Debug_Get();
    while (1) {
        #if 1
        if(_100ms_count >= (100 / LED_TASK_DELAY))
        {
            _100ms_count = 0 ;
            app_tof_sensor_poll_100ms();
        }
        else
            _100ms_count++;
        
   
        if(DBG_Resister->led)
        {

        }
        else
        {
            // [우선순위 1] 특수 비트가 하나라도 켜져 있는 상태라면
            if (led_status_resister != 0) {
                last_op_mode = -1; // 모드 무효화
                if((led_status_resister & HARDWARE_ERR_BIT) || (led_status_resister & SENSE_ERR_BIT))
                {
                    set_rgb_led(LED_BRIGHTNESS_MAX,0 , 0, 0); 
                }
                else if (led_status_resister & PAIRING_BIT) {
                    if(Breathing_Setting.used == 0)
                    {
                        Breathing_Setting.used = 1;
                        Breathing_Setting.target_b = 255;
                        Breathing_Setting.step = 2;
                        Breathing_Setting.brightness = 0; 
                    }
                    Breathing_LED();
                }
                else if (led_status_resister & OTA_START_BIT) {
                    if(toggle_time >= 2)
                    {
                        if(toggle_flag == true)
                        {
                            toggle_flag = false;
                            set_rgb_led(LED_BRIGHTNESS_MAX,0 , LED_BRIGHTNESS_MAX, 0);
                        }
                        else
                        {
                            toggle_flag = true;
                            set_rgb_led(0 ,0 , 0, 0); // 녹색
                        }
                        toggle_time = 0;
                    }
                    else
                        toggle_time++;

                }                  
                else if ((led_status_resister & TOF_DETECT_BIT) || (led_status_resister & CLEAN_MODE_BIT)){
                        set_rgb_led(0, LED_BRIGHTNESS_MAX, 0, 0); 
                }         
                

            }
            // [우선순위 2] 비트가 다 꺼진 정상 상태라면 op_mode 적용
            else {
                if(wifi_conn_enable)
                {
                    wifi_conn_enable--;
                    set_rgb_led(0, LED_BRIGHTNESS_MAX, 0, 0); 
                }
                else
                {
                    switch(last_op_mode) {
                        case OP_MODE_NORMAL: set_rgb_led(0, 0, 0, LED_BRIGHTNESS_MAX); break;
                        case OP_MODE_NIGHT:  set_rgb_led(0, 0, 0, LED_BRIGHTNESS_MAX/2); break;
                        case OP_MODE_SMART:  set_rgb_led(0,0 , LED_BRIGHTNESS_MAX, 0); break;
                        case OP_MODE_SLEEP:  set_rgb_led(0, 0, 0, 0);; break;
                        default: set_rgb_led(0, 0, 0, LED_BRIGHTNESS_MAX); break;
                    }
                }

                
            // ESP_LOGE(TAG, "last_op_mode = %08x",last_op_mode);
            }
        }
        // ⭐️ [중요] 처리가 다 끝난 시점에 마스터 버퍼를 업데이트하여 다음 외부 진입을 방어합니다.
        vTaskDelay(pdMS_TO_TICKS(LED_TASK_DELAY));
        #else
        // 원하는 색 조합으로 변경해 보세요.
            const uint32_t target_r = 255;
            const uint32_t target_g = 100;
            const uint32_t target_b = 0;
            const uint32_t target_w = 0;

            int brightness = 0;
            int step = 2; // 한 번에 변화할 밝기 크기 (작을수록 더 정밀하고 부드러워짐)

            while (1) {
                // 밝기 비율 계산 (0.00 ~ 1.00)
                float factor = (float)brightness / 255.0f;

                // 현재 밝기가 적용된 RGBW 값 산출
                uint32_t r = (uint32_t)(target_r * factor);
                uint32_t g = (uint32_t)(target_g * factor);
                uint32_t b = (uint32_t)(target_b * factor);
                uint32_t w = (uint32_t)(target_w * factor);

                // 모든 LED에 색상 적용
                for (int i = 0; i < LED_NUMBERS; i++) {
                    led_strip_set_pixel_rgbw(led_strip, i, r, g, b, w);
                }
                
                // 데이터를 LED로 밀어내어 물리적 반영
                led_strip_refresh(led_strip);

                // 밝기 증감 처리 (0 ~ 255 사이 왕복)
                brightness += step;
                if (brightness >= 255 || brightness <= 0) {
                    step = -step; // 최대/최소 도달 시 방향 반전
                }

                // 숨쉬기 속도 조절 (15ms 마다 갱신)
                vTaskDelay(pdMS_TO_TICKS(15));
            }        
        #endif
    }
}






void LED_task_init(void)
{
    TaskHandle_t xHandle = NULL;
    static uint8_t ucParameterToPass;
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




