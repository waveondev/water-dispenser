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
#include "esp_timer.h"  // 👈 이 줄을 추가해 주세요!
#include "ble_task.h"
#include "shadow_mqtt.h"
// #include "core_mqtt.h"
// #include "fleet_provisioning_with_csr_demo.h" 

static const char *TAG = "aws_iot_task";
extern EventGroupHandle_t s_wifi_event_group;
static bool is_aws_started = false;
static QueueHandle_t mqtt_tx_queue = NULL;
static QueueHandle_t tracker_mqtt_queue = NULL;


static esp_timer_handle_t Health_timer;
static esp_timer_handle_t Get_Jobs_timer;

#define AWS_IOT_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4)
extern int aws_iot_provisioning_main( int argc, char ** argv );
// 15분 = 15분 * 60초 * 1,000,000us

#define TIMER_1_MIN_IN_US    (60ULL * 1000000ULL)
#define TIMER_15_MIN_IN_US   (15ULL * TIMER_1_MIN_IN_US)





static void Get_Jobs_timer_callback(void* arg)
{
    #define TIMER_1_DAY_IN_US    (24ULL * 60ULL * 60ULL * 1000000ULL) // 86,400,000,000 µs    
    mqtt_queue_send(AWS_MESSEGE_JOBS_GET,NULL,0);
    
    if (esp_timer_is_active(Get_Jobs_timer)) {
        esp_timer_stop(Get_Jobs_timer);
    } 

    esp_timer_start_once(Get_Jobs_timer, TIMER_1_DAY_IN_US);

}
static void Health_timer_callback(void* arg)
{
    mqtt_queue_send(MESSEGE_HEALTH,NULL,0);
    
    if (esp_timer_is_active(Health_timer)) {
        esp_timer_stop(Health_timer);
    } 

    esp_timer_start_once(Health_timer, TIMER_15_MIN_IN_US);

}


bool mqtt_queue_send(messege_tx_mqtt_cmd_e cmd, void* data, uint32_t data_len)
{
    mqtt_packet_t mqtt_packet = {0};
    if(mqtt_tx_queue == NULL)
    {
        ESP_LOGW("mqtt_tx", "mqtt_tx_queue NULL.");
        return false;        
    }

    if(data != NULL)
    {
        mqtt_packet.data = calloc(1,data_len);
        if(mqtt_packet.data != NULL)
        {   
            memcpy(mqtt_packet.data,data,data_len);
        }   
    }

    if(Get_Accepted_Init_state() == false)
    {
        switch (cmd) {
            case MESSEGE_REGISTRATION:
            case MESSEGE_BOOT:
            case AWS_MESSEGE_SHADOW_GET: // 3번째 타겟 커맨드
                break; // 허용된 커맨드이므로 통과

            default:
                return false; // 그 외의 모든 커맨드는 차단/리턴
        }

    }


    mqtt_packet.cmd = cmd;

    mqtt_packet.data_len = data_len;
    ESP_LOGW("mqtt_tx", "mqtt_tx_queue send.");
    if (xQueueSend(mqtt_tx_queue, &mqtt_packet, 0) != pdPASS) {
        ESP_LOGW("mqtt_tx", "Queue full! Dropping packet and freeing memory.");
        return false;
    }
    return true;
}
uint8_t* tracker_mqtt_queue_send(messege_tx_mqtt_cmd_e cmd, uint8_t* mac, Motion_Packet_t* packet,uint32_t data_len,  uint8_t* data,
    uint32_t total_points,
    uint32_t send_points,
    uint32_t non_save_points
)
{
    tracker_mqtt_packet_t tracker_mqtt_packet = {0};

    if(tracker_mqtt_queue == NULL)
    {
        ESP_LOGW("mqtt_tx", "tracker_mqtt_queue NULL.");
        return NULL;        
    }


    memset(&tracker_mqtt_packet,0,sizeof(tracker_mqtt_packet_t));

    memcpy(tracker_mqtt_packet.mac,mac,sizeof(tracker_mqtt_packet.mac));
    memcpy(&tracker_mqtt_packet.packet,packet,sizeof(Motion_Packet_t));
    if(data != NULL)
    {
        tracker_mqtt_packet.data = calloc(1,data_len);
        if(tracker_mqtt_packet.data != NULL)
        {   
            memcpy(tracker_mqtt_packet.data,data,data_len);
        }   
        else
        {
            ESP_LOGW("mqtt_tx", "freeing memory.");
            return NULL;  
        }
    }
    tracker_mqtt_packet.total_points = total_points;
    tracker_mqtt_packet.send_points = send_points;
    tracker_mqtt_packet.non_save_points = non_save_points;
    tracker_mqtt_packet.cmd = cmd;
    tracker_mqtt_packet.data_len = data_len;
    ESP_LOGW("mqtt_tx", "tracker_mqtt_queue send.");
    
    if (xQueueSend(tracker_mqtt_queue, &tracker_mqtt_packet, 0) != pdPASS) {

        free(tracker_mqtt_packet.data);
        return NULL;   

    }
    return data;       
}

static esp_timer_handle_t aws_success_timer = NULL;
static esp_timer_handle_t aws_disconnect_timer = NULL;
bool aws_connect_flag = false;

#define MIN_TO_US(min) ((uint64_t)(min) * 60ULL * 1000000ULL)
#define SEC_TO_US(sec) ((uint64_t)(sec) * 1000000ULL)
#include "ble_task.h"
#include "app_led.h"
bool aws_connected_state(void)
{
    return aws_connect_flag;
}
static uint16_t retry_count = 0;
static void aws_success_callback(void* arg)
{
    if (esp_timer_is_active(aws_disconnect_timer)) {
        esp_timer_stop(aws_disconnect_timer);
    } 
    
    aws_connect_flag = true;
    ESP_LOGI(TAG, "aws_flag"); 
    retry_count = 0;
}
static void aws_discon_callback(void* arg)
{
    client_terminate();
    ESP_LOGI(TAG, "aws_discon"); 
}



static void flag_success_set(bool state)
{

    if (esp_timer_is_active(aws_success_timer)) {
        esp_timer_stop(aws_success_timer);
    } 
    if(state)
    {
        esp_timer_start_once(aws_success_timer, MIN_TO_US(1));
        ESP_LOGI(TAG, "aws_timer set"); 
    }
    else
        ESP_LOGI(TAG, "aws_timer reset"); 
}



static void ble_discon_timer_set(bool state, uint64_t discon_timeout)
{

    if (esp_timer_is_active(aws_disconnect_timer)) {
        esp_timer_stop(aws_disconnect_timer);
    } 
    if(state)
    {
        esp_timer_start_once(aws_disconnect_timer, SEC_TO_US(discon_timeout));
        ESP_LOGI(TAG, "ble_discon_timer set"); 
    }
    else
        ESP_LOGI(TAG, "ble_discon_timer reset"); 
}

#include "esp_heap_caps.h"
#include "rom/ets_sys.h" // ets_printf 사용을 위해 필요
#include "esp_heap_caps_init.h" // 일부 버전에서는 이 헤더에 훅 관련 기능이 포함됨
void esp_heap_trace_alloc_hook(void *ptr,
                               size_t size,
                               uint32_t caps)
{
    if(size == 92 || size == 128) {
        ets_printf("🎯 [ALLOC] Size: %d, Ptr: %p\n", size, ptr);
    }
}

void esp_heap_trace_free_hook(void *ptr)
{
    //ets_printf("[FREE] ptr=%p\n", ptr);
}
bool time_sync_get(void);

static void aws_iot_main_entry(void *pvParameters)
{

    ESP_LOGI(TAG, "AWS IoT 전담 태스크 시작");
    // Wi-Fi 연결 대기 (IP 할당 확인)
    xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );
    while(time_sync_get() == false)
    {
        vTaskDelay(1000);
    }
    static uint32_t discon_timeout = 0;
    // -------------------------------------------------------------
    // 1. [1회성 초기화] 큐 및 타이머 생성을 루프 밖에서 단 1번만 수행
    // -------------------------------------------------------------
    mqtt_tx_queue = xQueueCreate(10, sizeof(mqtt_packet_t));
    tracker_mqtt_queue = xQueueCreate(10, sizeof(tracker_mqtt_packet_t));



    const esp_timer_create_args_t aws_success_args = {
        .callback = &aws_success_callback,
        .name = "aws_flag_timer"
    };

    ESP_ERROR_CHECK(esp_timer_create(&aws_success_args, &aws_success_timer));


    const esp_timer_create_args_t aws_discon_args = {
        .callback = &aws_discon_callback,
        .name = "aws_discon_timer"
    };

    ESP_ERROR_CHECK(esp_timer_create(&aws_discon_args, &aws_disconnect_timer));

    const esp_timer_create_args_t Health_timer_args = {
        .callback = &Health_timer_callback,
        .name = "Health_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&Health_timer_args, &Health_timer));

    const esp_timer_create_args_t Get_Jobs_timer_args = {
        .callback = &Get_Jobs_timer_callback,
        .name = "Get_Jobs_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&Get_Jobs_timer_args, &Get_Jobs_timer));

    int provisioning_count = 0;
    mqtt_packet_t mqtt_packet;
    tracker_mqtt_packet_t tracker_mqtt_packet;

    // -------------------------------------------------------------
    // 2. [메인 재연결 루프]
    // -------------------------------------------------------------
    while(1)
    {
        // Wi-Fi 연결 대기 (IP 할당 확인)
        xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY
        );


        xQueueReset(mqtt_tx_queue);
        xQueueReset(tracker_mqtt_queue);

      
	// ... 문제가 되는 코드 (프로비저닝 로직) 실행 ...
                	//#include "esp_heap_trace.h"
    	//#define NUM_RECORDS 100
	//static heap_trace_record_t trace_record[NUM_RECORDS];

	// 루프 시작 전 (초기화 및 추적 시작)
	//heap_trace_init_standalone(trace_record, NUM_RECORDS);
	//heap_trace_start(HEAP_TRACE_LEAKS);

            ESP_LOGE("HEAP_CHECK", " 끝: %d", esp_get_free_heap_size());
        //aws_iot_provisioning_main(0, NULL);
        // AWS IoT 프로비저닝 및 MQTT 연결
        while(aws_iot_provisioning_main(0, NULL) != EXIT_SUCCESS)
        {
            ESP_LOGE("HEAP_CHECK", " 실패 직후 (정리 전) 힙: %d", esp_get_free_heap_size());
                
            // 🔄 1. 백그라운드 리소스 정리를 위해 3초 대기 (매우 중요)
            vTaskDelay(pdMS_TO_TICKS(3000)); 
            ESP_LOGE("HEAP_CHECK", " 실패 후 (정리 전) 힙: %d", esp_get_free_heap_size());
                
            // 🛑 2. 충분히 기다린 후 추적 정지 및 덤프 출력
            //heap_trace_stop();
            //heap_trace_dump();
            provisioning_count++;
            ESP_LOGI(TAG, "provisioning retry count = %d", provisioning_count);
            //if(provisioning_count > 10)
                //esp_restart();
            // 루프 시작 전 (초기화 및 추적 시작)
           // heap_trace_init_standalone(trace_record, NUM_RECORDS);
           // heap_trace_start(HEAP_TRACE_LEAKS);                
        }
        retry_count++;
        if(retry_count >= 20)
            esp_restart();
        // 2) MQTT 연결이 붙었으니 헬스 타이머 동작 시작!
        esp_timer_start_once(Health_timer, TIMER_1_MIN_IN_US);
        esp_timer_start_once(Get_Jobs_timer, TIMER_1_MIN_IN_US * 2);
        ESP_LOGI(TAG, "=== MQTT 송수신 메인 루프 진입 ===");

        flag_success_set(true);
        // MQTT 송수신 메인 루프
        for(;;) {
            if (xQueueReceive(mqtt_tx_queue, &mqtt_packet, pdMS_TO_TICKS(10)) == pdTRUE) {
                ESP_LOGW("mqtt_tx", "mqtt_rx = cmd  %d ",mqtt_packet.cmd);
                Send_cJSON_Messege(&mqtt_packet);
                if (mqtt_packet.data != NULL) {
                    free(mqtt_packet.data);
                    mqtt_packet.data = NULL; // Dangling Pointer 방지를 위해 NULL 처리 권장
                }                
            }
            if (xQueueReceive(tracker_mqtt_queue, &tracker_mqtt_packet, pdMS_TO_TICKS(10)) == pdTRUE) {
                Send_cJSON_Messege_for_tracker(&tracker_mqtt_packet);
            // ⭕ 훌륭함: 메시지 전송 처리 완료 후 동적 메모리 안전하게 해제
                if (tracker_mqtt_packet.data != NULL) {
                    free(tracker_mqtt_packet.data);
                    tracker_mqtt_packet.data = NULL; // Dangling Pointer 방지를 위해 NULL 처리 권장
                }
            }

            /* 수신 및 네트워크 연결 감시 */
            if (ProcessLoopWithTimeout(10) == false) {
                ESP_LOGE(TAG, "MQTT 연결 끊김 감지!");
                aws_connect_flag = false;
                flag_success_set(false);
                ble_discon_timer_set(true,discon_timeout);
                break; // for 루프 탈출
            }
        }
        
        // =========================================================
        // 🔴 [연결 끊김 시점]
        // =========================================================
        // 1) 네트워크가 끊겼으므로 헬스 타이머 즉시 정지!
        esp_timer_stop(Health_timer);
        esp_timer_stop(Get_Jobs_timer);
        // 2) 죽은 TLS/소켓 세션 정리
        DisconnectMqttSession();
        vTaskDelay(10000);
    }

    vTaskDelete(NULL);
}

void aws_iot_task_init(void)
{

    if (is_aws_started == false) {

        if (xTaskCreate(
            aws_iot_main_entry,                  // 태스크 함수
            "aws_iot_task",                // 태스크 이름
            AWS_IOT_TASK_STACK_SIZE,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 5,      // 우선순위
            NULL
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
            ESP_LOGE(TAG, "Error creating aws_iot_task on Core 1");
        }


        //xTaskCreate(aws_iot_main_entry, "aws_iot_task", 24576, NULL, 5, NULL);
        is_aws_started = true;
    }
}