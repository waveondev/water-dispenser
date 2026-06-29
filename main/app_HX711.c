#include "app_HX711.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gpio_util.h"
#include "FreeRTOS_CLI.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "app_config_flash.h"
static const char *TAG = __FILE__;
#if 0 
[SENSOR] Weight: 251.93 g (raw: 251934) 없을때
[SENSOR] Weight: 314.22 g (raw: 314222) 물통만 
[SENSOR] Weight: 346.72 g (raw: 346721) 접시+물통
[SENSOR] Weight: 363.90 g (raw: 366308) 전체

#endif
typedef struct  { uint32_t t; float w; }WSample;

static WSample g_wbuf[32];
static uint8_t g_wbuf_n = 0;
static uint8_t g_wbuf_i = 0;
static bool hx711_cal_enable = false;

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
static bool calc_weight_g(const app_config_t* s, int32_t raw, float* out_g) {
  if (s->hx1_scale == 0.0f) return false;
  *out_g = ((float)raw - (float)s->hx1_offset) / s->hx1_scale;
  return true;
}

static void push_weight_sample(float w) {
  const size_t max_samples = sizeof(g_wbuf) / sizeof(g_wbuf[0]);
  g_wbuf[g_wbuf_i] = (WSample){ millis(), w };
  g_wbuf_i = (uint8_t)((g_wbuf_i + 1) % max_samples);
  if (g_wbuf_n < max_samples) g_wbuf_n++;
}

static void HX711_cal_process(void)
{
    float w = 0.0f;
    bool ok_raw;
    bool ok_w;

    int32_t raw = 0;
    int32_t raw_sum = 0;
    uint8_t count = 0;
    app_config_t* app_config = get_app_config();
    for(int k = 0; k < 10; ++k) {
        ok_raw = hx711_read_raw(&raw);
        ok_w = ok_raw && calc_weight_g(app_config, raw, &w);
        ESP_LOGI(TAG,"[Offset Calc-%d] Weight: %.2f g (raw: %d)\r\n", k, ok_w ? w : 0.0f, raw);
        if (ok_w) {
            raw_sum += raw;
            count++;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);    
    }
    app_config->hx1_offset = count > 0 ? (raw_sum / count) : 0;
     ESP_LOGI(TAG, "Tare offset set to %d\r\n", app_config->hx1_offset);
}
void HX711_cal_init(void)
{
    hx711_cal_enable = true;
}

void HX711_Sensing(void)
{
    app_config_t* app_config = get_app_config();
    if(hx711_cal_enable == true)
    {
      hx711_cal_enable = false;
      HX711_cal_process();
    }
    else{
        float w = 0.0f;
        bool ok_raw;
        bool ok_w;
        int32_t raw = 0;
        int32_t raw_sum = 0;
        uint8_t count = 0;
        // HX711 weight
        ok_raw = hx711_read_raw(&raw);
        ok_w = ok_raw && calc_weight_g(app_config, raw, &w);

        if (ok_w) 
        {
            push_weight_sample(w);
            ESP_LOGI(TAG," Weight: %.2f g (raw: %d)\r\n", w, raw);
        }
    }

}


bool HX711_init(void)
{
    float w = 0.0f;
    bool ok_raw;
    bool ok_w;
// 1. PIN_HX711_DOUT 설정 (인풋 모드)
    gpio_config_t io_conf_dout = {
        .pin_bit_mask = (1ULL << PIN_HX711_DOUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,       // HX711은 보통 외부에 풀업이 있거나 필요 없음
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE          // ⚠️ HX711 데이터 핀은 인터럽트를 쓰지 않습니다.
    };
    gpio_config(&io_conf_dout);

    // 2. PIN_HX711_SCK 설정 (아웃풋 모드)
    gpio_config_t io_conf_sck = {
        .pin_bit_mask = (1ULL << PIN_HX711_SCK),
        .mode = GPIO_MODE_OUTPUT,               // ⚠️ 아웃풋 모드로 변경
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_sck);

    // 3. digitalWrite(PIN_HX711_SCK, LOW); 대체
    // 초기 시작 시 클럭 핀을 0(LOW) 상태로 안전하게 내려둡니다.
    gpio_set_level(PIN_HX711_SCK, 0);
    int32_t raw = 0;
    vTaskDelay(pdMS_TO_TICKS(500));
    return hx711_read_raw(&raw);
}

