#include "tx_mqtt.h"

#include "topic_list.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mqtt_operations.h"
#include "aws_iot_task.h"
#include "app_config_flash.h"
#include "device_config.h"
#include "ble_tracker_id.h"
static const char *TAG = __FILE__;

Motion_Packet_t motion_res;
Motion_Packet_t health_res;
static uint16_t water_fault_code = 0;
static uint16_t water_fault_code_send = 0;
static uint16_t water_fault_code_buf = 0;
#define WATER_MAJOR 1
#define WATER_MINOR 1
#define WATER_PATCH 1
void water_fault_enable(uint16_t status)
{
    water_fault_code |= status;
    if(water_fault_code != water_fault_code_buf)
    {
        water_fault_code_send = status;
        water_fault_code_buf = water_fault_code;
        ESP_LOGI(TAG,"MESSEGE_DIAGNOSTICS = %04x", water_fault_code_send);
        
        mqtt_queue_send(MESSEGE_DIAGNOSTICS,NULL,0);
    }
}

void water_fault_disable(uint16_t status)
{
    water_fault_code &= ~(status);
    if(water_fault_code != water_fault_code_buf)
    {
        water_fault_code_buf = water_fault_code;
       // mqtt_queue_send(MESSEGE_DIAGNOSTICS);
    }
}

#define G_UUID
static cJSON* Get_cJSON_Header(messege_tx_mqtt_cmd_e cmd)
{

    uint32_t current_timestamp = (uint32_t)(esp_log_timestamp() / 1000); // 예시: 실제 UTC epoch sec 적용
    cJSON *root = cJSON_CreateObject();
    if(root == NULL)
        return root;
    /* Root 레벨 필수 필드 추가 */
    cJSON_AddStringToObject(root, "id", "123e4567-e89b-12d3-a456-426614174000"); /* 실제로는 uuid 생성 함수 사용 */
    if(TRACKER_MESSEGE_ACTIVITY <= cmd)
    {
        cJSON_AddStringToObject(root, "env", "alpha");
        cJSON_AddStringToObject(root, "device_type", "tracker"); 
    }
    else
    {
        cJSON_AddStringToObject(root, "env", "alpha");
        cJSON_AddStringToObject(root, "device_type", CONFIG_DEVICE_TYPE); 
    }
    
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
        case TRACKER_MESSEGE_ACTIVITY:
            cJSON_AddStringToObject(root, "event_type", "activity");
        break;
        case TRACKER_MESSEGE_HEALTH:
            cJSON_AddStringToObject(root, "event_type", "health");
        break;
        default:
        cJSON_Delete(root);
        return NULL;
    }
    char str[20];
    if(TRACKER_MESSEGE_ACTIVITY <= cmd)
    {

        snprintf(str, sizeof(str), "v%d.%d.%d",health_res.health_data_res.major,
                                                health_res.health_data_res.minor,
                                                health_res.health_data_res.patch
                            );
        cJSON_AddStringToObject(root, "firmware", str);
    }
    else
    {
        snprintf(str, sizeof(str), "v%d.%d.%d",WATER_MAJOR,
                                        WATER_MINOR,
                                        WATER_PATCH
                    );
        cJSON_AddStringToObject(root, "firmware", str);
    }

    cJSON_AddNumberToObject(root, "timestamp", current_timestamp); /* 현재 시간 unix epoch seconds */

    return root;
}
#include "opmode_task.h"
#include "ble_tracker_id.h"
#include "esp_wifi.h"
#include "esp_sntp.h"  // v5.x 호환용
#include "clock.h"
#include "math.h"
static cJSON* Get_cJSON_Data(mqtt_packet_t* mqtt_packet)
{
    cJSON *data_obj = cJSON_CreateObject();
    uint8_t mac_byte[6];
    char sub_string[20];
    char dynamicMacStr[13]; // 12자리 MAC 문자열 + 널 종료 문자(\0)
    cJSON *subsystems;
    uint32_t uptime = Clock_GetTimeMs() / 1000;
    wifi_ap_record_t ap_info;
    int8_t rssi = 0;
    float data_weight = 0;
    Tracker_Device_t* Tracker_Name = NULL;
    DRINK_Packet_t *DRINK_Packet = NULL;
    esp_reset_reason_t reason = esp_reset_reason();
    double weight = 0;
    // 현재 연결된 AP 정보 가져오기 (성공 시 ESP_OK 반환)
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) 
    {
        ESP_LOGI(TAG, "SSID: %s", ap_info.ssid);
        ESP_LOGI(TAG, "RSSI: %d dBm", ap_info.rssi); // 예: -55 dBm
        rssi = ap_info.rssi;
    } 

    if(data_obj == NULL)
        return data_obj;

    switch(mqtt_packet->cmd)
    {
        case MESSEGE_REGISTRATION:
            // ESP32의 기본 Wi-Fi MAC 주소를 읽어옵니다.
            esp_read_mac(mac_byte, ESP_MAC_WIFI_STA);
            
            // 읽어온 MAC 주소를 콜론 없이 대문자 16진수 문자열로 포맷팅합니다 (예: "28372F9C283C")
            snprintf(dynamicMacStr, sizeof(dynamicMacStr), "%02X%02X%02X%02X%02X%02X",
                    mac_byte[0], mac_byte[1], mac_byte[2], mac_byte[3], mac_byte[4], mac_byte[5]);

            cJSON_AddStringToObject(data_obj, "mac", dynamicMacStr);
            cJSON_AddStringToObject(data_obj, "hw_rev", "r3.1");
            time_t now;
            time(&now); // 1970년 1월 1일 이후 경과된 초(sec)를 'now' 변수에 담음
            if (now < 1600000000) {
                // 아직 SNTP 시간 동기화가 안 된 경우
                // 백엔드 서버가 대신 처리하도록 0을 보내거나 기본값 설정
                cJSON_AddNumberToObject(data_obj, "paired_at", 0);
            } else {
                // 정상 동기화된 경우 현재 시간 전송
                cJSON_AddNumberToObject(data_obj, "paired_at", (double)now);
            }
        break;
        case MESSEGE_BOOT:
            cJSON_AddStringToObject(data_obj, "boot_reason", "power_on");
            cJSON_AddNumberToObject(data_obj, "uptime_sec", uptime);
            
            cJSON_AddStringToObject(data_obj, "power_source", "ADAPTER");
            cJSON_AddNumberToObject(data_obj, "reset_reason", reason);
            cJSON_AddStringToObject(data_obj, "last_shutdown_reason", "unknown");               
        break;
        case MESSEGE_ACCESS:
            Tracker_Name = GetTracker_Id_Name();
            cJSON_AddStringToObject(data_obj, "access_id", "1234");
            if(Tracker_Name != NULL)
            {
                cJSON_AddStringToObject(data_obj, "source", "tracker");
                cJSON_AddStringToObject(data_obj, "beacon_id", Tracker_Name->Device_ID);
                cJSON_AddNumberToObject(data_obj, "rssi_dbm", Tracker_Name->dev_info.rssi);
            }
            else
            {
                cJSON_AddStringToObject(data_obj, "source", "sensor");
                cJSON_AddStringToObject(data_obj, "beacon_id", "UNKNOWN");
                cJSON_AddNumberToObject(data_obj, "rssi_dbm", 0);
            }
            
// 🔧 수정: 문자열이 아닌 Float(숫자) 타입으로 전달해야 함
            if(mqtt_packet->data != NULL)
            {
                data_weight = *(float *)mqtt_packet->data;
                weight = round((double)data_weight * 100.0) / 100.0;
            }   
            cJSON_AddNumberToObject(data_obj, "start_weight", weight);

            
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
            if(mqtt_packet->data != NULL)
            {
                DRINK_Packet = (DRINK_Packet_t*)mqtt_packet->data;
                // 5. 나머지 숫자 및 선택 필드 추가
                weight = round((double)DRINK_Packet->start_weight * 100.0) / 100.0;
                cJSON_AddNumberToObject(data_obj, "start_weight", weight);
                weight = round((double)DRINK_Packet->end_weight * 100.0) / 100.0;
                cJSON_AddNumberToObject(data_obj, "end_weight", weight);     // Float
                weight = round((double)DRINK_Packet->total_intake_ml * 100.0) / 100.0;
                cJSON_AddNumberToObject(data_obj, "total_intake_ml", weight); // Float
                cJSON_AddNumberToObject(data_obj, "duration_sec", DRINK_Packet->duration_sec);     // Integer
            }
            else
            {
                cJSON_AddNumberToObject(data_obj, "start_weight", 0);   // Float
                cJSON_AddNumberToObject(data_obj, "end_weight", 0);     // Float
                cJSON_AddNumberToObject(data_obj, "total_intake_ml", 0); // Float
                cJSON_AddNumberToObject(data_obj, "duration_sec", 0);     // Integer                
            }
            
            // 선택 필드 (조건에 따라 추가 안 해도 됨)
            cJSON_AddStringToObject(data_obj, "alert_type", "NONE");
        break;
        case MESSEGE_DIAGNOSTICS:
            // 2. 공통 스칼라 필드 추가

            cJSON_AddNumberToObject(data_obj, "uptime_sec", uptime);
            cJSON_AddNumberToObject(data_obj, "reset_reason", reason);
            cJSON_AddNumberToObject(data_obj, "rssi_dbm", rssi);

            subsystems = cJSON_CreateObject();
            cJSON_AddStringToObject(subsystems, "water_supply", "ok");
            cJSON_AddStringToObject(subsystems, "motor.pump", "ok"); // 펌프 에러 발생
            cJSON_AddStringToObject(subsystems, "loadcell", "ok");
            cJSON_AddStringToObject(subsystems, "tof", "ok");
            cJSON_AddStringToObject(subsystems, "ble", "ok");
            cJSON_AddStringToObject(subsystems, "uv", "ok");
            cJSON_AddStringToObject(subsystems, "filter.water", "ok");
            cJSON_AddStringToObject(subsystems, "filter.debris", "ok");
            cJSON_AddStringToObject(subsystems, "power",         "ok");
            if (water_fault_code_send & WATER_LOW_FAULT) {
               cJSON_AddNumberToObject(subsystems, "water_level", 1);
            }
            else
               cJSON_AddNumberToObject(subsystems, "water_level", 0);    
            app_config_t* app_config = get_app_config();
            cJSON_AddNumberToObject(subsystems, "current_mode", app_config->op_mode); // Current Mode 추가        
            cJSON_AddItemToObject(data_obj, "subsystems", subsystems);

            // 🔧 수정: W-100 fault_code 적용 (PUMP_ERR)
            cJSON_AddStringToObject(data_obj, "mode", "operational");

            if(water_fault_code_send & WATER_BOWL_DETACHED_FAULT)
            {
                cJSON_AddStringToObject(data_obj, "alert_level", "critical");
                cJSON_AddStringToObject(data_obj, "fault_code", "BOWL_DETACHED");
                sprintf(sub_string,"loadcell");
            }
            else if(water_fault_code_send & WATER_FILTER_WATER_EX)
            {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "FILTER_WATER_EXPIRED");
                sprintf(sub_string,"filter.water"); 
            }
            else if(water_fault_code_send & WATER_FILTER_DEBRIS_EX)
            {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "FILTER_DEBRIS_EXPIRED");
                sprintf(sub_string,"filter.debris"); 
            }
            else if(water_fault_code_send & WATER_LOW_FAULT)
            {
                cJSON_AddStringToObject(data_obj, "alert_level", "critical");
                cJSON_AddStringToObject(data_obj, "fault_code", "WATER_EMPTY");
                sprintf(sub_string,"water_supply");
            }
            else if(water_fault_code_send & WATER_SPLASHING_FAULT)
            {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "SPLASHING");
                sprintf(sub_string,"behavior"); 
            }
            else
            {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "PUMP_ERR");
            }

            cJSON_AddStringToObject(data_obj, "affected_subsystem", "motor.pump");

            // context 객체
            cJSON *context = cJSON_CreateObject();
            cJSON_AddItemToObject(data_obj, "context", context);
        break;
        case MESSEGE_HEALTH:
      // 🔧 수정: W-100 서브시스템 정의 적용
            // 2. 공통 스칼라 필드 추가
            cJSON_AddNumberToObject(data_obj, "uptime_sec", uptime);
            cJSON_AddNumberToObject(data_obj, "reset_reason", reason);
            cJSON_AddNumberToObject(data_obj, "rssi_dbm", rssi);
            subsystems = cJSON_CreateObject();
            cJSON_AddStringToObject(subsystems, "water_supply", "ok");
            cJSON_AddStringToObject(subsystems, "motor.pump", "ok");
            cJSON_AddStringToObject(subsystems, "loadcell", "ok");
            cJSON_AddStringToObject(subsystems, "tof", "ok");
            cJSON_AddStringToObject(subsystems, "ble", "ok");
            cJSON_AddStringToObject(subsystems, "uv", "ok");
            cJSON_AddStringToObject(subsystems, "filter.water", "ok");
            cJSON_AddStringToObject(subsystems, "filter.debris", "ok");
            cJSON_AddStringToObject(subsystems, "power",         "ok");
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

// -------------------------------------------------------------
// T-100 전용 subsystems cJSON 객체 생성 함수
// -------------------------------------------------------------
cJSON* create_t100_subsystems_json(fault_code fault)
{
    cJSON *subsystems = cJSON_CreateObject();
    
    // fault_code 비트필드 상태에 따라 "ok" / "fault" 매핑
    cJSON_AddStringToObject(subsystems, "power",   (fault.bit.Bat_Status > 1) ? "fault" : "ok");
    cJSON_AddStringToObject(subsystems, "imu",     (fault.bit.IMU_Err)       ? "fault" : "ok");
    cJSON_AddStringToObject(subsystems, "ble",     (fault.bit.BLE_Err)       ? "fault" : "ok");
    cJSON_AddStringToObject(subsystems, "storage", (fault.bit.storage)       ? "fault" : "ok");

    return subsystems;
}
static cJSON* Get_cJSON_Data_for_Tracker(messege_tx_mqtt_cmd_e cmd, tracker_mqtt_packet_t* tracker_mqtt_packet)
{
    cJSON *data_obj = cJSON_CreateObject();
    Motion_Packet_t* packet = &tracker_mqtt_packet->packet;
    const uint16_t * points;
    char dynamicMacStr[30]; // 12자리 MAC 문자열 + 널 종료 문자(\0)
    cJSON *subsystems;
    if(data_obj == NULL)
        return data_obj;

    switch(cmd)
    {
        case TRACKER_MESSEGE_ACTIVITY:
            snprintf(dynamicMacStr, sizeof(dynamicMacStr), "TRACKER_%02X%02X%02X%02X%02X%02X",
            tracker_mqtt_packet->mac[0], tracker_mqtt_packet->mac[1], tracker_mqtt_packet->mac[2],
             tracker_mqtt_packet->mac[3], tracker_mqtt_packet->mac[4], tracker_mqtt_packet->mac[5]);
            cJSON_AddStringToObject(data_obj, "tracker_id", dynamicMacStr); // 예: "TRACKER_AABBCCDDEEFF"        
            cJSON_AddStringToObject(data_obj, "activity_id", "123e4567-e89b-12d3-a456-426614174000");
            cJSON_AddNumberToObject(data_obj, "seq_num", packet->motion_data.seq);
            cJSON_AddNumberToObject(data_obj, "interval_min", motion_res.motion_req.interval);
            cJSON_AddNumberToObject(data_obj, "total_points", motion_res.motion_req.total_points);
            // 4. points 배열 추가 (uint16_t 그대로 추가, 비트 언패킹은 백엔드가 수행)
            cJSON *points_array = cJSON_CreateArray();
// pack_data 포인터를 배열처럼 접근하기 위해 uint16_t 포인터로 캐스팅
            points = (const uint16_t *)tracker_mqtt_packet->data;


            for (int i = 0; i < tracker_mqtt_packet->data_len; i++) {
                cJSON_AddItemToArray(points_array, cJSON_CreateNumber(points[i]));
            }
            cJSON_AddItemToObject(data_obj, "points", points_array);
            cJSON_AddNumberToObject(data_obj, "battery_pct", health_res.health_data_res.Bat_Level);

        break;
        case TRACKER_MESSEGE_DIAGNOSTICS:
        {
            // 1. 트래커 필수 확장 필드 (T-100 문서 §3)
            cJSON_AddNumberToObject(data_obj, "seq_num", 1);

            // 2. 공통 스칼라 필드 (04-diagnostics-and-health §2.2)
            cJSON_AddNumberToObject(data_obj, "uptime_sec", packet->health_data_res.uptime_sec);
            cJSON_AddNumberToObject(data_obj, "reset_reason", packet->health_data_res.fault_flag.bit.reset_reason);
            cJSON_AddNumberToObject(data_obj, "rssi_dbm", packet->health_data_res.target_rssi);

            // 3. T-100 서브시스템 상태 스냅샷 (§7 명세)
            subsystems = create_t100_subsystems_json(packet->health_data_res.fault_flag);

            cJSON_AddItemToObject(data_obj, "subsystems", subsystems);

            // 4. 진단 전용 정보 (§3.3 & §6 명세)
            cJSON_AddStringToObject(data_obj, "mode", "operational"); // operational / qc / obd
            
            // fault_flag 비트에 따른 fault_code & alert_level 매핑 예시
            if (packet->health_data_res.fault_flag.bit.IMU_Err) {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "IMU_ERR");
                cJSON_AddStringToObject(data_obj, "affected_subsystem", "imu");
            } else if (packet->health_data_res.fault_flag.bit.BLE_Err) {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "BLE_ERR");
                cJSON_AddStringToObject(data_obj, "affected_subsystem", "ble");
            } else if (packet->health_data_res.fault_flag.bit.storage) {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "STORAGE_ERR");
                cJSON_AddStringToObject(data_obj, "affected_subsystem", "storage");
            } else if (packet->health_data_res.fault_flag.bit.Bat_Status == 2) {
                cJSON_AddStringToObject(data_obj, "alert_level", "normal");
                cJSON_AddStringToObject(data_obj, "fault_code", "BATTERY_LOW");
                cJSON_AddStringToObject(data_obj, "affected_subsystem", "power");
            } else if (packet) {
                cJSON_AddStringToObject(data_obj, "alert_level", "critical");
                cJSON_AddStringToObject(data_obj, "fault_code", "BATTERY_CRITICAL");
                cJSON_AddStringToObject(data_obj, "affected_subsystem", "power");
            }

            // 5. Context 객체 (필요 시 진단 보조 데이터)
            cJSON *context = cJSON_CreateObject();
            cJSON_AddNumberToObject(context, "raw_fault_byte", packet->health_data_res.fault_flag.byte);
            cJSON_AddItemToObject(data_obj, "context", context);
            break;
        }

        case TRACKER_MESSEGE_HEALTH:
        {
            // 1. 트래커 필수 확장 필드 (T-100 문서 §3)
            cJSON_AddNumberToObject(data_obj, "seq_num", 2);

            // 2. 공통 스칼라 필드 (04-diagnostics-and-health §1.2)
            cJSON_AddNumberToObject(data_obj, "uptime_sec", packet->health_data_res.uptime_sec);
            cJSON_AddNumberToObject(data_obj, "reset_reason", packet->health_data_res.fault_flag.bit.reset_reason);
            cJSON_AddNumberToObject(data_obj, "rssi_dbm", packet->health_data_res.target_rssi);

            // 3. T-100 서브시스템 상태 스냅샷 (§7 명세)
            // 3. T-100 서브시스템 상태 스냅샷 (§7 명세)
            subsystems = create_t100_subsystems_json(packet->health_data_res.fault_flag);
                
            cJSON_AddItemToObject(data_obj, "subsystems", subsystems);

            // 4. T-100 전용 헬스 필드 (T-100 §3.4 명세)
            cJSON_AddNumberToObject(data_obj, "battery_pct", packet->health_data_res.Bat_Level);
            
            // fw_version 포맷팅 ("v1.0.4")
            char fw_str[16];
            snprintf(fw_str, sizeof(fw_str), "v%u.%u.%u", 
                     packet->health_data_res.major, 
                     packet->health_data_res.minor, 
                     packet->health_data_res.patch);
            cJSON_AddStringToObject(data_obj, "fw_version", fw_str);

            // 게이트웨이가 측정해 준 BLE RSSI (옵션 필드)
            cJSON_AddNumberToObject(data_obj, "gateway_rssi_dbm", packet->health_data_res.target_rssi);
            break;
        }

        default:
        cJSON_Delete(data_obj);
        return NULL;
    }


    return data_obj;
}


void Send_cJSON_Messege(mqtt_packet_t* mqtt_packet)
{
    cJSON* root = NULL;

    uint8_t mac_byte[6];
    char dynamicMacStr[13]; // 12자리 MAC 문자열 + 널 종료 문자(\0)
    
    // ESP32의 기본 Wi-Fi MAC 주소를 읽어옵니다.
    esp_read_mac(mac_byte, ESP_MAC_WIFI_STA);

    // 읽어온 MAC 주소를 콜론 없이 대문자 16진수 문자열로 포맷팅합니다 (예: "28372F9C283C")
    snprintf(dynamicMacStr, sizeof(dynamicMacStr), "%02X%02X%02X%02X%02X%02X",
            mac_byte[0], mac_byte[1], mac_byte[2], mac_byte[3], mac_byte[4], mac_byte[5]);

// 1. AWS 예약 기능용 메시지 처리 (Header 감싸지 않음)
    if (mqtt_packet->cmd == AWS_MESSEGE_AWS_JOBS_GET) 
    {
        // Get_cJSON_Data()가 반환하는 JSON 객체를 그대로 root로 사용!
        root = Get_cJSON_Data(mqtt_packet);
        if (root == NULL) 
        {
             ESP_LOGI(TAG,"Header = %d" , mqtt_packet->cmd);
             return;
        }
    } 
    else 
    {
        root = Get_cJSON_Header(mqtt_packet->cmd);
        if (root == NULL) 
        {
             ESP_LOGI(TAG,"Header = %d" , mqtt_packet->cmd);
             return;
        }
        

        cJSON* data = Get_cJSON_Data(mqtt_packet);
        if (data == NULL) {
            ESP_LOGI(TAG,"data = %d" , mqtt_packet->cmd);
            cJSON_Delete(root);
            return;
        }
        cJSON_AddItemToObject(root, "data", data);
    }

    /* 공백 없이 압축된 JSON 문자열 생성 */
    char *payloadBuf = cJSON_PrintUnformatted(root);
    
    if (payloadBuf != NULL) 
    {
                        // 💡 토픽도 registration에 맞게 수정하실 수 있도록 남겨두었습니다.
        char pub_topic[100];
        const char * pubTopic = pub_topic; 
        switch(mqtt_packet->cmd)
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
                ESP_LOGW(TAG, "Unknown command input: %d", mqtt_packet->cmd);
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



void Send_cJSON_Messege_for_tracker(tracker_mqtt_packet_t* tracker_mqtt_packet)
{
    Motion_Packet_t* packet = &tracker_mqtt_packet->packet;
    messege_tx_mqtt_cmd_e cmd = tracker_mqtt_packet->cmd;
    if(cmd == TRACKER_MESSEGE_HEALTH)
        memcpy(&health_res,&tracker_mqtt_packet->packet,sizeof(Motion_Packet_t));
    if(packet->event_code == MOTION_START_RESPONSE)
    {
        memcpy(&motion_res,&tracker_mqtt_packet->packet,sizeof(Motion_Packet_t));
        printf("total = %d interval = %d ",motion_res.motion_req.total_points,motion_res.motion_req.interval);
        return;
    }
    cJSON* root = Get_cJSON_Header(cmd);
    if(root == NULL)
        return;

    cJSON* data = Get_cJSON_Data_for_Tracker(cmd,tracker_mqtt_packet);
    if(data == NULL)
    {
        cJSON_Delete(root);
        return;
    }

    cJSON_AddItemToObject(root, "data", data);

    char dynamicMacStr[13]; // 12자리 MAC 문자열 + 널 종료 문자(\0)
    // 읽어온 MAC 주소를 콜론 없이 대문자 16진수 문자열로 포맷팅합니다 (예: "28372F9C283C")
    snprintf(dynamicMacStr, sizeof(dynamicMacStr), "%02X%02X%02X%02X%02X%02X",
            tracker_mqtt_packet->mac[0], tracker_mqtt_packet->mac[1], tracker_mqtt_packet->mac[2],
             tracker_mqtt_packet->mac[3], tracker_mqtt_packet->mac[4], tracker_mqtt_packet->mac[5]);


    /* 공백 없이 압축된 JSON 문자열 생성 */
    char *payloadBuf = cJSON_PrintUnformatted(root);
    
    if (payloadBuf != NULL) 
    {
                        // 💡 토픽도 registration에 맞게 수정하실 수 있도록 남겨두었습니다.
        char pub_topic[100];
        const char * pubTopic = pub_topic; 
        switch(cmd)
        {
            case TRACKER_MESSEGE_ACTIVITY:
                snprintf(pub_topic,sizeof(pub_topic),TRACKER_TX_TOPIC_ACTIVITY,dynamicMacStr);
            break;
            case TRACKER_MESSEGE_HEALTH:
                snprintf(pub_topic,sizeof(pub_topic),TRACKER_TX_TOPIC_HEALTH,dynamicMacStr);
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



