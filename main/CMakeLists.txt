idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    set(requires esp_event esp_stubs protocol_examples_common nvs_flash)
endif()

# IPv4/v6 설정에 따른 소스 파일 선택 (중복 제거)
if("${CONFIG_EXAMPLE_IPV4}" STREQUAL y)
    set(tcp_client_ip "wifi/tcp_client_v4.c")
else()
    set(tcp_client_ip "wifi/tcp_client_v6.c")
endif()

# 컴포넌트 등록
idf_component_register(SRCS "app_button.c" "wifi/wifi_util.c" 
                            "wifi/simple_ota_example.c" 
                            "gpio_util.c"
                            "cli/config_cli.c" 
                            "spiffs_util.c" 
                            "ippe_util.c" 
                            "cli/FreeRTOS_CLI.c" 
                            "cli/set_cli.c" 
                            "wifi/wifi_task.c" 
                            "wifi/tcp_client_main.c" 
                            "app_moter.c"
                            "wifi/tcp_server.c" 
                            "mqtt/mqtt_parse.c" 
                            "mqtt/mqtt_main.c" 
                            "jbx-list.c" 
                            "ble/ble_util.c"    # 👈 줄을 나눠서 명확하게 배치
                            "ble/ble_parse.c"   # 👈 줄을 나눠서 명확하게 배치
                            "ble/ble_task.c"    # 👈 줄을 나눠서 명확하게 배치
                            "app_button.c"
                            "app_led.c"
                            "app_sensor.c"
                            "VL53L0X.c"
                            "app_adc.c"
                            "${tcp_client_ip}"
                            INCLUDE_DIRS "." "ble" "wifi" "cli" "mqtt"
                       # 블루투스, OTA, NVS flash에 더해 'driver'(GPIO용)까지 싹 다 명시합니다.

                       PRIV_REQUIRES bt esp_https_ota nvs_flash app_update bootloader_support driver esp_driver_gpio vfs spiffs esp_driver_ledc led_strip esp_adc)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "-DconfigGENERATE_RUN_TIME_STATS=1"
    "-DportGET_RUN_TIME_COUNTER_VALUE()=((unsigned_long)esp_timer_get_time())"
)