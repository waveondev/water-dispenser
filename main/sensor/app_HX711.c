#include "app_HX711.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gpio_util.h"
#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config_flash.h"
#include "app_led.h"
#include "debug_cli.h"
#include "tx_mqtt.h"
#include "aws_iot_task.h"
#include "setting_cmd.h"
static const char *TAG = __FILE__;

static float filtered_weight = 0.0f; // 현재 필터링된 최종 무게값
#include "math.h"


#include "hx711_lib.h"
static int32_t hx711_data;
static float hx711_data_buf;

hx711_t dev = {
    .dout = PIN_HX711_DOUT,
    .pd_sck = PIN_HX711_SCK,
    .gain = HX711_GAIN_A_64
};
static uint16_t hx711_cal_enable = 0;
// 파일 상단 static 변수에 추가
static int32_t min_raw_after_case = 0;   // 케이스 일부 제거 후 최저 안정화 Raw 값(80,000) 자동 저장용
static bool track_min_raw = false;       // 케이스 캘리브레이션 직후부터 추적 활성화 플래그

static void HX711_scale_process(float weight)
{
    app_config_t* app_config = get_app_config();
    app_facto_config_t* facto_config = get_facto_config();


    hx711_calibrate_scale(&dev,100,weight,&app_config->hx1_scale,(int32_t)app_config->case_raw_data);
    facto_config->hx1_scale = app_config->hx1_scale;
    app_nvs_save_set();
    facto_nvs_save_set();
    APP_String_printf("Tare case set to %d(%.2f)\r\n", app_config->case_raw_data,(int32_t)app_config->hx1_scale);
    Setting_Enable(HX711_Scale_cal);
}

static void HX711_case_raw_process(void)
{
    app_config_t* app_config = get_app_config();
    app_facto_config_t* facto_config = get_facto_config();
    int32_t cal_data = 0;

    while(hx711_read_average(&dev, 100, &cal_data) != ESP_OK){}
    app_config->case_raw_data = (cal_data);
    facto_config->case_raw_data = app_config->case_raw_data;    
    min_raw_after_case = cal_data;
    track_min_raw = true;
    app_nvs_save_set();
    facto_nvs_save_set();
    APP_String_printf("Tare case set to %d\r\n", app_config->case_raw_data);

    Setting_Enable(HX711_Case_cal);
}
void HX711_cal_init(uint16_t cal)
{
    hx711_cal_enable = cal;
}
float loadcell_data_get(void)
{
    return hx711_data_buf;
}

void HX711_Sensing(void)
{
    static bool first_init  = false;
    static float before_weight = 0;
    static uint32_t bowl_count = 0;
    static uint32_t water_low_count = 0;
    static float water_increase_data = 0;
    static uint32_t water_increase_count = 0;
    esp_err_t r;
    DBG_Resister_t *DBG_Resister = Debug_Get();
    app_config_t* app_config = get_app_config();

    r = hx711_read_average(&dev, 10, &hx711_data);

    if (r != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not read data: %d (%s)", r, esp_err_to_name(r));
        return;
    }
    // 1. 순수 차이값(음수 포함)을 정수로 먼저 계산
    int32_t net_raw = hx711_data - app_config->case_raw_data;

    // 2. float 변수에 대입하여 명확하게 float으로 변환 후 나눗셈
    float net_raw_float = (float)net_raw;
    hx711_data_buf = (net_raw_float / app_config->hx1_scale);
    if(water_increase_data == 0)
        water_increase_data = hx711_data_buf;
    if(DBG_Resister->HX711)
    {
        APP_String_printf( "hx711_data: (%d)\r\n",hx711_data);
        APP_String_printf(" Raw: %.2f g\r\n", hx711_data_buf);    
    }

    if(hx711_data_buf > water_increase_data + 2.0f)
    {  
        water_increase_count++;
    }
    else
    {
        if(water_increase_count > 10)
        {
            ESP_LOGI(TAG, "water_increase = %d", water_increase_count);

            //물붓기

        }
        water_increase_count = 0;
    }
    water_increase_data = hx711_data_buf;




    float safe_min_threshold = (float)app_config->min_weight_threshold; 

    //if(before_weight == 0.0f)
        //before_weight = loadcell_data_get();
    
    float currunt_weight = loadcell_data_get();
    if(first_init == false)
    {
        first_init = true;
        before_weight = currunt_weight;
    }
    float safe_release_threshold = safe_min_threshold + 10.0f; 
    // 3. 이전 무게보다 50 이상 갑자기 줄었는지 검사
    if (currunt_weight < 0 && currunt_weight < (before_weight - 50.0f))
    {
        if(bowl_count < 10)
            bowl_count++;
    }
    else 
    {
        // 정상 범위(무게가 유지되거나 늘어나거나, 50 미만으로 살짝 줄었을 때)는
        // 현재 무게를 다음 비교를 위한 before_weight로 갱신
        
        before_weight = currunt_weight;
        bowl_count = 0;
    }

    if(bowl_count >= 10)
    {
        if(!led_bit_status(HARDWARE_ERR_BIT))
        {
            led_bit_enable(HARDWARE_ERR_BIT);
            water_fault_enable(WATER_BOWL_DETACHED_FAULT);
        }   
    }
    else
    {
        if(led_bit_status(HARDWARE_ERR_BIT))
        {
            led_bit_disable(HARDWARE_ERR_BIT);
            water_fault_disable(WATER_BOWL_DETACHED_FAULT);
        }      

        // 💡 이제 안전한 로컬 변수끼리만 비교합니다.
        if (loadcell_data_get() < safe_min_threshold) // 물부족
        {
            if(water_low_count < 10)
                water_low_count++;

        }
        else
        {
            water_low_count = 0;
        }

        if(water_low_count >= 10)
        {
            if(!led_bit_status(WATER_LOW_BIT))
            {
                led_bit_enable(WATER_LOW_BIT);
                water_fault_enable(WATER_LOW_FAULT);
            }       
        }
        else
        {
            if(led_bit_status(WATER_LOW_BIT))
            {
                led_bit_disable(WATER_LOW_BIT);
                water_fault_disable(WATER_LOW_FAULT);
            }
        }
    }

    // 💡 3. 에러 해제 조건식도 안전한 로컬 변수로 교체합니다.
    // 흔들림 방지(히스테리시스)를 위해 임계값(200)보다 1g 큰 safe_min_threshold + 1.0f(즉, 201.0f)로 대칭을 맞춥니다.


 
}

#define HX711_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 1)

static void HX711_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting sensor task");
    
    while (1) {
        if(hx711_cal_enable)
        {
            if(hx711_cal_enable == 1)
                HX711_case_raw_process();
            else
                HX711_scale_process((float)hx711_cal_enable);
            hx711_cal_enable = 0;

        }    
        else{
            static int32_t prev_raw = 0;
            static uint8_t stable_cnt = 0;

            // ... (기존 hx711_read_average(&dev, 10, &hx711_data) 및 에러 체크 코드) ...

            // [추가] 케이스 일부를 빼서 무게가 줄어들었을 때의 안정화 값(80,000)을 자동으로 잡는 로직
            if (track_min_raw) {
                if (abs(hx711_data - prev_raw) < 300) { // 흔들림 없이 안정된 상태일 때만 체크
                    if (stable_cnt < 10) {
                        stable_cnt++;
                    } else {
                        // 기존에 기록된 값보다 더 가벼워진 안정화 값이 나오면 갱신 (100,000 -> 80,000)
                        // 나중에 2kg 추를 올려 값이 커지면(280,000) 이 조건을 안 타므로 80,000이 안전하게 보존됨!
                        if (hx711_data < min_raw_after_case) {
                            min_raw_after_case = hx711_data;
                        }
                    }
                } else {
                    stable_cnt = 0; // 손으로 케이스를 빼거나 추를 올리는 도중에는 갱신 보류
                }
                prev_raw = hx711_data;
            }            
            HX711_Sensing();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}


bool HX711_task_init(void)
{
#if 1

    if(hx711_init(&dev) != ESP_OK)
    {
        ESP_LOGE(TAG, "HX711 Error\r\n");
        led_bit_enable(LOADCELL_ERR_BIT);
        water_fault_disable(WATER_LOADCELL_ERR);
    }
    HX711_Sensing();
    if (xTaskCreate(
            HX711_task,                  // 태스크 함수
            "HX711_task",                // 태스크 이름
            HX711_TASK_STACK_SIZE,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 1,      // 우선순위
            NULL
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        ESP_LOGE(TAG, "Error creating Sensor_task on Core 1");
    }
#endif

    return true;
}


