#include "app_TOF.h"
#include "gpio_util.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"

#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform.h"
#include "app_config_flash.h"
uint32_t _trace_level;
int _modules;
static const char *TAG = __FILE__;

// ⭐️ ST 공식 API용 디바이스 구조체 전역 변수 선언 (두 채널 분리)
static VL53L0X_Dev_t dev_tof0;
static VL53L0X_Dev_t dev_tof1;

static bool g_tof0_ok = false;
static uint32_t g_tof0_last_ok_ms = 0;

static bool g_tof1_ok = false;
static uint32_t g_tof1_last_ok_ms = 0;

static uint16_t tof0_mm;
static uint16_t tof1_mm;
uint32_t ts_tof0_ms = 0;
uint32_t ts_tof1_ms = 0;

// 💡 내부 헬퍼 함수: 단일 센서 ST C API 초기화 및 아크릴 보정 
static bool init_single_vl53l0x(VL53L0X_Dev_t *pDevice, i2c_port_t i2c_port, const char *sensor_name)
{
    VL53L0X_Error status;
    uint32_t refSpadCount;
    uint8_t isApertureSpads;
    uint8_t VhvSettings;
    uint8_t PhaseCal;

    // 1. 플랫폼 바인딩 세팅 (I2C 포트와 기본 주소 0x29)
    pDevice->i2c_port_num = i2c_port;
    pDevice->i2c_address  = 0x29; 

    // 2. ST C API 기본 데이터 및 스태틱 초기화
    status = VL53L0X_DataInit(pDevice);
    if (status != VL53L0X_ERROR_NONE) {
        ESP_LOGE(TAG, "[%s] DataInit Failed: %d", sensor_name, status);
        return false;
    }
    status = VL53L0X_StaticInit(pDevice);
    if (status != VL53L0X_ERROR_NONE) {
        ESP_LOGE(TAG, "[%s] StaticInit Failed: %d", sensor_name, status);
        return false;
    }

    // 3. SPAD 관리 및 내부 기준치 온도시정(Calibration)
    status = VL53L0X_PerformRefSpadManagement(pDevice, &refSpadCount, &isApertureSpads);
    if (status != VL53L0X_ERROR_NONE) return false;

    status = VL53L0X_PerformRefCalibration(pDevice, &VhvSettings, &PhaseCal);
    if (status != VL53L0X_ERROR_NONE) return false;

    // 4. 싱글 측정 모드 및 타임예산(33ms) 세팅
    status = VL53L0X_SetDeviceMode(pDevice, VL53L0X_DEVICEMODE_SINGLE_RANGING);
    if (status != VL53L0X_ERROR_NONE) return false;

    status = VL53L0X_SetMeasurementTimingBudgetMicroSeconds(pDevice, 33000);
    if (status != VL53L0X_ERROR_NONE) return false;

    // 5. ⭐️ [핵심] 아크릴 크로스토크(Xtalk) 자동 학습 보정 ⭐️
    // ⚠️ 주의: 기기에 아크릴을 조립한 상태에서 센서 정면 정확히 10cm(100mm) 지점에 하얀 벽/종이를 대고 전원을 켜야 합니다!
    FixPoint1616_t target_distance_mm = 100;
    uint32_t xtalk_rate_mcps = 0;

    ESP_LOGI(TAG, "[%s] 아크릴 반사광(Xtalk) 보정 연산 시작... (10cm 앞에 타겟 유지 필요)", sensor_name);
    status = VL53L0X_PerformXTalkCalibration(pDevice, target_distance_mm, &xtalk_rate_mcps);
    
    if (status == VL53L0X_ERROR_NONE) {
        ESP_LOGI(TAG, "[%s] 아크릴 보정 완료! 노이즈 세기: %d Mcps", sensor_name, (int)xtalk_rate_mcps);
        VL53L0X_SetXTalkCompensationEnable(pDevice, 1); // 보정 엔진 활성화
    } else {
        ESP_LOGE(TAG, "[%s] 아크릴 보정 실패 (%d). 보정 없이 측정을 진행합니다.", sensor_name, status);
        VL53L0X_SetXTalkCompensationEnable(pDevice, 0);
    }

    return true;
}

// 💡 센서가 정상적으로 응답하는지 체크하는 디텍트 함수
bool VL53L0X_Detect(void)
{
    bool condition_tof0 = false;
    bool condition_tof1 = false;

    // TOF0 조건 체크
    if (g_tof0_ok) {
        if (tof0_mm > 70 && tof0_mm < 200) {
            condition_tof0 = true;
        }
    }

    // TOF1 조건 체크
    if (g_tof1_ok) {
        if (tof1_mm > 70 && tof1_mm < 200) {
            condition_tof1 = true;
        }
    }

    // ⭐️ 둘 중에 하나라도 조건을 만족(OR 연산)하면 true 반환, 둘 다 아니면 false 반환
    if (condition_tof0 || condition_tof1) {
        return true;
    } else {
        return false;
    }
}

// 💡 주기적으로 호출하여 센서 데이터를 읽어오는 함수 (100ms 등 태스크 내부 딜레이 주기 필수)
void VL53L0X_Sensing(void)
{
    VL53L0X_RangingMeasurementData_t measure_data0;
    VL53L0X_RangingMeasurementData_t measure_data1;
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // --- TOF0 (I2C 포트 0) 측정 ---
    if (g_tof0_ok) {
        VL53L0X_Error err0 = VL53L0X_PerformSingleRangingMeasurement(&dev_tof0, &measure_data0);
        if (err0 == VL53L0X_ERROR_NONE && measure_data0.RangeStatus == 0) {
            tof0_mm = measure_data0.RangeMilliMeter; // 아크릴 노이즈가 제거된 깨끗한 실제 거리
            ts_tof0_ms = now_ms;
            g_tof0_last_ok_ms = now_ms;
        } else {
            // 정밀 측정 실패 시 기본 범위 오류 가드 (필요시 에러 로그 활성화)
            // ESP_LOGW(TAG, "TOF0 Range Status Warning: %d", measure_data0.RangeStatus);
        }
    }

    // --- TOF1 (I2C_PORT 1) 측정 ---
    if (g_tof1_ok) {
        VL53L0X_Error err1 = VL53L0X_PerformSingleRangingMeasurement(&dev_tof1, &measure_data1);
        if (err1 == VL53L0X_ERROR_NONE && measure_data1.RangeStatus == 0) {
            tof1_mm = measure_data1.RangeMilliMeter; // 아크릴 노이즈가 제거된 깨끗한 실제 거리
            ts_tof1_ms = now_ms;
            g_tof1_last_ok_ms = now_ms;
        } else {
            // ESP_LOGW(TAG, "TOF1 Range Status Warning: %d", measure_data1.RangeStatus);
        }
    }
    // ⭐️ [0.5초마다 터미널에 보정된 센서값 출력] ⭐️

        
    ESP_LOGI(TAG, "========================================");
    if (g_tof0_ok) ESP_LOGI(TAG, "  [TOF0] Distance: %4d mm", tof0_mm);
    else           ESP_LOGW(TAG, "  [TOF0] DISCONNECTED");
    
    if (g_tof1_ok) ESP_LOGI(TAG, "  [TOF1] Distance: %4d mm", tof1_mm);
    else           ESP_LOGW(TAG, "  [TOF1] DISCONNECTED");
    ESP_LOGI(TAG, "========================================");

}

bool TOF_VL53L0X_init(void)
{
    // -------------------------------------------------------------
    // 1. I2C 포트 1 초기화 (TOF1용)
    // -------------------------------------------------------------
    i2c_config_t i2c_bus1_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOF1_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = PIN_TOF1_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = 400000,
    };
    
    if (i2c_param_config(I2C_NUM_1, &i2c_bus1_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config I2C Port 1");
    }
    if (i2c_driver_install(I2C_NUM_1, i2c_bus1_cfg.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C Port 1");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    // -------------------------------------------------------------
    // 2. I2C 포트 0 초기화 (TOF0용)
    // -------------------------------------------------------------
    i2c_config_t i2c_bus0_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOF0_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = PIN_TOF0_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = 400000, // 400kHz
    };
    
    if (i2c_param_config(I2C_NUM_0, &i2c_bus0_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config I2C Port 0");
    }
    if (i2c_driver_install(I2C_NUM_0, i2c_bus0_cfg.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C Port 0");
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // -------------------------------------------------------------
    // 3. ⭐️ ST 공식 C API를 이용한 하드웨어 엔진 캘리브레이션 구동 ⭐️
    // -------------------------------------------------------------
    ESP_LOGI(TAG, "Starting VL53L0X Pure C Initialization...");

    g_tof0_ok = init_single_vl53l0x(&dev_tof0, I2C_NUM_0, "TOF0_PORT0");
    g_tof1_ok = init_single_vl53l0x(&dev_tof1, I2C_NUM_1, "TOF1_PORT1");

    if (g_tof0_ok && g_tof1_ok) {
        ESP_LOGI(TAG, "🎉 양쪽 TOF 센서 모두 아크릴 보정 및 초기화 완벽 성공!");
    } else {
        ESP_LOGE(TAG, "⚠️ 일부 센서 초기화 실패 (TOF0: %s, TOF1: %s)", 
                 g_tof0_ok ? "OK" : "FAIL", g_tof1_ok ? "OK" : "FAIL");
    }

    return (g_tof0_ok && g_tof1_ok);
}