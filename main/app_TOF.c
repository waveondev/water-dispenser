#include "app_TOF.h"
#include "gpio_util.h"
#include "esp_system.h"
#include "esp_err.h"

#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "VL53L0X.h"

static const char *TAG = __FILE__;


static bool g_tof0_ok = false;
static uint32_t g_tof0_last_ok_ms = 0;
static VL53L0X_t g_tof0;

static bool g_tof1_ok = false;
static uint32_t g_tof1_last_ok_ms = 0;
static VL53L0X_t g_tof1;

uint16_t tof0_mm;
uint16_t tof1_mm;
uint32_t ts_tof0_ms;
uint32_t ts_tof1_ms;


void VL53L0X_Sensing(void)
{
    uint32_t tof_ms;
    tof_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // -------------------------------------------------------------
    // 1. TOF0 센서 값 읽기 예제
    // -------------------------------------------------------------
    if (g_tof0_ok) {
        // 하드웨어가 연속 측정중인 버퍼에서 최신 거리 값(mm)을 빼옵니다.
        uint16_t dist0 = VL53L0X_readRangeContinuousMillimeters(&g_tof0);

        // 내부 타이머 카운트를 초과하여 타임아웃 에러가 발생했는지 검증
        if (VL53L0X_timeoutOccurred(&g_tof0)) {
            ESP_LOGE(TAG, "TOF0 Read Timeout! (Value: %d)", dist0);
            tof0_mm = 0; // 에러 시 안전한 기본값 대입
        } else {
            tof0_mm = dist0;
            g_tof0_last_ok_ms = tof_ms; // 마지막 정상 수신 시간 갱신
        }
    } 
    else {
        tof0_mm = 0; // 초기화 자체가 실패했던 경우
    }
    // -------------------------------------------------------------
    // 2. TOF1 센서 값 읽기 예제
    // -------------------------------------------------------------
    if (g_tof1_ok) {
        uint16_t dist1 = VL53L0X_readRangeContinuousMillimeters(&g_tof1);

        if (VL53L0X_timeoutOccurred(&g_tof1)) {
        ESP_LOGE(TAG, "TOF1 Read Timeout! (Value: %d)", dist1);
        tof1_mm = 0;
        } 
        else 
        {
            tof1_mm = dist1;
            g_tof1_last_ok_ms = tof_ms;
        }      
    } 
    else {
        tof1_mm = 0;
    }

    // -------------------------------------------------------------
    // 3. 수신 결과 디버그 출력 및 전역 데이터 전송
    // -------------------------------------------------------------
    APP_String_printf("Measured Distance -> TOF0: %d mm, TOF1: %d mm\r\n", tof0_mm, tof1_mm); 
}

bool TOF_VL53L0X_init(void)
{

#if 1

    i2c_config_t i2c_bus1_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOF1_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_TOF1_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    
    if (i2c_param_config(I2C_NUM_1, &i2c_bus1_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config I2C Port 1");
    }
    if (i2c_driver_install(I2C_NUM_1, i2c_bus1_cfg.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C Port 1");
    }

    g_tof1_ok = false;

    // 버스 1번에 TOF1 지정 및 초기화 (주소 0x29)
    VL53L0X_setBus(&g_tof1, I2C_NUM_1, ADDRESS_DEFAULT);
    if (VL53L0X_init(&g_tof1, true)) {
        g_tof1_ok = true;
        VL53L0X_startContinuous(&g_tof1, 0); // 👈 [추가] TOF1 측정 하드웨어 시동!
    }
    else
    {
        ESP_LOGE(TAG, "Failed to init I2C Port 1");
        return g_tof1_ok;
    }
    

#endif

// -------------------------------------------------------------
    // 2. I2C 포트 0 초기화 (TOF0용)
    // -------------------------------------------------------------
    i2c_config_t i2c_bus0_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOF0_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_TOF0_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000, // 400kHz
    };
    
    if (i2c_param_config(I2C_NUM_0, &i2c_bus0_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config I2C Port 0");
    }
    // 버퍼 크기를 0으로 주어 마스터 모드로 드라이버 설치
    if (i2c_driver_install(I2C_NUM_0, i2c_bus0_cfg.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C Port 0");
    }

    g_tof0_ok = false;
    // 버스 0번에 TOF0 지정 및 초기화 (주소 0x29)
    VL53L0X_setBus(&g_tof0, I2C_NUM_0, ADDRESS_DEFAULT);
    if (VL53L0X_init(&g_tof0, true)) {
        g_tof0_ok = true;
        VL53L0X_startContinuous(&g_tof0, 0); // 👈 [추가] TOF0 측정 하드웨어 시동! (주기 0 = 무한 연속 측정)
    }
    else
    {
        ESP_LOGE(TAG, "Failed to install I2C Port 0");
        return g_tof0_ok;
    }

    return true;
}


