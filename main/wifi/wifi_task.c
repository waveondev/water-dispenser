#include "string.h"
#include "wifi_task.h"
#include "esp_netif.h"

#include "protocol_examples_common.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "protocol_examples_common.h"
#include "example_common_private.h"
#include "mqtt_main.h"

static const char* TAG = "wifi_task";

#define USER_CONFIG_EXAMPLE_WIFI_SSID "iptime_lab0"
#define USER_CONFIG_EXAMPLE_WIFI_PASSWORD "gunpo!0929"
//#define USER_CONFIG_EXAMPLE_HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#define USER_CONFIG_EXAMPLE_HOST_PORT CONFIG_EXAMPLE_PORT

#include <esp_https_ota.h>

#include <esp_ota_ops.h>
/*
void wifi_init_sta_static_ip(char* WIFI_SSID, char* WIFI_PASS)
{
    // 1. 기본 네트워크 인터페이스 초기화
 //   esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    // 2. DHCP 클라이언트 중지
    esp_netif_dhcpc_stop(netif);

    // 3. 고정 IP 설정
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ipaddr_addr("192.168.0.61");
    ip_info.gw.addr = ipaddr_addr("192.168.0.1");
    ip_info.netmask.addr = ipaddr_addr("255.255.255.0");

    esp_netif_set_ip_info(netif, &ip_info);

    // 4. Wi-Fi 초기화 및 시작
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wifi_config ;
    memcpy(wifi_config.sta.ssid,WIFI_SSID,sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password,WIFI_PASS,sizeof(wifi_config.sta.password));
//
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    ESP_LOGI(TAG, "Wi-Fi STA static IP setup done");
}*/

#define WIFI_MAX_VALUE 30
static uint16_t ap_count = 0;
static wifi_ap_record_t ap_list[WIFI_MAX_VALUE];


uint16_t remove_duplicate_best_rssi(wifi_ap_record_t *list, uint16_t count)
{
    uint16_t new_count = 0;

    for(int i=0; i<count; i++)
    {
        int found = -1;

        for(int j=0; j<new_count; j++)
        {
            if(strcmp((char*)list[i].ssid,
                      (char*)list[j].ssid)==0)
            {
                found = j;
                break;
            }
        }


        if(found >= 0)
        {
            // 더 강한 신호로 교체
            if(list[i].rssi > list[found].rssi)
            {
                memcpy(&list[found],
                       &list[i],
                       sizeof(wifi_ap_record_t));
            }
        }
        else
        {
            memcpy(&list[new_count],
                   &list[i],
                   sizeof(wifi_ap_record_t));

            new_count++;
        }
    }

    return new_count;
}

uint16_t wifi_scan_start(void)
{
// 3. Wi-Fi 스캔 설정 및 시작
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false, // 숨겨진 SSID도 스캔
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,
        .scan_time.active.max = 0
    };

    memset(ap_list,0,sizeof(ap_list));
    ap_count = 0;

    ESP_LOGI(TAG, "Wi-Fi 스캔 시작...");
    // true로 설정하면 스캔이 완료될 때까지 블로킹(대기)합니다.
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true)); 

    // 4. 스캔된 AP 개수 확인 및 리스트 가져오기
    uint16_t number = WIFI_MAX_VALUE; // 최대 가져올 AP 개수




    // ⭐ 순서 변경: 실제 발견된 총 개수를 먼저 확인합니다.
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));


    // 발견된 게 있다면 버퍼 크기(20) 내에서 레코드를 가져옵니다.
    if (ap_count > 0) {
        if (ap_count < number) {
            number = ap_count; // 실제 개수만큼만 가져오도록 제한
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_list));
        number = remove_duplicate_best_rssi(ap_list, number);
        for (int i = 0; i < number; i++) {
            ESP_LOGI(TAG, "SSID: %s | RSSI: %d | 채널: %d", 
                    ap_list[i].ssid, ap_list[i].rssi, ap_list[i].primary);
        }
        ESP_LOGI(TAG, "총 %d 개의 AP를 찾았습니다.", number);
    }
    return number;
}
void Wifi_Connect(uint8_t* target_ssid, uint8_t* target_password)
{
    uint8_t ssid[32];
    uint8_t passward[64];
    memset(ssid,0,sizeof(ssid));
    memset(passward,0,sizeof(passward));    
    memcpy(ssid,target_ssid,strlen((char*)target_ssid));
    memcpy(passward,target_password,strlen((char*)target_password));

    ESP_LOGD(TAG,"empty ssid block default ssid set= %s", ssid);
    ESP_LOGD(TAG,"empty pass block default pass set= %s", passward);

    example_connect(ssid, passward);
    //mqtt_main();
}

void wifi_init(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;
    ESP_LOGI(TAG,"wifi task_start");
    
    const esp_partition_t* run_parti;
    run_parti = esp_ota_get_running_partition();
    ESP_LOGD("partition","type = %d", run_parti->type);
    ESP_LOGD("partition","subtype = %d", run_parti->subtype);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    example_wifi_start();
}