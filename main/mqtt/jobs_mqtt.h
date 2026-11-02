#ifndef __JOBS_MQTT_H__
#define __JOBS_MQTT_H__
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_operations.h"



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

typedef struct {
    char jobId[64];
    char version[32];
    char sha256[65];        // 64자 hex + '\0'
    int size_bytes;
    int min_rssi_dbm;
    char *ota_url;          // URL은 1,000자 이상이므로 동적 할당(malloc) 권장
} fota_job_info_t;

typedef struct{
    char status[20];
    char step[20];
    char error[20];
    char jobId[64];
    uint32_t progress;
}JOBS_Update_Packet_t;

void jobs_mqtt_rx_message(MQTTPublishInfo_t * pPublishInfo);
void Jobs_mqtt_send(jobs_error_e state);

#endif