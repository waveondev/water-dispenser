/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_event.h"
#include "ble/ble_task.h"
#include "wifi_task.h"
#include "FreeRTOS_CLI.h"
#include "gpio_util.h"
#include "lwip/apps/mqtt.h"
#include "app_moter.h"
#include "app_button.h"
#include "app_led.h"
#include "app_adc.h"
#include "opmode_task.h"

// AWS IoT Provisioning header 추가
#include "aws_iot_task.h"
#include "esp_spiffs.h"

extern void tcp_client(void);

#include "esp_vfs_dev.h"
#include "app_sensor.h"
#include "app_config_flash.h"
#include "motion_task.h"
#include "ble_tracker_id.h"

//[by.jeon] 하드디스크(SPIFFS) 설정 및 초기화 함수
static esp_vfs_spiffs_conf_t spiffs_conf = {
  .base_path = "/spiffs",
  .partition_label = "spiffs_storage",
  .max_files = 5,
  .format_if_mount_failed = true
};

static void filesystem_init(void)
{
    ESP_LOGI("SPIFFS", "Initializing SPIFFS");
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE("SPIFFS", "Failed to mount or format filesystem");
        return;
    }
    ESP_LOGI("SPIFFS", "SPIFFS mounted successfully");
}

void app_main(void)
{
#if 0
        // TX 변환 끄기: \n 그대로 전송
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM,ESP_LINE_ENDINGS_LF);
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM,ESP_LINE_ENDINGS_LF);
    // RX 쪽도 필요 시 조절 가능
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    ESP_ERROR_CHECK( ret );
#endif
    // NVS_Flash_init(); [by.jeon]


    // =========================================================================
    // 1️NVS (비휘발성 플래시 메모리) 초기화
    // AWS 프로비저닝 과정에서 발급받은 "고유 인증서"와 "개인키"를 
    // 기기의 플래시 메모리에 영구 저장하려면 NVS가 반드시 켜져 있어야 함.
    // =========================================================================
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS 파티션이 꼬였을 경우 포맷하고 다시 시도하는 방어 코드
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // [by.jeon] 가상 하드디스크(SPIFFS) 켜기! (인증서를 읽기 위해 필수)
    filesystem_init();

    console_task_init();
    init_motor_ledc();
    button_task_init();
    init_led_strip();
    LED_task_init();
    if(sensor_init() == false)
        led_bit_enable(SENSE_ERR_BIT);

    
    opmode_task_init();
    Create_Tracker_Capture_Task();
    ble_task_init();
    MotionTaskInit();

    wifi_init();

    //[by.jeon] wifi가 연결이 되는 시점에 aws provisioning를 해야 한다.
    //aws_iot_task_init();

    //charge_init();

    //mqtt_client_connect()
    //if(wifi_info_get_used())
    

}
