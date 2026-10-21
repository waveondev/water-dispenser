
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
#include "driver/rmt_tx.h"
#include "app_adc.h"

#if 0
#else

#define MOTOR_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 1)

void TPL0401_init(void);
esp_err_t TPL0401_SetValue(int value);
rmt_channel_handle_t pwm_chan = NULL;
rmt_encoder_handle_t copy_encoder = NULL;

#define MOTOR_IN1_GPIO       (PIN_PUMP_PWM)
#define LEDC_FREQUENCY      ( 20 * 1000 * 1000)            // 20kHz 설정
static const char *TAG = __FILE__;
static int current_target_percentage = 0; 
static uint32_t* Motor_Used_Time;
static esp_timer_handle_t Motor_used_timer;

// 모터 제어 함수 (percentage: 0 ~ 100)
void set_motor_speed_percent(int percentage) {
// DMA가 없을 때는 internal RAM을 채우기 위해 4개 심볼 배열 구성
    static rmt_symbol_word_t pwm_symbols[4]; 
    
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    uint32_t total_ticks = 1000; // 1000틱 = 50us (20kHz)
    uint32_t high_ticks = (total_ticks * percentage) / 100;
    uint32_t low_ticks = total_ticks - high_ticks;

    // 타이머 제어
    if (esp_timer_is_active(Motor_used_timer)) {
        esp_timer_stop(Motor_used_timer);
    }
    if (percentage > 0) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(Motor_used_timer, 1000000));
    }

    // 듀티에 따른 심볼 패턴 작성
    rmt_symbol_word_t sym;
    if (percentage == 100) {
        sym.level0 = 1; sym.duration0 = total_ticks / 2;
        sym.level1 = 1; sym.duration1 = total_ticks / 2;
    } else if (percentage == 0) {
        sym.level0 = 0; sym.duration0 = total_ticks / 2;
        sym.level1 = 0; sym.duration1 = total_ticks / 2;
    } else {
        sym.level0 = 1; sym.duration0 = high_ticks;
        sym.level1 = 0; sym.duration1 = low_ticks;
    }

    // 4개 심볼 공간에 동일 파형 복사 (Internal RAM 순환 안정화)
    for (int i = 0; i < 4; i++) {
        pwm_symbols[i] = sym;
    }

    // 3. 무한 루프 송출 설정 (-1)
    rmt_transmit_config_t tx_config = {
        .loop_count = -1, 
    };

    rmt_disable(pwm_chan); 
    rmt_enable(pwm_chan);

    // 4. DMA를 통해 심볼 전송 시작 (기존 송출을 덮어씀)
    ESP_ERROR_CHECK(rmt_transmit(pwm_chan, copy_encoder, pwm_symbols, sizeof(pwm_symbols), &tx_config));

}


// 백그라운드 태스크 및 진행 상태 감시용 전역 변수
static TaskHandle_t xMotorBoostTaskHandle = NULL;

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
    if(hardware_error_enable())
        return;
    app_config_t* app_config = get_app_config();
    (*Motor_Used_Time)++;
    if(*Motor_Used_Time >= (app_config->moter_life_days *SECONDS_IN_DAYS) )
    {
        water_fault_enable(WATER_FILTER_WATER_EX);
        led_bit_enable(FILTER_WATER_BIT);
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
        led_bit_enable(FILTER_DEBRIS_BIT);
        if(filter_send_flag == false)
        {
            filter_send_flag = true;
            filter_nvs_save_set();
        }
    }
}

static void motor_boost_task(void *pvParameters)
{
    static uint32_t empty_count = 0;
    int current_value = 0;
    int min_value = 0;
    while(1)
    {
        if (xSemaphoreTake(motor_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) 
        {
            ESP_LOGW("RECEIVER", "큐 수신 완료! -> [모터 구동] %d",current_target_percentage);
            
            #if MOTOR_I2C_MODE
                TPL0401_SetValue(0);
                current_value = current_target_percentage;
                if(min_value == 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(1000)); // 정확히 1초(1000ms)만 대기   
                    for(;current_value<100;current_value++)
                    {
                        TPL0401_SetValue(current_value);
                        vTaskDelay(pdMS_TO_TICKS(100)); // 정확히 1초(1000ms)만 대기   
                        // ESP_LOGI(TAG,"100 = %d",i);
                        if(GetMotor_adc() < 30)
                        {
                            min_value = current_value - 4;
                            break;;
                        }
                        if(current_target_percentage == 100)
                        {
                            TPL0401_SetValue(current_target_percentage);
                            break;
                        }
                    }
                }
                ESP_LOGW("min_value", " %d",min_value);
                TPL0401_SetValue(0);  
                for(int i = 0; i <30;i++)
                {

                    vTaskDelay(pdMS_TO_TICKS(100)); // 정확히 1초(1000ms)만 대기   
                    if(current_target_percentage == 100)
                    {
                        TPL0401_SetValue(current_target_percentage);
                        break;
                    }
                }
                { 
                    for(int i = 30 ;i<min_value;i++)
                    {

                        TPL0401_SetValue(i);
                         ESP_LOGI(TAG,"i = %d",i);
                        if(current_target_percentage == 100)
                        {
                            TPL0401_SetValue(current_target_percentage);
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(50)); // 정확히 1초(1000ms)만 대기   
                    }
                    for(int i = min_value;i>30;i--)
                    {
                        TPL0401_SetValue(i);
                         ESP_LOGI(TAG,"i = %d",i);
                        if(current_target_percentage == 100)
                        {
                            TPL0401_SetValue(current_target_percentage);
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(50)); // 정확히 1초(1000ms)만 대기   
                    }
                    for(int i = 30 ;i<=min_value;i++)
                    {
                        TPL0401_SetValue(i);
                         ESP_LOGI(TAG,"i = %d",i);
                        if(current_target_percentage == 100)
                        {
                            TPL0401_SetValue(current_target_percentage);
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(50)); // 정확히 1초(1000ms)만 대기   
                    }
                }
                //TPL0401_SetValue(min_value);


            #else
   
                    set_motor_speed_percent(100);

                for(int i = 0;i<5;i++)
                {
                    vTaskDelay(pdMS_TO_TICKS(100)); // 정확히 1초(1000ms)만 대기   
                    // ESP_LOGI(TAG,"100 = %d",i);
                    if(current_target_percentage == 0)
                    break;
                }

            
  
                set_motor_speed_percent(current_target_percentage);
            #endif                
        }
        #if MOTOR_I2C_MODE        
        if(current_target_percentage != 100)
        #else
        if(current_target_percentage != 0)
        #endif
        {

            {
                if(GetMotor_adc() < 100)
                {
                    if(empty_count < 10)
                    {
                        empty_count++;
                    }

                }
                else
                {
                    if(empty_count)
                    empty_count--;

                }                
            }


            {
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
                if(empty_count)
                    ESP_LOGI(TAG,"empty_count = %d", empty_count);
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
        set_motor_speed_percent(0);
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

void init_motor_ledc(void) {
    // 1. RMT TX 채널 설정 (DMA 활성화)
    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = MOTOR_IN1_GPIO,             // 👈 출력할 GPIO 15번 핀
        .mem_block_symbols = 48,    // DMA 내부 블록 사이즈
        .resolution_hz = LEDC_FREQUENCY,  // 🌟 20MHz 해상도 (1틱 = 50ns)
        .trans_queue_depth = 4,
        .flags.with_dma = false,     // 🌟 핵심: DMA 통신 켜기
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
    #if MOTOR_I2C_MODE
    TPL0401_init();
    #endif
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
    current_target_percentage = 100;
    TPL0401_SetValue(current_target_percentage);    

}
#endif

