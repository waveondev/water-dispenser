#include "app_led.h"
#include "led_strip.h"

#define BLINK_GPIO   14   // 네오픽셀 데이터 선이 연결된 GPIO 핀 번호
#define LED_NUMBERS  3   // 연결된 네오픽셀 LED 총 개수 (예: 3개)

static led_strip_handle_t led_strip;

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
    }
    else
    {
        led_strip_set_pixel_rgbw(led_strip, 0, R, G, B, 0);
        led_strip_set_pixel_rgbw(led_strip, 1, R, G, B, 0);
    }

    // 실제 SK6812 칩들로 32비트 정밀 신호 전송
    led_strip_refresh(led_strip); 
}

