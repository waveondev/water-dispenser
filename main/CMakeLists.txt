idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    set(requires esp_event esp_stubs protocol_examples_common nvs_flash)
endif()


# 컴포넌트 등록
idf_component_register(SRCS "app_button.c" 
                            "wifi/http_ota.c" 
                            "wifi/wifi_task.c"     
                            "gpio_util.c"
                            "cli/config_cli.c" 
                            "cli/FreeRTOS_CLI.c" 
                            "cli/set_cli.c" 
                            "main.c" 
                            "app_moter.c"
                            "mqtt/mqtt_parse.c" 
                            "mqtt/mqtt_main.c" 
                            "jbx-list.c" 
                            "ble/ble_parse.c"   
                            "ble/ble_task.c"    
                            "app_button.c"
                            "app_led.c"
                            "app_sensor.c"
                            "VL53L0X.c"
                            "app_adc.c"
                            "app_HX711.c"
                            "app_TOF.c"
                            "app_nvs.c"
                            "app_config_flash.c"
                            INCLUDE_DIRS "." "ble" "wifi" "cli" "mqtt"
                       # 블루투스, OTA, NVS flash에 더해 'driver'(GPIO용)까지 싹 다 명시합니다.

                       PRIV_REQUIRES bt esp_https_ota nvs_flash app_update bootloader_support driver esp_driver_gpio vfs esp_driver_ledc led_strip esp_adc)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "-DconfigGENERATE_RUN_TIME_STATS=1"
    "-DportGET_RUN_TIME_COUNTER_VALUE()=((unsigned_long)esp_timer_get_time())"
)