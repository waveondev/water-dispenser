#include "tx_mqtt.h"

#include "topic_list.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mqtt_operations.h"

static const char *TAG = __FILE__;
#define G_UUID
static cJSON* Get_cJSON_Header(messege_tx_mqtt_cmd_e cmd)
{
    /* --------------------------------------------------------- */
    /* 백엔드 규격에 맞춘 JSON 데이터 조립 (cJSON 적용) */
    /* --------------------------------------------------------- */
    cJSON *root = cJSON_CreateObject();
    if(root == NULL)
        return root;
    /* Root 레벨 필수 필드 추가 */
    cJSON_AddStringToObject(root, "id", "123e4567-e89b-12d3-a456-426614174000"); /* 실제로는 uuid 생성 함수 사용 */
    cJSON_AddStringToObject(root, "env", "alpha");
    cJSON_AddStringToObject(root, "device_type", "w100"); 
    switch(cmd)
    {
        case MESSEGE_REGISTRATION:
            cJSON_AddStringToObject(root, "event_type", "registration");
        break;
        case MESSEGE_BOOT:
            cJSON_AddStringToObject(root, "event_type", "boot");
        break;
        case MESSEGE_ACCESS:
            cJSON_AddStringToObject(root, "event_type", "access");
        break;
        case MESSEGE_DRINK:
            cJSON_AddStringToObject(root, "event_type", "drink");
        break;
        case MESSEGE_DIAGNOSTICS:
            cJSON_AddStringToObject(root, "event_type", "diagnostics");
        break;
        case MESSEGE_HEALTH:
            cJSON_AddStringToObject(root, "event_type", "health");
        break;
        default:
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(root, "firmware", "v1.0.0");
    cJSON_AddNumberToObject(root, "timestamp", 1747396800); /* 현재 시간 unix epoch seconds */

    return root;
}


static cJSON* Get_cJSON_Data(messege_tx_mqtt_cmd_e cmd)
{
    cJSON *data_obj = cJSON_CreateObject();
    uint8_t mac_byte[6];
    char dynamicMacStr[13]; // 12자리 MAC 문자열 + 널 종료 문자(\0)
    cJSON *subsystems;
    if(data_obj == NULL)
        return data_obj;

    switch(cmd)
    {
        case MESSEGE_REGISTRATION:
            // ESP32의 기본 Wi-Fi MAC 주소를 읽어옵니다.
            esp_read_mac(mac_byte, ESP_MAC_WIFI_STA);
            
            // 읽어온 MAC 주소를 콜론 없이 대문자 16진수 문자열로 포맷팅합니다 (예: "28372F9C283C")
            snprintf(dynamicMacStr, sizeof(dynamicMacStr), "%02X%02X%02X%02X%02X%02X",
                    mac_byte[0], mac_byte[1], mac_byte[2], mac_byte[3], mac_byte[4], mac_byte[5]);

            cJSON_AddStringToObject(data_obj, "mac", dynamicMacStr);
            cJSON_AddStringToObject(data_obj, "hw_rev", "r3.1");
            
            /* 🚨 필수 추가 필드: registration에는 home_id와 paired_at이 꼭 필요합니다 */
            cJSON_AddStringToObject(data_obj, "home_id", "home_test_01");
            cJSON_AddNumberToObject(data_obj, "paired_at", 1747396800);
        break;
        case MESSEGE_BOOT:
            cJSON_AddStringToObject(data_obj, "boot_reason", "power_on");
            cJSON_AddNumberToObject(data_obj, "uptime_sec", 0);
            
            cJSON_AddStringToObject(data_obj, "power_source", "ADAPTER");
            cJSON_AddNumberToObject(data_obj, "reset_reason", 0);
            cJSON_AddStringToObject(data_obj, "last_shutdown_reason", "unknown");            
        break;
        case MESSEGE_ACCESS:
            cJSON_AddStringToObject(data_obj, "access_id", "1234");
            cJSON_AddStringToObject(data_obj, "source", "sensor");
            cJSON_AddStringToObject(data_obj, "beacon_id", "UNKNOWN");
            cJSON_AddNumberToObject(data_obj, "rssi_dbm", 0);
// 🔧 수정: 문자열이 아닌 Float(숫자) 타입으로 전달해야 함
            cJSON_AddNumberToObject(data_obj, "start_weight", 100.2);
        break;
        case MESSEGE_DRINK:
            cJSON_AddStringToObject(data_obj, "drink_id", "550e8400-e29b-41d4-a716-446655440000");
            cJSON_AddStringToObject(data_obj, "session_type", "unknown");

            // 1. access_id_refs: 아이템을 추가하지 않고 빈 배열 그대로 루트에 전달
            cJSON *access_id_refs = cJSON_CreateArray();
            //cJSON_AddItemToArray(access_id_refs, cJSON_CreateString("acc_001"));
            //cJSON_AddItemToArray(access_id_refs, cJSON_CreateString("acc_002"));
            cJSON_AddItemToObject(data_obj, "access_id_refs", access_id_refs);

            // 2. participants: 아이템을 추가하지 않고 빈 배열 그대로 루트에 전달
            cJSON *participants = cJSON_CreateArray();
            #if 0 
            // 첫 번째 participant 객체 생성 및 입력
            cJSON *pet1 = cJSON_CreateObject();
            cJSON_AddStringToObject(pet1, "pet_id", "pet_001");
            cJSON_AddNumberToObject(pet1, "duration_sec", 120);
            cJSON_AddItemToArray(participants, pet1);
            
            // (필요 시) 두 번째 participant 객체 추가 예시 (최대 5개)
            cJSON *pet2 = cJSON_CreateObject();
            cJSON_AddStringToObject(pet2, "pet_id", "pet_002");
            cJSON_AddNumberToObject(pet2, "duration_sec", 45);
            cJSON_AddItemToArray(participants, pet2);            
            #endif
            cJSON_AddItemToObject(data_obj, "participants", participants);

            // 5. 나머지 숫자 및 선택 필드 추가
            cJSON_AddNumberToObject(data_obj, "start_weight", 500.5);   // Float
            cJSON_AddNumberToObject(data_obj, "end_weight", 420.2);     // Float
            cJSON_AddNumberToObject(data_obj, "total_intake_ml", 80.3); // Float
            cJSON_AddNumberToObject(data_obj, "duration_sec", 165);     // Integer
            
            // 선택 필드 (조건에 따라 추가 안 해도 됨)
            cJSON_AddStringToObject(data_obj, "alert_type", "NONE");
        break;
        case MESSEGE_DIAGNOSTICS:
            // 2. 공통 스칼라 필드 추가
            cJSON_AddNumberToObject(data_obj, "uptime_sec", 174739);
            cJSON_AddNumberToObject(data_obj, "reset_reason", 0);
            cJSON_AddNumberToObject(data_obj, "rssi_dbm", -70);

            subsystems = cJSON_CreateObject();
            cJSON_AddStringToObject(subsystems, "water_supply", "ok");
            cJSON_AddStringToObject(subsystems, "motor.pump", "fault"); // 펌프 에러 발생
            cJSON_AddStringToObject(subsystems, "loadcell", "ok");
            cJSON_AddStringToObject(subsystems, "tof", "ok");
            cJSON_AddStringToObject(subsystems, "ble", "ok");
            cJSON_AddStringToObject(subsystems, "uv", "ok");
            cJSON_AddStringToObject(subsystems, "filter.water", "ok");
            cJSON_AddStringToObject(subsystems, "filter.debris", "ok");
            cJSON_AddItemToObject(data_obj, "subsystems", subsystems);

            // 🔧 수정: W-100 fault_code 적용 (PUMP_ERR)
            cJSON_AddStringToObject(data_obj, "alert_level", "normal");
            cJSON_AddStringToObject(data_obj, "fault_code", "PUMP_ERR");
            cJSON_AddStringToObject(data_obj, "affected_subsystem", "motor.pump");

            // context 객체
            cJSON *context = cJSON_CreateObject();
            cJSON_AddNumberToObject(context, "pump_current_mA", 0); // 펌프 전류 측정값 예시
            cJSON_AddNumberToObject(context, "retry_count", 3);
            cJSON_AddItemToObject(data_obj, "context", context);
        break;
        case MESSEGE_HEALTH:
      // 🔧 수정: W-100 서브시스템 정의 적용
            subsystems = cJSON_CreateObject();
            cJSON_AddStringToObject(subsystems, "water_supply", "ok");
            cJSON_AddStringToObject(subsystems, "motor.pump", "ok");
            cJSON_AddStringToObject(subsystems, "loadcell", "ok");
            cJSON_AddStringToObject(subsystems, "tof", "ok");
            cJSON_AddStringToObject(subsystems, "ble", "ok");
            cJSON_AddStringToObject(subsystems, "uv", "ok");
            cJSON_AddStringToObject(subsystems, "filter.water", "ok");
            cJSON_AddStringToObject(subsystems, "filter.debris", "ok");
            cJSON_AddItemToObject(data_obj, "subsystems", subsystems);   

            // 🔧 필수 추가: W-100 헬스 측정 데이터 항목 (§2.6 참조)
            cJSON_AddStringToObject(data_obj, "power_source", "ADAPTER");
            cJSON_AddNumberToObject(data_obj, "water_level", 0);                // 0:충분, 1:부족, 2:없음
            cJSON_AddNumberToObject(data_obj, "pump_status", 0);                // 0:정상, 1:결착불량/이물질
            cJSON_AddNumberToObject(data_obj, "water_filter_life_pct", 95);     // 정수 필터 잔여수명 (%)
            cJSON_AddNumberToObject(data_obj, "debris_filter_life_pct", 80);    // 이물질 필터 잔여수명 (%)
            cJSON_AddNumberToObject(data_obj, "uv_status", 0);                  // 0:미소독, 1:소독중, 2:일시중지
            cJSON_AddNumberToObject(data_obj, "current_mode", 0);               // 0:급수, 1:정지, 2:스마트
        break;
        default:
        cJSON_Delete(data_obj);
        return NULL;
    }


    return data_obj;
}

void Send_cJSON_Messege(messege_tx_mqtt_cmd_e cmd)
{
    cJSON* root = Get_cJSON_Header(cmd);
    if(root == NULL)
        return;

    cJSON* data = Get_cJSON_Data(cmd);
    if(data == NULL)
    {
        cJSON_Delete(root);
        return;
    }

    uint8_t mac_byte[6];
    char dynamicMacStr[13]; // 12자리 MAC 문자열 + 널 종료 문자(\0)
    
    // ESP32의 기본 Wi-Fi MAC 주소를 읽어옵니다.
    esp_read_mac(mac_byte, ESP_MAC_WIFI_STA);

    // 읽어온 MAC 주소를 콜론 없이 대문자 16진수 문자열로 포맷팅합니다 (예: "28372F9C283C")
    snprintf(dynamicMacStr, sizeof(dynamicMacStr), "%02X%02X%02X%02X%02X%02X",
            mac_byte[0], mac_byte[1], mac_byte[2], mac_byte[3], mac_byte[4], mac_byte[5]);


    cJSON_AddItemToObject(root, "data", data);
    /* 공백 없이 압축된 JSON 문자열 생성 */
    char *payloadBuf = cJSON_PrintUnformatted(root);
    
    if (payloadBuf != NULL) 
    {
                        // 💡 토픽도 registration에 맞게 수정하실 수 있도록 남겨두었습니다.
        char pub_topic[100];
        const char * pubTopic = pub_topic; 
        switch(cmd)
        {
            case MESSEGE_REGISTRATION:
                snprintf(pub_topic,sizeof(pub_topic),SERVER_TX_TOPIC_REGISTRATION,dynamicMacStr);
            break;
            case MESSEGE_BOOT:
                snprintf(pub_topic,sizeof(pub_topic),SERVER_TX_TOPIC_BOOT,dynamicMacStr);
            break;
            case MESSEGE_ACCESS:
                snprintf(pub_topic,sizeof(pub_topic),SERVER_TX_TOPIC_ACCESS,dynamicMacStr);
            break;
            case MESSEGE_DRINK:
                snprintf(pub_topic,sizeof(pub_topic),SERVER_TX_TOPIC_DRINK,dynamicMacStr);
            break;
            case MESSEGE_DIAGNOSTICS:
                snprintf(pub_topic,sizeof(pub_topic),SERVER_TX_TOPIC_DIAGNOSTICS,dynamicMacStr);
            break;
            case MESSEGE_HEALTH:
                snprintf(pub_topic,sizeof(pub_topic),SERVER_TX_TOPIC_HEALTH,dynamicMacStr);
            break;
            default:
                pubTopic = NULL;
                ESP_LOGW(TAG, "Unknown command input: %d", cmd);
            break;
        }                          
        if(pubTopic != NULL)
        {
            /* 데이터 송신 (Publish) */
            bool pubStatus = PublishToTopic( pubTopic, strlen( pubTopic ), payloadBuf, strlen( payloadBuf ) );
            
            if( pubStatus == true )
            {
                ESP_LOGI(TAG,"퍼블리시 성공! [전송 카운트: %d]", 1);
                ESP_LOGI(TAG,"보낸 페이로드: %s", payloadBuf );
            }
        }
        cJSON_free(payloadBuf);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to print unformatted JSON");
    }
    cJSON_Delete(root);
}






