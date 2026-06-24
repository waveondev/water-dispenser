#include "string.h"
#include "esp_netif.h"

#include "protocol_examples_common.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "spiffs_util.h"
#include "protocol_examples_common.h"
#include "example_common_private.h"
#include "wifi_util.h"
#include "mqtt_main.h"
#define WIFI_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)
#define WIFI_TASK_DELAY_MS(x) (x/portTICK_PERIOD_MS)

static const char* TAG = "wifi_task";
extern void tcp_client(char* addr, unsigned short port);
extern void tcp_server(void);
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


static void wifi_main(void* arg)
{
    char ssid[32];
    char passward[64];
    memset(ssid,0,sizeof(ssid));
    memset(passward,0,sizeof(passward));
    const wifi_info_t* wifi_info = wifi_Info_get();


#if 0
    if(spiffs_str_read(WIFI_SSID_PATH, ssid, sizeof(ssid)) != 0)
    {
        ESP_LOGE(TAG,"empty ssid block default ssid set= %s", USER_CONFIG_EXAMPLE_WIFI_SSID);
        memcpy(ssid,USER_CONFIG_EXAMPLE_WIFI_SSID,sizeof(USER_CONFIG_EXAMPLE_WIFI_SSID));
    }
    if(spiffs_str_read(WIFI_PASSWARD_PATH, passward, sizeof(passward)) != 0)
    {
        ESP_LOGE(TAG,"empty pass block default pass set= %s", USER_CONFIG_EXAMPLE_WIFI_PASSWORD);
        memcpy(passward,USER_CONFIG_EXAMPLE_WIFI_PASSWORD,sizeof(USER_CONFIG_EXAMPLE_WIFI_PASSWORD));
    }
    if(spiffs_str_read(WIFI_IPV4_SERVER_ADDR_PATH, addr, sizeof(addr)) != 0)
    {
        ESP_LOGE(TAG,"empty addr block default addr set= %s", USER_CONFIG_EXAMPLE_HOST_IP_ADDR);
        memcpy(addr,USER_CONFIG_EXAMPLE_HOST_IP_ADDR,sizeof(USER_CONFIG_EXAMPLE_HOST_IP_ADDR));
    }
    port = wifi_info_get_hostport();
#else
        ESP_LOGD(TAG,"empty ssid block default ssid set= %s", wifi_info->ssid);
        memcpy(ssid,wifi_info->ssid,sizeof(ssid));
        ESP_LOGD(TAG,"empty pass block default pass set= %s", wifi_info->password);
       memcpy(passward,wifi_info->password,sizeof(passward));
  //      ESP_LOGE(TAG,"empty addr block default addr set= %s", USER_CONFIG_EXAMPLE_HOST_IP_ADDR);
#endif
    


    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    //wifi_scan();
    //wifi_init_sta_static_ip(ssid,passward);
    ESP_ERROR_CHECK(example_connect());
    





   // printf("tcp_start");
   mqtt_main();
    //tcp_server();
   // tcp_client(wifi_info->host_ip,wifi_info->host_port);
   while(1)
   {
     vTaskDelay(1000 / portTICK_PERIOD_MS);
   }
}


void wifi_task_init(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;
    ESP_LOGI(TAG,"wifi task_start");
    
    const esp_partition_t* run_parti;
    run_parti = esp_ota_get_running_partition();
    ESP_LOGD("partition","type = %d", run_parti->type);
    ESP_LOGD("partition","subtype = %d", run_parti->subtype);

    ESP_LOGI(TAG,"wifi_main task_start");
    if (xTaskCreatePinnedToCore(
            wifi_main,                  // 태스크 함수
            "Wifi_Main",                // 태스크 이름
            WIFI_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 3,      // 우선순위
            &xHandle,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
                  ESP_LOGE(TAG, "Error creating Wifi_Main on Core 1");
    }

}