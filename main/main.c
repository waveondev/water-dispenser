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
#include "ble_tracker_id.h"

//[by.jeon] 하드디스크(SPIFFS) 설정 및 초기화 함수
static esp_vfs_spiffs_conf_t spiffs_conf = {
  .base_path = "/spiffs",
  .partition_label = "spiffs_storage",
  .max_files = 5,
  .format_if_mount_failed = true
};

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "soc/rtc_cntl_reg.h"
void enter_download_mode(void)
{

    // USB Serial/JTAG ROM 출력 영구 비활성화
    if (!esp_efuse_read_field_bit(
            ESP_EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT)) {

        esp_err_t ret = esp_efuse_write_field_bit(
            ESP_EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT
        );

        if (ret != ESP_OK) {
            ESP_LOGE("EFUSE", "Failed: %s",
                     esp_err_to_name(ret));
            return;
        }
    }

#if 0



        // USB Serial/JTAG ROM 출력 영구 비활성화
    if (!esp_efuse_read_field_bit(
            ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE)) {

        esp_err_t ret = esp_efuse_write_field_bit(
            ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE
        );

        if (ret != ESP_OK) {
            ESP_LOGE("EFUSE", "Failed: %s",
                     esp_err_to_name(ret));
            return;
        }
    }
#endif

    // 다운로드 모드 부트 플래그
    REG_WRITE(RTC_CNTL_OPTION1_REG,
              RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    // Task WDT 발생
    esp_restart();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

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
    bool qc_mode = false;
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
    #if JIGMODE
    void ble_jig_main(void);
    ble_jig_main();
    #else
    NVS_Flash_init();

    filesystem_init();

    qc_mode = button_task_init();
    init_motor_ledc(qc_mode);

    sensor_init();

    
  
    LED_task_init(qc_mode);
    console_task_init(qc_mode);
    ble_task_init(qc_mode);
    if(qc_mode == false)
    {
    #if 1
        opmode_task_init();
        Create_Tracker_Capture_Task();
        
        
        wifi_init();
        aws_iot_task_init();
    #endif
    }
    #endif



}
