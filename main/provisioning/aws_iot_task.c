#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h" // Wi-Fi 대기용
#include "esp_log.h"
#include "aws_iot_task.h"
#include "aws_iot_config.h"
#include "wifi_task.h"

// #include "core_mqtt.h"
// #include "fleet_provisioning_with_csr_demo.h" 

static const char *TAG = "aws_iot_task";

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

    ESP_LOGI("AWS_TASK", "인터넷 연결 확인됨! AWS Fleet Provisioning 프로세스를 시작합니다.");

    /*-----------------------------------------------------------------*/
    /* 여기에 기존에 성공했던 AWS 데모 코드의 본문 내용을 복사해 넣으세요.  */
    /* - 임시 인증서로 연결                                           */
    /* - CSR 생성 및 새 인증서 발급 요청                              */
    /* - NVS 플래시에 새 인증서 저장                                  */
    /* - 새 인증서로 본 서버(MQTT) 재접속                              */
    /*-----------------------------------------------------------------*/
    
    // 예시 구동 함수 호출
    // runFleetProvisioningWithCsrDemo();

    // 태스크가 끝나지 않고 계속 MQTT 메시지를 수신/송신하도록 하거나, 
    // 통신 전담 루프를 돌려야 합니다.
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL); // 만약 루프를 빠져나간다면 태스크 종료
}

void aws_iot_task_init(void)
{
    xTaskCreate(aws_iot_main_entry, "aws_iot_task", 24576, NULL, 5, NULL);
}