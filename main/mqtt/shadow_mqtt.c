#include "shadow_mqtt.h"
#include "aws_iot_task.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#include "cJSON.h"
#include "esp_log.h"
#include "app_config_flash.h"
#include "opmode_task.h"
#include "app_led.h"
#include "app_moter.h"
#define TAG __FILE__
// 중복 실행 방지를 위한 직전 처리 UUID 캐시

void shadow_mqtt_send(shadow_set_e state);
typedef struct {
    // 공통 메타 필드
    uint32_t version;
    uint32_t timestamp;

    // 기기 상태 및 모드 설정
    bool filter_alert_enabled;
    int sleep_mode;
    int current_mode;

    // 명령 요청 식별용 UUID (36자 + '\0')
    char tare_request[40];
    char water_filter_reset_request[40];
    char debris_filter_reset_request[40];
    char reboot_request[40];
} shadow_packet_info_t;
void print_shadow_packet_info(const shadow_packet_info_t *info);
static shadow_packet_info_t shadow_packet_info;

typedef struct {
    shadow_packet_info_t shadow_packet_info;

    struct {
        uint8_t has_filter_alert_enabled : 1;
        uint8_t has_sleep_mode           : 1;
        uint8_t has_current_mode         : 1;
        uint8_t has_tare_request         : 1;
        uint8_t has_water_filter_reset   : 1;
        uint8_t has_debris_filter_reset  : 1;
        uint8_t has_reboot_request       : 1;
    } valid;
} shadow_state_info_t;
bool Get_Accepted_Init = false;
#if 0
/* ========================================================== */
/* [ACK 전송 헬퍼] $aws/things/<thing>/shadow/update 로 발행    */
/* ========================================================== */
static void shadow_send_reported(const char *json_reported_body)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(TAG, "MAC 주소 읽기 실패");
        return;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "$aws/things/%s_%02X%02X%02X%02X%02X%02X/shadow/update",
             CONFIG_DEVICE_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char payload[512];
    snprintf(payload, sizeof(payload), "{\"state\":{\"reported\":{%s}}}", json_reported_body);

    ESP_LOGI(TAG, "▶ [Shadow Reported 전송] %s", payload);
    mqtt_publish_to_server(topic, payload, strlen(payload), 1);
}
#endif
void set_filter_alert_enabled(bool state);
void Set_tare_request(void)
{
    shadow_mqtt_send(TARE_REQUEST);
}
bool Get_Accepted_Init_state(void)
{
    return Get_Accepted_Init;
}
void print_shadow_packet_info(const shadow_packet_info_t *info)
{
    if (info == NULL) {
        ESP_LOGE(TAG, "[Shadow Info] 포인터가 NULL입니다.");
        return;
    }

    ESP_LOGI(TAG, "┌─────────────── [Shadow Packet Info] ───────────────┐");
    ESP_LOGI(TAG, "│ Version              : %-35u│", info->version);
    ESP_LOGI(TAG, "│ Timestamp            : %-35u│", info->timestamp);
    ESP_LOGI(TAG, "├────────────────────────────────────────────────────┤");
    ESP_LOGI(TAG, "│ Filter Alert Enabled : %-35s│", info->filter_alert_enabled ? "true" : "false");
    ESP_LOGI(TAG, "│ Sleep Mode           : %-35d│", info->sleep_mode);
    ESP_LOGI(TAG, "│ Current Mode         : %-35d│", info->current_mode);
    ESP_LOGI(TAG, "├────────────────────────────────────────────────────┤");
    ESP_LOGI(TAG, "│ Tare Request UUID    : %-35s│", info->tare_request[0] ? info->tare_request : "(none)");
    ESP_LOGI(TAG, "│ Water Filter UUID    : %-35s│", info->water_filter_reset_request[0] ? info->water_filter_reset_request : "(none)");
    ESP_LOGI(TAG, "│ Debris Filter UUID   : %-35s│", info->debris_filter_reset_request[0] ? info->debris_filter_reset_request : "(none)");
    ESP_LOGI(TAG, "│ Reboot Request UUID  : %-35s│", info->reboot_request[0] ? info->reboot_request : "(none)");
    ESP_LOGI(TAG, "└────────────────────────────────────────────────────┘");
}

void shadow_mqtt_send(shadow_set_e state)
{
    //   mqtt_queue_send(AWS_MESSEGE_JOBS_UPDATE,&JOBS_Update_Packet,sizeof(JOBS_Update_Packet_t)); 
    #define PAYLOAD_BUFFER_SIZE  256

    char *payload = calloc(1, PAYLOAD_BUFFER_SIZE);
    if (payload == NULL) {
        return;
    }
    app_config_t* app_config = get_app_config();


    switch (state)
    {
        /* ------------------------------------------------------------------ */
        /* B1. 작동 방식 변경 (current_mode echo: 0 또는 3)                     */
        /* ------------------------------------------------------------------ */
        case CURRUNT_MODE:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"current_mode\":%d}}}", 
                    shadow_packet_info.current_mode);
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
            break;

        /* ------------------------------------------------------------------ */
        /* 필터 알림 활성화 여부 (설정 echo)                                     */
        /* ------------------------------------------------------------------ */
        case FILTER_ALERT_ENABLE:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"filter_alert_enabled\":%s}}}", 
                    shadow_packet_info.filter_alert_enabled ? "true" : "false");
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
            break;

        /* ------------------------------------------------------------------ */
        /* B9. 수면 모드 (sleep_mode echo)                                    */
        /* ------------------------------------------------------------------ */
        case SLEEP_MODE:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"sleep_mode\":%d}}}", 
                    shadow_packet_info.sleep_mode);
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
            break;

        /* ------------------------------------------------------------------ */
        /* B6. 영점 조절 (tare_ack echo + 갱신된 loadcell_tare)                */
        /* ------------------------------------------------------------------ */
        case TARE_REQUEST:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"tare_ack\":\"%s\",\"loadcell_tare\":%ld}}}", 
                    shadow_packet_info.tare_request, 
                    app_config->case_raw_data);
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
        break;

        /* ------------------------------------------------------------------ */
        /* B4. 정수 필터 초기화 (water_filter_reset_ack + 수명 100%)            */
        /* ------------------------------------------------------------------ */
        case WATER_FILETER_RESET:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"water_filter_reset_ack\":\"%s\",\"water_filter_life_pct\":100}}}", 
                    shadow_packet_info.water_filter_reset_request);
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
            break;

        /* ------------------------------------------------------------------ */
        /* B5. 이물질 필터 초기화 (debris_filter_reset_ack + 수명 100%)          */
        /* ------------------------------------------------------------------ */
        case DEBRIS_FILTER_RESET:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"debris_filter_reset_ack\":\"%s\",\"debris_filter_life_pct\":100}}}", 
                    shadow_packet_info.debris_filter_reset_request);
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
            break;

        /* ------------------------------------------------------------------ */
        /* B3. 원격 재부팅 (reboot_ack echo 후 기기 재부팅)                      */
        /* ------------------------------------------------------------------ */
        case REBOOT_REQUEST:
            snprintf(payload, PAYLOAD_BUFFER_SIZE, 
                    "{\"state\":{\"reported\":{\"reboot_ack\":\"%s\"}}}", 
                    shadow_packet_info.reboot_request);
            mqtt_queue_send(AWS_MESSEGE_SHADOW_UPDATE, payload, strlen(payload) + 1);
            break;

        default:
            break;
    }
    free(payload);
}
bool parse_shadow_get_state(const char *payload, size_t len, shadow_state_info_t *out_info)
{
    if (payload == NULL || out_info == NULL) {
        return false;
    }

    memset(out_info, 0, sizeof(shadow_state_info_t));

    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (root == NULL) {
        ESP_LOGE(TAG, "get/accepted JSON 파싱 실패");
        return false;
    }

    // 1. 공통 메타 필드 (version: 79, timestamp: 1793153431)
    cJSON *ver_item = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsNumber(ver_item)) {
        shadow_packet_info.version = (uint32_t)ver_item->valuedouble;
        out_info->shadow_packet_info.version = shadow_packet_info.version;
    }

    cJSON *time_item = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsNumber(time_item)) {
        shadow_packet_info.timestamp = (uint32_t)time_item->valuedouble;
        out_info->shadow_packet_info.timestamp = shadow_packet_info.timestamp;
    }

    // 2. state 객체 진입
    cJSON *state = cJSON_GetObjectItem(root, "state");
    if (state == NULL || !cJSON_IsObject(state)) {
        cJSON_Delete(root);
        return false;
    }

    // 3. state 내부의 "desired" 타겟 객체 진입 (부팅 동기화는 desired 기준)
    cJSON *target = cJSON_GetObjectItem(state, "desired");
    if (target == NULL || !cJSON_IsObject(target)) {
        // desired가 없을 경우 delta 시도
        target = cJSON_GetObjectItem(state, "delta");
    }

    if (target == NULL || !cJSON_IsObject(target)) {
        ESP_LOGW(TAG, "state 내부에 desired/delta 객체가 없음");
        cJSON_Delete(root);
        return false;
    }

    // 4. 필드별 파싱 및 static 변수와 비교하여 변경된 항목만 valid 비트 세팅

    // filter_alert_enabled (bool: true)
    cJSON *item = cJSON_GetObjectItem(target, "filter_alert_enabled");
    if (cJSON_IsBool(item)) {
        bool val = cJSON_IsTrue(item);
        shadow_packet_info.filter_alert_enabled = val;
        out_info->valid.has_filter_alert_enabled = 1;
        out_info->shadow_packet_info.filter_alert_enabled = val;
    }

    // sleep_mode (int: 0)
    item = cJSON_GetObjectItem(target, "sleep_mode");
    if (cJSON_IsNumber(item)) {
        int val = item->valueint;
        shadow_packet_info.sleep_mode = val;
        out_info->valid.has_sleep_mode = 1;
        out_info->shadow_packet_info.sleep_mode = val;
    }

    // current_mode (int: 0) -> 현재 reported가 3이므로 0으로 변경 감지됨
    item = cJSON_GetObjectItem(target, "current_mode");
    if (cJSON_IsNumber(item)) {
        int val = item->valueint;
        shadow_packet_info.current_mode = val;
        out_info->valid.has_current_mode = 1;
        out_info->shadow_packet_info.current_mode = val;
    }

    // tare_request ("4f260ef6-f24d-4e9c-b41f-ab8a77e1af0b")
    item = cJSON_GetObjectItem(target, "tare_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.tare_request, item->valuestring, sizeof(shadow_packet_info.tare_request)) != 0) {
            snprintf(shadow_packet_info.tare_request, sizeof(shadow_packet_info.tare_request), "%s", item->valuestring);
            out_info->valid.has_tare_request = 1;
        }
        snprintf(out_info->shadow_packet_info.tare_request, sizeof(out_info->shadow_packet_info.tare_request), "%s", item->valuestring);
    }

    // water_filter_reset_request ("58da8fb4-fcd1-401d-8cdb-b456d4f4a786")
    item = cJSON_GetObjectItem(target, "water_filter_reset_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.water_filter_reset_request, item->valuestring, sizeof(shadow_packet_info.water_filter_reset_request)) != 0) {
            snprintf(shadow_packet_info.water_filter_reset_request, sizeof(shadow_packet_info.water_filter_reset_request), "%s", item->valuestring);
            out_info->valid.has_water_filter_reset = 1;
        }
        snprintf(out_info->shadow_packet_info.water_filter_reset_request, sizeof(out_info->shadow_packet_info.water_filter_reset_request), "%s", item->valuestring);
    }

    // debris_filter_reset_request ("850db5bb-8ed5-4205-b55e-04048975f9e5")
    item = cJSON_GetObjectItem(target, "debris_filter_reset_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.debris_filter_reset_request, item->valuestring, sizeof(shadow_packet_info.debris_filter_reset_request)) != 0) {
            snprintf(shadow_packet_info.debris_filter_reset_request, sizeof(shadow_packet_info.debris_filter_reset_request), "%s", item->valuestring);
            out_info->valid.has_debris_filter_reset = 1;
        }
        snprintf(out_info->shadow_packet_info.debris_filter_reset_request, sizeof(out_info->shadow_packet_info.debris_filter_reset_request), "%s", item->valuestring);
    }

    // reboot_request ("c29ecda9-a78e-432e-9d94-918742c2e55e")
    item = cJSON_GetObjectItem(target, "reboot_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.reboot_request, item->valuestring, sizeof(shadow_packet_info.reboot_request)) != 0) {
            snprintf(shadow_packet_info.reboot_request, sizeof(shadow_packet_info.reboot_request), "%s", item->valuestring);
            out_info->valid.has_reboot_request = 1;
        }
        snprintf(out_info->shadow_packet_info.reboot_request, sizeof(out_info->shadow_packet_info.reboot_request), "%s", item->valuestring);
    }

    cJSON_Delete(root);
    return true;
}

void shadow_mqtt_get_accepted_rx_messege(MQTTPublishInfo_t * pPublishInfo)
{
    char* Payload = calloc(1,pPublishInfo->payloadLength + 1);
    int payload_len = pPublishInfo->payloadLength;

    if(Payload == NULL)
        return;
    memcpy(Payload,pPublishInfo->pPayload,  pPublishInfo->payloadLength);
    Payload[pPublishInfo->payloadLength] = 0;

    // int 캐스팅으로 길이를 넘겨주고 버퍼 포인터 전달
    ESP_LOGI(TAG, "Payload Length: %d, Data: %.*s", 
            (int)payload_len, 
            (int)payload_len, 
            (const char *)Payload);
    shadow_state_info_t shadow_data;

    if (!parse_shadow_get_state((const char *)pPublishInfo->pPayload, pPublishInfo->payloadLength, &shadow_data)) {
        ESP_LOGE(TAG, "Shadow Get 응답 파싱 실패");
        return;
    }
    Get_Accepted_Init = true;
    ESP_LOGI(TAG, "=== 부팅 시 Shadow 최신 동기화 완료 (v%u) ===", shadow_data.shadow_packet_info.version);

    // 1. 동작 모드 동기화 (desired: current_mode = 0)
    if (shadow_data.valid.has_current_mode) {
        ESP_LOGI(TAG, "최신 동작 모드 적용: %d", shadow_packet_info.current_mode);
        Opmode_Set((uint32_t*)&shadow_packet_info.current_mode);
    }

    // 2. 수면 모드 동기화 (desired: sleep_mode = 0)
    if (shadow_data.valid.has_sleep_mode) {
        ESP_LOGI(TAG, "최신 수면 모드 적용: %d", shadow_packet_info.sleep_mode);
        Night_Mode(shadow_packet_info.sleep_mode);
    }

    // 3. 필터 알림 활성화 여부 동기화 (desired: true)
    if (shadow_data.valid.has_filter_alert_enabled) {
        ESP_LOGI(TAG, "필터 알림 옵션 적용: %s", 
                 shadow_packet_info.filter_alert_enabled ? "true" : "false");
        set_filter_alert_enabled(shadow_packet_info.filter_alert_enabled);                 
    }
    print_shadow_packet_info(&shadow_packet_info);
    // ※ 주의: 부팅 직후 get/accepted에 들어있는 reboot_request, tare_request 등은 
    // 이전 실행 기록일 수 있으므로, 이미 처리된 UUID는 NVS 등과 비교하여 재실행되지 않도록 주의해야 합니다.            
}

bool parse_shadow_state(const char *payload, size_t len, shadow_state_info_t *out_info)
{
    if (payload == NULL || len == 0 || out_info == NULL) {
        ESP_LOGE(TAG, "인자 오류: payload 또는 out_info가 NULL이거나 길이가 0");
        return false;
    }

    memset(out_info, 0, sizeof(shadow_state_info_t));

    // 1. JSON 파싱
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 파싱 실패! (len: %d)", (int)len);
        return false;
    }

    // 2. 공통 메타 필드 (version, timestamp)
    cJSON *ver_item = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsNumber(ver_item)) {
        shadow_packet_info.version = (uint32_t)ver_item->valuedouble;
        out_info->shadow_packet_info.version = shadow_packet_info.version;
    }

    cJSON *time_item = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsNumber(time_item)) {
        shadow_packet_info.timestamp = (uint32_t)time_item->valuedouble;
        out_info->shadow_packet_info.timestamp = shadow_packet_info.timestamp;
    }

    // 3. state 오브젝트 진입
    cJSON *state = cJSON_GetObjectItem(root, "state");
    if (state == NULL || !cJSON_IsObject(state)) {
        ESP_LOGE(TAG, "state 오브젝트 누락");
        cJSON_Delete(root);
        return false;
    }

    // 4. 필드별 파싱 (키가 존재하면 무조건 valid=1 세팅)

    // filter_alert_enabled (bool)
    cJSON *item = cJSON_GetObjectItem(state, "filter_alert_enabled");
    if (cJSON_IsBool(item)) {
        bool val = cJSON_IsTrue(item);
        out_info->shadow_packet_info.filter_alert_enabled = val;     
        if(out_info->shadow_packet_info.filter_alert_enabled != shadow_packet_info.filter_alert_enabled)
        {
            out_info->valid.has_filter_alert_enabled = 1;
        }
        shadow_packet_info.filter_alert_enabled = val;
    }

    // sleep_mode (int)
    item = cJSON_GetObjectItem(state, "sleep_mode");
    if (cJSON_IsNumber(item)) {
        int val = item->valueint;
        out_info->shadow_packet_info.sleep_mode = val;     
        if(out_info->shadow_packet_info.sleep_mode != shadow_packet_info.sleep_mode)
        {
            out_info->valid.has_sleep_mode = 1;
        }
        shadow_packet_info.sleep_mode = val;
    }

    // current_mode (int)
    item = cJSON_GetObjectItem(state, "current_mode");
    if (cJSON_IsNumber(item)) {
        int val = item->valueint;
        out_info->shadow_packet_info.current_mode = val;     
        if(out_info->shadow_packet_info.current_mode != shadow_packet_info.current_mode)
        {
            out_info->valid.has_current_mode = 1;
        }
        shadow_packet_info.current_mode = val;
    }

    // tare_request (UUID string)
    item = cJSON_GetObjectItem(state, "tare_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        // UUID는 이전과 다를 때만 액션을 수행해야 하므로 비교 유지
        if (strncmp(shadow_packet_info.tare_request, item->valuestring, sizeof(shadow_packet_info.tare_request)) != 0) {
            snprintf(shadow_packet_info.tare_request, sizeof(shadow_packet_info.tare_request), "%s", item->valuestring);
            out_info->valid.has_tare_request = 1; // 새로운 요청일 때만 1
        }
        snprintf(out_info->shadow_packet_info.tare_request, sizeof(out_info->shadow_packet_info.tare_request), "%s", item->valuestring);
    }

    // water_filter_reset_request (UUID string)
    item = cJSON_GetObjectItem(state, "water_filter_reset_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.water_filter_reset_request, item->valuestring, sizeof(shadow_packet_info.water_filter_reset_request)) != 0) {
            snprintf(shadow_packet_info.water_filter_reset_request, sizeof(shadow_packet_info.water_filter_reset_request), "%s", item->valuestring);
            out_info->valid.has_water_filter_reset = 1;
        }
        snprintf(out_info->shadow_packet_info.water_filter_reset_request, sizeof(out_info->shadow_packet_info.water_filter_reset_request), "%s", item->valuestring);
    }

    // debris_filter_reset_request (UUID string)
    item = cJSON_GetObjectItem(state, "debris_filter_reset_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.debris_filter_reset_request, item->valuestring, sizeof(shadow_packet_info.debris_filter_reset_request)) != 0) {
            snprintf(shadow_packet_info.debris_filter_reset_request, sizeof(shadow_packet_info.debris_filter_reset_request), "%s", item->valuestring);
            out_info->valid.has_debris_filter_reset = 1;
        }
        snprintf(out_info->shadow_packet_info.debris_filter_reset_request, sizeof(out_info->shadow_packet_info.debris_filter_reset_request), "%s", item->valuestring);
    }

    // reboot_request (UUID string)
    item = cJSON_GetObjectItem(state, "reboot_request");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        if (strncmp(shadow_packet_info.reboot_request, item->valuestring, sizeof(shadow_packet_info.reboot_request)) != 0) {
            snprintf(shadow_packet_info.reboot_request, sizeof(shadow_packet_info.reboot_request), "%s", item->valuestring);
            out_info->valid.has_reboot_request = 1;
        }
        snprintf(out_info->shadow_packet_info.reboot_request, sizeof(out_info->shadow_packet_info.reboot_request), "%s", item->valuestring);
    }

    cJSON_Delete(root);
    return true;
}
/* ========================================================== */
/* 수신 메시지 처리 메인 함수                                  */
/* ========================================================== */
void set_reset_flag(void);
void HX711_cal_init(uint16_t cal);
void shadow_mqtt_update_delta_rx_messege(MQTTPublishInfo_t * pPublishInfo)
{
    shadow_state_info_t shadow_data;

    char* Payload = calloc(1,pPublishInfo->payloadLength + 1);
    int payload_len = pPublishInfo->payloadLength;

    if(Payload == NULL)
        return;
    memcpy(Payload,pPublishInfo->pPayload,  pPublishInfo->payloadLength);
    Payload[pPublishInfo->payloadLength] = 0;

    // int 캐스팅으로 길이를 넘겨주고 버퍼 포인터 전달
    ESP_LOGI(TAG, "Payload Length: %d, Data: %.*s", 
            (int)payload_len, 
            (int)payload_len, 
            (const char *)Payload);

    if (!parse_shadow_state((const char *)Payload, payload_len, &shadow_data)) {
        ESP_LOGE(TAG, "Shadow Payload 파싱 실패");
        return;
    }

    // 값이 실제로 변경된 항목만 들어옴
    if (shadow_data.valid.has_filter_alert_enabled) {
        ESP_LOGI(TAG, "새로운 Filter Alert 반영: %s", 
                 shadow_data.shadow_packet_info.filter_alert_enabled ? "true" : "false");
        set_filter_alert_enabled(shadow_packet_info.filter_alert_enabled?true:false);
        shadow_mqtt_send(FILTER_ALERT_ENABLE);        
    }

    if (shadow_data.valid.has_sleep_mode) {
        ESP_LOGI(TAG, "새로운 Sleep Mode 반영: %d", shadow_data.shadow_packet_info.sleep_mode);
        Night_Mode(shadow_packet_info.sleep_mode);
        shadow_mqtt_send(SLEEP_MODE);        
    }

    if (shadow_data.valid.has_current_mode) {
        ESP_LOGI(TAG, "새로운 Current Mode 반영: %d", shadow_data.shadow_packet_info.current_mode);
        shadow_packet_info.current_mode = shadow_data.shadow_packet_info.current_mode;
        Opmode_Set((uint32_t*)&shadow_packet_info.current_mode);        
        shadow_mqtt_send(CURRUNT_MODE);
        // shadow_send_reported("...");
    }

    if (shadow_data.valid.has_debris_filter_reset) {
        ESP_LOGI(TAG, "새로운 이물질 필터 리셋 요청 처리: %s", shadow_data.shadow_packet_info.debris_filter_reset_request);
        motor_change();
        shadow_mqtt_send(DEBRIS_FILTER_RESET);      
    }

    if (shadow_data.valid.has_water_filter_reset) {
        ESP_LOGI(TAG, "새로운 정수 필터 리셋 요청 처리: %s", shadow_data.shadow_packet_info.water_filter_reset_request);
        filter_change();
        shadow_mqtt_send(WATER_FILETER_RESET);                      
    }

    if (shadow_data.valid.has_tare_request) {
        ESP_LOGI(TAG, "새로운 영점 조절 요청 처리: %s", shadow_data.shadow_packet_info.tare_request);
        // shadow_send_reported("...");
        HX711_cal_init(1);
           
    }

    if (shadow_data.valid.has_reboot_request) {
        ESP_LOGI(TAG, "새로운 재부팅 요청 처리: %s", shadow_data.shadow_packet_info.reboot_request);
        set_reset_flag();
        shadow_mqtt_send(REBOOT_REQUEST);  
    }
}


//펌웨어 파일 전송 




