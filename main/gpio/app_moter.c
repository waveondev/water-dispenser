
#include <stdio.h>
#include "app_moter.h"

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


#include "driver/i2c.h"
#include "esp_log.h"
#include "app_adc.h"


#define MOTOR_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 1)

void TPL0401_init(void);
esp_err_t TPL0401_SetValue(int value);


#if !MOTOR_I2C_MODE
#define MOTOR_IN1_GPIO       (PIN_PUMP_PWM)
#endif

static const char *TAG = __FILE__;

#if MOTOR_I2C_MODE
    static int current_target_percentage = 100; 
#else
static int current_target_percentage = 0; 
#endif

static uint32_t* Motor_Used_Time;
static esp_timer_handle_t Motor_used_timer;


// 백그라운드 태스크 및 진행 상태 감시용 전역 변수
static TaskHandle_t xMotorBoostTaskHandle = NULL;
static bool filter_alert_enabled = false;
// 큐 핸들 변수 선언
SemaphoreHandle_t motor_semaphore = NULL;
static uint32_t* Motor_Used_Time;
static uint32_t* Filter_Used_Time;
#define SECONDS_IN_DAYS    (24UL * 60UL * 60UL)
static volatile bool filter_send_flag = false; // volatile 추가
static volatile bool moter_send_flag = false; // volatile 추가
static int start_count = 5;
static int start_off_time = 90;
static int start_on_time = 3000;
static uint32_t empty_count = 0;
void motor_start_set(int count,int off_time, int on_time)
{
    start_count = count ;
    start_off_time = off_time;
    start_on_time = on_time;
    printf("count = %d, off time = %d, on time = %d",count, off_time, on_time);
}

void set_filter_alert_enabled(bool state)
{
    filter_alert_enabled = state;
}
void motor_change(void)
{
    (*Motor_Used_Time) = 0;
    motor_nvs_save_set();
    led_bit_disable(FILTER_WATER_BIT);
    water_fault_disable(WATER_FILTER_WATER_EX);
    moter_send_flag = false;
}
void filter_change(void)
{
    (*Filter_Used_Time) = 0;
    filter_nvs_save_set();
    led_bit_disable(FILTER_DEBRIS_BIT);
    water_fault_disable(WATER_FILTER_DEBRIS_EX);
    filter_send_flag = false;
}
static void motor_used_timer_callback(void* arg)
{
    if(Water_empty_enable())
        return;
    app_config_t* app_config = get_app_config();
    (*Motor_Used_Time)++;
    if(*Motor_Used_Time >= (app_config->moter_life_days *SECONDS_IN_DAYS) )
    {
        if(filter_alert_enabled == true)
        {
            water_fault_enable(WATER_FILTER_WATER_EX);
            led_bit_enable(FILTER_WATER_BIT);
            if(moter_send_flag == false)
            {
                moter_send_flag = true;
                motor_nvs_save_set();
            }
        }
    }

    (*Filter_Used_Time)++;
    if(*Filter_Used_Time >= (app_config->filter_life_days *SECONDS_IN_DAYS) )
    {
        if(filter_alert_enabled == true)
        {        
            water_fault_enable(WATER_FILTER_DEBRIS_EX);
            led_bit_enable(FILTER_DEBRIS_BIT);
            if(filter_send_flag == false)
            {
                filter_send_flag = true;
                filter_nvs_save_set();
            }
        }
    }


    static uint32_t Minute_15 = 900;
    Minute_15--;
    if(Minute_15 == 0)
    {
        Minute_15 = 900;
        motor_ontime_nvs_save_set();
    } 
}

static void motor_boost_task(void *pvParameters)
{

    int current_value = 0;
    bool motor_en = read_nvs_motor_en_flag();
    while(1)
    {
        if (xSemaphoreTake(motor_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) 
        {
            ESP_LOGW("RECEIVER", "큐 수신 완료! -> [모터 구동] %d",current_target_percentage);
            
            #if MOTOR_I2C_MODE
                TPL0401_SetValue(0);

                #if 1
  
                    vTaskDelay(pdMS_TO_TICKS(2000)); // 정확히 1초(1000ms)만 대기   
                    for(current_value = 40;current_value<90;current_value++)
                    {
                        TPL0401_SetValue(current_value);
                        vTaskDelay(pdMS_TO_TICKS(200)); // 정확히 1초(1000ms)만 대기   
                        // ESP_LOGI(TAG,"100 = %d",i);
                    }


                TPL0401_SetValue(0);   

                
                #endif
                //TPL0401_SetValue(current_target_percentage);
            #else

            if(current_target_percentage)
            {
                    gpio_set_level(MOTOR_IN1_GPIO, 1);   
                    vTaskDelay(pdMS_TO_TICKS(4000)); // 정확히 1초(1000ms)만 대기      
                    for(int z = 0; z < start_count;z++)
                    {   

                        gpio_set_level(MOTOR_IN1_GPIO, 0);   
                        vTaskDelay(pdMS_TO_TICKS(start_off_time)); // 정확히 1초(1000ms)만 대기     
                        gpio_set_level(MOTOR_IN1_GPIO, 1);   
                        vTaskDelay(pdMS_TO_TICKS(start_on_time)); // 정확히 1초(1000ms)만 대기                            
                        ESP_LOGI(TAG,"z = %d", z);
                    }            
            }


            #endif   
            if(current_target_percentage)
                ESP_ERROR_CHECK(esp_timer_start_periodic(Motor_used_timer, 1000000));       
        }
        #if MOTOR_I2C_MODE        
        if(current_target_percentage != 100)
        #else
        if(current_target_percentage != 0)
        #endif
        {
            if(GetMotor_adc() < 250)
            {
                if(empty_count < 10)
                {
                    empty_count++;
                }
            }
            else
            {
                if(motor_en == false)
                {
                    motor_en = true;
                    write_nvs_motor_en_flag(motor_en);
                }
                if(empty_count)
                empty_count--;
            }                
            if(empty_count >= 5)
            {
                //물없음
                led_bit_enable(WATER_EMPTY_BIT);
                water_fault_enable(WATER_EMPTY_FAULT);      
            }
            else
            {
                led_bit_disable(WATER_EMPTY_BIT);
                water_fault_disable(WATER_EMPTY_FAULT); 
            }
        }
    }
}


void start_motor_with_boost(int target_percentage, int duration_sec)
{
#if MOTOR_I2C_MODE
    target_percentage = 100 - target_percentage;
    if (target_percentage >= 100 && current_target_percentage != target_percentage ) {
       // ESP_LOGI(TAG, "[PUMP] 🛑 모터 즉시 정지 (0%%)");
        current_target_percentage = target_percentage;
        TPL0401_SetValue(current_target_percentage);
        return; 
    }
#else
    if (target_percentage <= 0) {
       // ESP_LOGI(TAG, "[PUMP] 🛑 모터 즉시 정지 (0%%)");
        gpio_set_level(MOTOR_IN1_GPIO, 0);   
        current_target_percentage = 0;
        return; 
    }
#endif
    if (target_percentage == current_target_percentage) {
        return; 
    }

    current_target_percentage = target_percentage;

    if (motor_semaphore != NULL) {
        xSemaphoreGive(motor_semaphore); // 세마포어에 신호(1) 채우기
        ESP_LOGI("SENDER", "모터 동작 세마포어 송신!");
    }
}

void init_motor_ledc(bool qc_mode) {
    #if MOTOR_I2C_MODE
    
    gpio_config_t io_conf = {                   
        .pin_bit_mask =(1ULL << PUMP_EN),             // 설정할 GPIO 핀 15, 16, 2 지정
        .mode = GPIO_MODE_OUTPUT,             // 출력 모드로 설정
        .pull_up_en = GPIO_PULLUP_DISABLE,    // 내부 풀업 비활성화
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 내부 풀다운 활성화 (기본 LOW 상태 유지)
        .intr_type = GPIO_INTR_DISABLE,       // 인터럽트 사용 안 함
    };
    gpio_config(&io_conf);    
    gpio_set_level(PUMP_EN, 1); 
    #else
    gpio_config_t io_conf = {                   
        .pin_bit_mask =(1ULL << MOTOR_IN1_GPIO),             // 설정할 GPIO 핀 15, 16, 2 지정
        .mode = GPIO_MODE_OUTPUT,             // 출력 모드로 설정
        .pull_up_en = GPIO_PULLUP_DISABLE,    // 내부 풀업 비활성화
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 내부 풀다운 활성화 (기본 LOW 상태 유지)
        .intr_type = GPIO_INTR_DISABLE,       // 인터럽트 사용 안 함
    };
    gpio_config(&io_conf);    
    gpio_set_level(MOTOR_IN1_GPIO, 0);     

    #endif


    Motor_Used_Time = get_motor_time();
    Filter_Used_Time = get_filter_time();
    
    const esp_timer_create_args_t Motor_Used_timer_args = {
        .callback = &motor_used_timer_callback,
        .name = "Motor_used_timer"
    };

    // 타이머 생성
    ESP_ERROR_CHECK(esp_timer_create(&Motor_Used_timer_args, &Motor_used_timer));
    #if MOTOR_I2C_MODE
    TPL0401_init();
    #endif
// 2. 이진 세마포어 생성 (초기 상태: 0 - 비어있음)
    motor_semaphore = xSemaphoreCreateBinary();
    
    if (motor_semaphore == NULL) {
        ESP_LOGE("MAIN", "세마포어 생성 실패!");
    }

    if (xTaskCreate(
            motor_boost_task,                  // 태스크 함수
            "motor_boost_task",                // 태스크 이름
            MOTOR_TASK_STACK_SIZE,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 1,      // 우선순위
            NULL                  // 태스크 핸들
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating motor_boost_task on Core 1");
    }

    if(qc_mode == true)
        start_motor_with_boost(100,0);

}




#define TPL0401_ADDR      0x2E
#define TPL0401_REG_WIPER 0x00

#define I2C_PORT           I2C_NUM_0

esp_err_t TPL0401_SetValue(int value)
{
    // 범위 제한
    if (value < 0) {
        value = 0;
    }

    if (value > 127) {
        value = 127;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);

    // Slave Address + Write
    i2c_master_write_byte(
        cmd,
        (TPL0401_ADDR << 1) | I2C_MASTER_WRITE,
        true
    );

    // Wiper Register
    i2c_master_write_byte(
        cmd,
        TPL0401_REG_WIPER,
        true
    );

    // Wiper Value
    i2c_master_write_byte(
        cmd,
        (uint8_t)value,
        true
    );

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(
        I2C_PORT,
        cmd,
        pdMS_TO_TICKS(100)
    );

    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wiper = %d", value);
    } else {
        ESP_LOGE(TAG, "Set Wiper failed: %s",
                 esp_err_to_name(ret));
    }

    return ret;
}

/**
 * TPL0401 Wiper 값 쓰기
 *
 * value : 0 ~ 127
 */
esp_err_t TPL0401_Write(uint8_t value)
{
    if (value > 127) {
        value = 127;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);

    // Slave Address + Write
    i2c_master_write_byte(
        cmd,
        (TPL0401_ADDR << 1) | I2C_MASTER_WRITE,
        true
    );

    // Register Address
    i2c_master_write_byte(
        cmd,
        TPL0401_REG_WIPER,
        true
    );

    // Wiper Data
    i2c_master_write_byte(
        cmd,
        value,
        true
    );

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(
        I2C_PORT,
        cmd,
        pdMS_TO_TICKS(100)
    );

    i2c_cmd_link_delete(cmd);

    return ret;
}


/**
 * TPL0401 Wiper 값 읽기
 */
esp_err_t TPL0401_Read(uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    /*
     * 먼저 Register Address 0x00 지정
     */
    i2c_master_start(cmd);

    i2c_master_write_byte(
        cmd,
        (TPL0401_ADDR << 1) | I2C_MASTER_WRITE,
        true
    );

    i2c_master_write_byte(
        cmd,
        TPL0401_REG_WIPER,
        true
    );

    /*
     * Repeated START 후 Read
     */
    i2c_master_start(cmd);

    i2c_master_write_byte(
        cmd,
        (TPL0401_ADDR << 1) | I2C_MASTER_READ,
        true
    );

    i2c_master_read_byte(
        cmd,
        value,
        I2C_MASTER_NACK
    );

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(
        I2C_PORT,
        cmd,
        pdMS_TO_TICKS(100)
    );

    i2c_cmd_link_delete(cmd);

    return ret;
}


void i2c_scan(void)
{
    for (uint8_t addr = 1; addr < 127; addr++) {

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();

        i2c_master_start(cmd);

        i2c_master_write_byte(
            cmd,
            (addr << 1) | I2C_MASTER_WRITE,
            true
        );

        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(
            I2C_NUM_0,
            cmd,
            pdMS_TO_TICKS(100)
        );

        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "FOUND: 0x%02X", addr);
        }
    }
}
void TPL0401_init(void)
{
    i2c_config_t i2c_bus0_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TPL_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = TPL_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000, // 400kHz
    };
    
    if (i2c_param_config(I2C_NUM_0, &i2c_bus0_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config I2C Port 0");
    }
    if (i2c_driver_install(I2C_NUM_0, i2c_bus0_cfg.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C Port 0");
    }
    i2c_scan();
    TPL0401_SetValue(100);
}


