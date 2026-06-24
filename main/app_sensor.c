#include "app_sensor.h"
#include "gpio_util.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_util.h"
// 구버전 ESP-IDF용 I2C 헤더 경로
#include "driver/i2c.h"
#include "VL53L0X.h"
#define SENSOR_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)
#define TASK_DELAY_MS(x) (x/portTICK_PERIOD_MS)
static const char *TAG = __FILE__;
#ifndef PACKED
#define PACKED __attribute__((packed))
#endif
static void push_weight_sample(float w);
// cal
typedef struct  {
  float hx1_scale;                 // counts per gram
  int32_t hx1_offset;              // tare offset
  float hx2_scale;                 // reserved (2nd loadcell)
  int32_t hx2_offset;
  uint8_t hx_filter_win;           // moving average window

  // ToF legacy placeholder (keep for compatibility)
  int16_t tof_offset_mm;
  uint16_t tof_valid_min;

  // PHOTO/IR
  uint16_t photo_thr;              // (reserved) photo analog threshold
  uint16_t ir_diff_thr;            // IR diff threshold

  // VCNL3030X01 (ToF 대체)
  uint8_t  vcnl_i2c_addr;          // default 0x60
  uint16_t vcnl_ps_near_th;        // near threshold
  uint16_t vcnl_ps_sat_th;         // saturation threshold
  uint16_t vcnl_ps_conf12;         // PS_CONF12 (default 0x0A10)
  uint16_t vcnl_ps_conf3ms;        // PS_CONF3MS (default 0x0000)

  // health/stability
  uint16_t sensor_timeout_ms;          // stale timeout
  uint16_t loadcell_unstable_window_ms;// time window for stability check
  uint16_t loadcell_unstable_span_mg;  // unstable if span >= this

  uint8_t reserved[8];
}cal_t;

typedef struct  {

  cal_t  cal;

}settings_t;
typedef struct  { uint32_t t; float w; }WSample;
settings_t g_settings;
static WSample g_wbuf[32];
static uint8_t g_wbuf_n = 0;
static uint8_t g_wbuf_i = 0;
#include "esp_timer.h"
// ESP-IDF 전역 I2C 핸들 정의 (app_sensor.c 상단 혹은 헤더에 선언 필요)
// 1. 구버전은 별도의 버스 핸들러 객체가 없으므로 핸들 변수를 모두 제거하거나 포트 번호(int)로 대체합니다.
static i2c_port_t g_i2c_port0 = I2C_NUM_0;
static i2c_port_t g_i2c_port1 = I2C_NUM_1;
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
static unsigned long millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}
// -------------------------
// HX711 bitbang
// -------------------------
static bool hx711_read_raw(int32_t* out_raw) {
  // Wait for ready: DOUT goes LOW
  uint32_t start = millis();
  uint32_t loop_count = 0;
  while (gpio_get_level(PIN_HX711_DOUT) == 1) {
    if (millis() - start > 50) return false;
        loop_count++;
    if (loop_count > 10) { 
        vTaskDelay(1); // 10번 이상 안 나오면 통신 스택(코어0)을 위해 양보
    } else {
        esp_rom_delay_us(10); // 처음에는 10us 단위로 촘촘하게 센서 확인
    }
  }

  uint32_t data = 0;
  // 24 bits
  for (int i = 0; i < 24; ++i) {
    gpio_set_level(PIN_HX711_SCK, 1);
    esp_rom_delay_us(1);
    data = (data << 1) | (gpio_get_level(PIN_HX711_DOUT) ? 1 : 0);
    gpio_set_level(PIN_HX711_SCK, 0);
    esp_rom_delay_us(1);
  }

  // Gain channel A 128: 1 extra pulse
  gpio_set_level(PIN_HX711_SCK, 1);
  esp_rom_delay_us(1);
  gpio_set_level(PIN_HX711_SCK, 0);
  esp_rom_delay_us(1);

  // sign extend 24-bit
  if (data & 0x800000) data |= 0xFF000000;
  *out_raw = (int32_t)data;
  //out_raw = -out_raw; // Invert to make positive weight positive value
  return true;
}
static bool calc_weight_g(const settings_t* s, int32_t raw, float* out_g) {
  if (s->cal.hx1_scale == 0.0f) return false;
  *out_g = ((float)raw - (float)s->cal.hx1_offset) / s->cal.hx1_scale;
  return true;
}

static void push_weight_sample(float w) {
  const size_t max_samples = sizeof(g_wbuf) / sizeof(g_wbuf[0]);
  g_wbuf[g_wbuf_i] = (WSample){ millis(), w };
  g_wbuf_i = (uint8_t)((g_wbuf_i + 1) % max_samples);
  if (g_wbuf_n < max_samples) g_wbuf_n++;
}
void HX711_cal_init(void)
{
      float w = 0.0f;
      bool ok_raw;
          const settings_t* cfg = &g_settings;
  bool ok_w;
    int32_t raw = 0;
  int32_t raw_sum = 0;
  uint8_t count = 0;
  for(int k = 0; k < 10; ++k) {
    ok_raw = hx711_read_raw(&raw);
    ok_w = ok_raw && calc_weight_g(cfg, raw, &w);
    APP_String_printf("[SENSOR][Offset Calc-%d] Weight: %.2f g (raw: %d)\r\n", k, ok_w ? w : 0.0f, raw);
    if (ok_w) {
      raw_sum += raw;
      count++;
    }

            vTaskDelay(500 / portTICK_PERIOD_MS);    
  }
  g_settings.cal.hx1_offset = count > 0 ? (raw_sum / count) : 0;
  APP_String_printf("[SENSOR] Tare offset set to %d\r\n", g_settings.cal.hx1_offset);

}
void Sensor_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting sensor task");
    const settings_t* cfg = &g_settings;
    g_settings.cal.hx1_scale = 1000.0f;
      float w = 0.0f;
  bool ok_raw;
  bool ok_w;
    int32_t raw = 0;
    static uint32_t ts_ms;
    HX711_cal_init();
    while (1) {
        #if 1
        // HX711 weight
              ok_raw = hx711_read_raw(&raw);
            ok_w = ok_raw && calc_weight_g(cfg, raw, &w);
            //s.hx_raw = raw;
            //s.weight_valid = ok_w;
            //s.weight_g = ok_w ? w : 0.0f;
            //s.ts_weight_ms = s.ts_ms;
            if (ok_w) 
            {
                push_weight_sample(w);
                APP_String_printf("[SENSOR] Weight: %.2f g (raw: %d)\r\n", w, raw);
            }
        #endif
        vTaskDelay(1000 / portTICK_PERIOD_MS);
// 로컬 스냅샷 구조체 초기화 및 현재 타임스탬프 기록

        ts_ms = (uint32_t)(esp_timer_get_time() / 1000);

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
                g_tof0_last_ok_ms = ts_ms; // 마지막 정상 수신 시간 갱신
            }
        } else {
            tof0_mm = 0; // 초기화 자체가 실패했던 경우
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // -------------------------------------------------------------
        // 2. TOF1 센서 값 읽기 예제
        // -------------------------------------------------------------
        if (g_tof1_ok) {
            uint16_t dist1 = VL53L0X_readRangeContinuousMillimeters(&g_tof1);
            
            if (VL53L0X_timeoutOccurred(&g_tof1)) {
                ESP_LOGE(TAG, "TOF1 Read Timeout! (Value: %d)", dist1);
                tof1_mm = 0;
            } else {
                tof1_mm = dist1;
                g_tof1_last_ok_ms = ts_ms;
            }      
        } else {
            tof1_mm = 0;
        }

        // -------------------------------------------------------------
        // 3. 수신 결과 디버그 출력 및 전역 데이터 전송
        // -------------------------------------------------------------
       APP_String_printf("Measured Distance -> TOF0: %d mm, TOF1: %d mm\r\n", tof0_mm, tof1_mm);

        vTaskDelay(1000 / portTICK_PERIOD_MS);

    }
    
}

void HX711_init(void)
{
// 1. PIN_HX711_DOUT 설정 (인풋 모드)
    gpio_config_t io_conf_dout = {
        .pin_bit_mask = (1ULL << PIN_HX711_DOUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,       // HX711은 보통 외부에 풀업이 있거나 필요 없음
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE          // ⚠️ HX711 데이터 핀은 인터럽트를 쓰지 않습니다.
    };
    gpio_config(&io_conf_dout);

    // 2. PIN_HX711_SCK 설정 (아웃풋 모드)
    gpio_config_t io_conf_sck = {
        .pin_bit_mask = (1ULL << PIN_HX711_SCK),
        .mode = GPIO_MODE_OUTPUT,               // ⚠️ 아웃풋 모드로 변경
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_sck);

    // 3. digitalWrite(PIN_HX711_SCK, LOW); 대체
    // 초기 시작 시 클럭 핀을 0(LOW) 상태로 안전하게 내려둡니다.
    gpio_set_level(PIN_HX711_SCK, 0);

}

void TOF_VL53L0X_init(void)
{
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

    // -------------------------------------------------------------
    // 3. I2C 포트 1 초기화 (TOF1용)
    // -------------------------------------------------------------
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
    g_tof0_ok = false;
    g_tof1_ok = false;
    
    // 아래 두 줄은 VL53L0X_setBus 내부에서 처리되므로 지우셔도 무방합니다.
     g_tof0.i2c_port = I2C_NUM_0;
    g_tof1.i2c_port = I2C_NUM_1;
    // 버스 0번에 TOF0 지정 및 초기화 (주소 0x29)
    VL53L0X_setBus(&g_tof0, g_i2c_port0, ADDRESS_DEFAULT);
    if (VL53L0X_init(&g_tof0, true)) {
        g_tof0_ok = true;
        VL53L0X_startContinuous(&g_tof0, 0); // 👈 [추가] TOF0 측정 하드웨어 시동! (주기 0 = 무한 연속 측정)
    }

    // 버스 1번에 TOF1 지정 및 초기화 (주소 0x29)
    VL53L0X_setBus(&g_tof1, g_i2c_port1, ADDRESS_DEFAULT);
    if (VL53L0X_init(&g_tof1, true)) {
        g_tof1_ok = true;
        VL53L0X_startContinuous(&g_tof1, 0); // 👈 [추가] TOF1 측정 하드웨어 시동!
    }
}
void sensor_init(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    HX711_init();
    TOF_VL53L0X_init();

    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreatePinnedToCore(
            Sensor_task,                  // 태스크 함수
            "sensor_task",                // 태스크 이름
            SENSOR_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 4,      // 우선순위
            &xHandle,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        ESP_LOGE(TAG, "Error creating Sensor_task on Core 1");
    }


}