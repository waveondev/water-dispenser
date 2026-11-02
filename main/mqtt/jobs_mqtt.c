#include "jobs_mqtt.h"

#define TAG __FILE__


#include "cJSON.h"
#include "http_ota.h"
#include "tx_mqtt.h"
#include "aws_iot_task.h"
#include "esp_wifi.h"
fota_job_info_t job_info;

void fota_job_info_free(fota_job_info_t *info) {
    if (info && info->ota_url) {
        free(info->ota_url);
        info->ota_url = NULL;
    }
}

#if 0

typedef enum{
    IN_PROGRESS_DOWNLOAD,
    IN_PROGRESS_INSTALLING,
    SUCCESSED_COMPLETED,
    FAILED_LOW_RSSI,
    FAILED_DOWNLOAD_FAILED,
    FAILED_CHECKSUM_MISMATCH,
    FAILED_FLASH_WRITE,
    FAILED_SELP_TEST,
}jobs_error_e;

#endif
void Jobs_mqtt_send(jobs_error_e state)
{
    JOBS_Update_Packet_t JOBS_Update_Packet;
    memset(&JOBS_Update_Packet, 0, sizeof(JOBS_Update_Packet_t));

    // 공통 필드: 현재 진행 중인 Job ID 복사
    memcpy(JOBS_Update_Packet.jobId, job_info.jobId, sizeof(JOBS_Update_Packet.jobId) - 1);

    switch(state)
    {
    /* ========================================================== */
    /* 1. 진행 중 상태 (IN_PROGRESS)                               */
    /* ========================================================== */
    case IN_PROGRESS_DOWNLOAD:
        // 다운로드 시작 단계
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "IN_PROGRESS");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "downloading");
        break;

    case IN_PROGRESS_INSTALLING:
        // 다운로드 완료 후 플래시 검증 및 설치/부팅 설정 단계
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "IN_PROGRESS");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "installing");
        break;

    /* ========================================================== */
    /* 2. 성공 상태 (SUCCEEDED)                                   */
    /* ========================================================== */
    case SUCCESSED_COMPLETED:
        // 새 펌웨어로 정상 부팅 후 롤백 취소/자가진단 통과 시 전송
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "SUCCEEDED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "completed");
        break;

    /* ========================================================== */
    /* 3. 실패 상태 (FAILED)                                      */
    /* ========================================================== */
    case FAILED_LOW_RSSI:
        // 신호 감도 미달로 OTA 시작 전단 취소
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "FAILED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "pre_check");
        snprintf(JOBS_Update_Packet.error, sizeof(JOBS_Update_Packet.error), "LOW_RSSI");
        break;

    case FAILED_DOWNLOAD_FAILED:
        // HTTP 통신 단절, 타임아웃, S3 Presigned URL 만료, 메모리 부족
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "FAILED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "downloading");
        snprintf(JOBS_Update_Packet.error, sizeof(JOBS_Update_Packet.error), "DOWNLOAD_FAILED");
        break;

    case FAILED_CHECKSUM_MISMATCH:
        // 바이너리 깨짐, SHA256 체크섬 불일치, 헤더 검증(OTA_VALIDATE) 실패
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "FAILED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "verification");
        snprintf(JOBS_Update_Packet.error, sizeof(JOBS_Update_Packet.error), "CHECKSUM_MISMATCH");
        break;

    case FAILED_FLASH_WRITE:
        // 플래시 Erase/Write 에러, otadata 갱신 실패, 파티션 용량 부족
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "FAILED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "flashing");
        snprintf(JOBS_Update_Packet.error, sizeof(JOBS_Update_Packet.error), "FLASH_WRITE_ERROR");
        break;

    case FAILED_SELP_TEST:
        // 새 펌웨어로 부팅 후 센서/주변장치 자가진단 실패 (롤백 발생)
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "FAILED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "self_test");
        snprintf(JOBS_Update_Packet.error, sizeof(JOBS_Update_Packet.error), "SELF_TEST_FAILED");
        break;

    default:
        snprintf(JOBS_Update_Packet.status, sizeof(JOBS_Update_Packet.status), "FAILED");
        snprintf(JOBS_Update_Packet.step, sizeof(JOBS_Update_Packet.step), "unknown");
        snprintf(JOBS_Update_Packet.error, sizeof(JOBS_Update_Packet.error), "UNKNOWN_ERROR");
        break;
    }
    mqtt_queue_send(AWS_MESSEGE_JOBS_UPDATE,&JOBS_Update_Packet,sizeof(JOBS_Update_Packet_t));           
}

bool parse_fota_job_cjson(const char *payload, size_t len, fota_job_info_t *out_info) {
    if (!payload || !out_info) return false;
    memset(out_info, 0, sizeof(fota_job_info_t));

    // payload가 null-terminated가 아닐 수 있으므로 임시 버퍼 생성 필요 시 확보
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) return false;

    bool success = false;
    cJSON *execution = cJSON_GetObjectItem(root, "execution");
    if (!execution) goto cleanup;

    // 1. Job ID
    cJSON *jobId = cJSON_GetObjectItem(execution, "jobId");
    if (cJSON_IsString(jobId) && jobId->valuestring) {
        strncpy(out_info->jobId, jobId->valuestring, sizeof(out_info->jobId) - 1);
    }

    // 2. jobDocument 내부 진입
    cJSON *jobDoc = cJSON_GetObjectItem(execution, "jobDocument");
    if (!jobDoc) goto cleanup;

    // 3. S3 Presigned URL 추출 (길이가 1KB 이상이므로 동적 할당)
    cJSON *url = cJSON_GetObjectItem(jobDoc, "url");
    if (cJSON_IsString(url) && url->valuestring) {
        out_info->ota_url = strdup(url->valuestring);
    }

    // 4. 기타 필수 메타데이터
    cJSON *version = cJSON_GetObjectItem(jobDoc, "version");
    if (cJSON_IsString(version) && version->valuestring) {
        strncpy(out_info->version, version->valuestring, sizeof(out_info->version) - 1);
    }

    cJSON *sha256 = cJSON_GetObjectItem(jobDoc, "sha256");
    if (cJSON_IsString(sha256) && sha256->valuestring) {
        strncpy(out_info->sha256, sha256->valuestring, sizeof(out_info->sha256) - 1);
    }

    cJSON *size = cJSON_GetObjectItem(jobDoc, "size_bytes");
    if (cJSON_IsNumber(size)) {
        out_info->size_bytes = size->valueint;
    }

    cJSON *min_rssi = cJSON_GetObjectItem(jobDoc, "min_rssi_dbm");
    if (cJSON_IsNumber(min_rssi)) {
        out_info->min_rssi_dbm = min_rssi->valueint;
    }

    success = (out_info->ota_url != NULL);

cleanup:
    cJSON_Delete(root);
    if (!success) {
        fota_job_info_free(out_info);
    }
    return success;
}

void jobs_mqtt_rx_message(MQTTPublishInfo_t * pPublishInfo)
{
    wifi_ap_record_t ap_info;

    char* Payload = calloc(1,pPublishInfo->payloadLength + 1);
    int payload_len = pPublishInfo->payloadLength;

    if(Payload == NULL)
        return;
    memcpy(Payload,pPublishInfo->pPayload,  pPublishInfo->payloadLength);
    Payload[pPublishInfo->payloadLength] = 0;

    if ( Payload != NULL && payload_len > 0 )
    {
        ESP_LOGI(TAG, "jobs_mqtt_rx_message = %s %d",Payload,  payload_len); // 예: -55 dBm
        if (parse_fota_job_cjson((const char *)Payload, payload_len, &job_info)) {
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) 
            {
                ESP_LOGI(TAG, "SSID: %s", ap_info.ssid);
                ESP_LOGI(TAG, "RSSI: %d dBm", ap_info.rssi); 
            } 
            #if 1
            if(ap_info.rssi < -80)
            {
                fota_job_info_free(&job_info);    
                Jobs_mqtt_send(FAILED_LOW_RSSI);         
                return;
            }
            #endif
            
            ESP_LOGI(TAG,"[FOTA] Job ID: %s", job_info.jobId);
            ESP_LOGI(TAG,"[FOTA] Version: %s", job_info.version);
            ESP_LOGI(TAG,"[FOTA] Binary Size: %d bytes", job_info.size_bytes);
            ESP_LOGI(TAG,"[FOTA] URL 파싱 완료 (길이: %d)", (int)strlen(job_info.ota_url));
            ota_main(job_info.ota_url);

            fota_job_info_free(&job_info);
        }
    }
    free(Payload);
}









