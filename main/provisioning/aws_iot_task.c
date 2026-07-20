#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h" // Wi-Fi 대기용
#include "esp_log.h"
#include "aws_iot_task.h"
#include "aws_iot_config.h"
#include "wifi_task.h"
#include "mqtt_operations.h"

#include "tx_mqtt.h"

// #include "core_mqtt.h"
// #include "fleet_provisioning_with_csr_demo.h" 

static const char *TAG = "aws_iot_task";
extern EventGroupHandle_t s_wifi_event_group;
static bool is_aws_started = false;
static QueueHandle_t mqtt_tx_queue = NULL;

extern int aws_iot_provisioning_main( int argc, char ** argv );

void mqtt_queue_send(messege_tx_mqtt_cmd_e cmd)
{
    if (xQueueSend(mqtt_tx_queue, &cmd, 0) != pdPASS) {
        ESP_LOGW("mqtt_tx", "Queue full! Dropping packet and freeing memory.");
    }
}


static void aws_iot_main_entry(void *pvParameters)
{
    ESP_LOGI(TAG, "AWS IoT 전담 태스크가 시작!");

    // -------------------------------------------------------------------------
    // Wi-Fi가 연결되어 IP를 받아올 때까지 이 태스크를 완전히 중지(Block)시킴.
    // -------------------------------------------------------------------------
    
    xEventGroupWaitBits(
        s_wifi_event_group,   // wifi_task.c가 관리하는 이벤트 그룹
        WIFI_CONNECTED_BIT,   // IP 할당 완료 비트
        pdFALSE,              // 비트를 자동으로 지우지 않음 (연결 유지 확인용)
        pdTRUE,               // 설정한 모든 비트가 켜질 때까지 대기
        portMAX_DELAY         // ⏳ 연결될 때까지 무한정 대기 (인터넷 안 되면 여기서 대기)
    );

    ESP_LOGI(TAG, "인터넷 연결 확인됨! AWS Fleet Provisioning 프로세스를 시작합니다.");

    aws_iot_provisioning_main(0, NULL);

    ESP_LOGI(TAG,"=== 2. MQTT 루프를 시작합니다 ===");

    messege_tx_mqtt_cmd_e cmd;

    for(;;) {
        if (xQueueReceive(mqtt_tx_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            Send_cJSON_Messege(cmd);
        }
        /* 백그라운드에서 지속적으로 수신 버퍼를 감시하며 콜백 함수를 유도합니다 */
        ProcessLoopWithTimeout(); 
    }

    vTaskDelete(NULL); // 만약 루프를 빠져나간다면 태스크 종료
}

void aws_iot_task_init(void)
{
    mqtt_tx_queue = xQueueCreate(10, sizeof(messege_tx_mqtt_cmd_e));



    if (is_aws_started == false) {

        if (xTaskCreatePinnedToCore(
            aws_iot_main_entry,                  // 태스크 함수
            "aws_iot_task",                // 태스크 이름
            24576,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 5,      // 우선순위
            NULL,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
            ESP_LOGE(TAG, "Error creating aws_iot_task on Core 1");
        }


        //xTaskCreate(aws_iot_main_entry, "aws_iot_task", 24576, NULL, 5, NULL);
        is_aws_started = true;
    }


}