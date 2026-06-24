#include "app_button.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_util.h"

#define BUTTON_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)
#define WIFI_TASK_DELAY_MS(x) (x/portTICK_PERIOD_MS)
static const char *TAG = __FILE__;
#define BUTTON_DEBOUNCE_TIME_MS 50
#define BUTTON_SINGLE_CLICK_TIME_MS 300
#define BUTTON_DOUBLE_CLICK_TIME_MS 600
#define BUTTON_LONG_PRESS_3_SEC 3000
#define BUTTON_LONG_PRESS_5_SEC 5000
#define BUTTON_LONG_PRESS_10_SEC 10000
static QueueHandle_t gpio_evt_queue = NULL;

typedef enum  {
    BUTTON_STATUS_IDLE,
    BUTTON_STATUS_PRESSED,
    BUTTON_STATUS_RELEASED
}eButtonStatus;

typedef enum {
    BUTTON_PRESSED,
    BUTTON_RELEASED,
    BUTTON_DOUBLE_CLICK,
    BUTTON_HOLD
} ButtonEvent;
eButtonStatus getButtonStatus(void);
void setButtonStatus(eButtonStatus status) ;
eButtonStatus button_status = BUTTON_STATUS_IDLE;
uint32_t button_press_start_time = 0;
uint32_t last_button_press_time = 0; // Added for double-click detection

// 1. [ISR] 인터럽트가 발생했을 때 실행되는 핸들러 함수 (매우 가볍게 작성해야 함)
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    static uint32_t lastInterruptTime = 0;
    static bool buttonPressed = false;

    uint32_t currentTime = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

    if (gpio_evt_queue == nullptr) {
        // Queue not initialized, ignore the interrupt
        return;
    }
    #if 1
    if ((currentTime - lastInterruptTime) > BUTTON_DEBOUNCE_TIME_MS) {
// 2. ⚠️ 추측하지 말고, 현재 핀의 실제 전기적 상태를 직접 읽습니다.
        // (PIN_PKEY_STAT 부분은 사용하시는 버튼 핀 번호 매크로로 넣어주세요)
        int pin_level = gpio_get_level((gpio_num_t)PIN_PKEY_STAT);
        
        ButtonEvent event;
        if (pin_level == 1) {
            // 풀업/풀다운 회로 구성에 따라 다릅니다. 
            // 일반적인 풀업(평소1, 누르면0) 기준이라면 1이 되었을 때가 RELEASED입니다.
            event = BUTTON_RELEASED; 
        } else {
            event = BUTTON_PRESSED;
        }

        // 3. 큐로 이벤트 전송
        xQueueSendFromISR(gpio_evt_queue, &event, nullptr);
        
        // 4. 마지막 인터럽트 발생 시간 갱신
        lastInterruptTime = currentTime;
    }
    #endif


}
// Button status setters and getters

void setButtonStatus(eButtonStatus status) {
    button_status = status;
}

eButtonStatus getButtonStatus(void) {
    return button_status;
}

void Button_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting button task");
   ButtonEvent event;
    while (1) {
        if (xQueueReceive(gpio_evt_queue, &event, portMAX_DELAY)) {
            uint32_t releaseTime = 0;
            uint32_t pressDuration = 0;
            uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
            APP_String_printf("Button Event: %s at %u ms \r\n", 
                  event == BUTTON_PRESSED ? "PRESSED" : "RELEASED", currentTime);

            switch (event) {
                case BUTTON_PRESSED:
                    if ((currentTime - last_button_press_time) <= BUTTON_DOUBLE_CLICK_TIME_MS) {
                        ButtonEvent doubleClickEvent = BUTTON_DOUBLE_CLICK;
                        xQueueSend(gpio_evt_queue, &doubleClickEvent, portMAX_DELAY);
                        APP_String_printf("Double Click Detected at %u ms\r\n", currentTime);
                    } else {
                        button_press_start_time = currentTime;
                        setButtonStatus(BUTTON_STATUS_PRESSED);
                        //APP_String_printf("Button Pressed at %u ms\r\n", button_press_start_time);
                    }
                    last_button_press_time = currentTime;
                    break;

                case BUTTON_RELEASED:
                    releaseTime = currentTime;
                    pressDuration = releaseTime - button_press_start_time;
                    setButtonStatus(BUTTON_STATUS_RELEASED);
                    //APP_String_printf("Button Released at %u ms, Duration: %u ms\r\n", releaseTime, pressDuration);

                    if (pressDuration >= BUTTON_LONG_PRESS_10_SEC) {
                        bf_LongPress10SecAction();
                    } else if (pressDuration >= BUTTON_LONG_PRESS_5_SEC) {
                        bf_LongPress5SecAction();
                    } else if (pressDuration >= BUTTON_LONG_PRESS_3_SEC) {
                        bf_LongPress3SecAction();
                    } else {
                        bf_SingleClickAction();
                    }
                    break;

                case BUTTON_DOUBLE_CLICK:
                    bf_DoubleClickAction();
                    break;

                case BUTTON_HOLD:
                    APP_String_printf("Button Hold Event Detected\r\n");
                    break;

                default:
                    APP_String_printf("Unknown Button Event\r\n" );
                    break;
            }
        }
    }
    
}
// Button actions

void bf_SingleClickAction(void) {
    //sendOpModeCmdEvent(OP_MODE_CMD_SHORT_PRESS);
    APP_String_printf("Single Click Action executed \r\n");
}

void bf_DoubleClickAction(void) {
    APP_String_printf("Double Click Action executed\r\n");
    // Add your double click action code here
    APP_String_printf("Performing double click specific operation...");
    // Example: Trigger OTA update or toggle a feature
}

void bf_LongPress3SecAction(void) {
    APP_String_printf("Long Press 3 Sec Action executed\r\n");
    // Add your long press 3 sec action code here

}
void bf_LongPress5SecAction(void)
{
    APP_String_printf("Long Press 5 Sec Action executed \r\n");
}

void bf_LongPress10SecAction(void) {
    APP_String_printf("Long Press 10 Sec Action executed \r\n");
    //delay(1000); // Wait for 1 second
    //ESP.restart();
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

    // 인터럽트 이벤트를 전달할 FreeRTOS 큐 생성 (크기 10)
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    

    // ISR 서비스 설치 (기본 설정 플래그 0)
    gpio_install_isr_service(0);
    
    // 특정 GPIO 핀에 ISR 핸들러 등록
    gpio_isr_handler_add(PIN_PKEY_STAT, gpio_isr_handler, (void*) PIN_PKEY_STAT);
    
    ESP_LOGI(TAG, "GPIO %d 인터럽트 설정 완료 (Any Edge)", PIN_PKEY_STAT);
    setButtonStatus(BUTTON_STATUS_RELEASED);

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