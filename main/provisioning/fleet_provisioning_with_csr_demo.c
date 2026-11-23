/*
 * AWS IoT Device SDK for Embedded C 202211.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Demo for showing use of the Fleet Provisioning library to use the Fleet
 * Provisioning feature of AWS IoT Core for provisioning devices with
 * credentials. This demo shows how a device can be provisioned with AWS IoT
 * Core using the Certificate Signing Request workflow of the Fleet
 * Provisioning feature.
 *
 * The Fleet Provisioning library provides macros and helper functions for
 * assembling MQTT topics strings, and for determining whether an incoming MQTT
 * message is related to the Fleet Provisioning API of AWS IoT Core. The Fleet
 * Provisioning library does not depend on any particular MQTT library,
 * therefore the functionality for MQTT operations is placed in another file
 * (mqtt_operations.c). This demo uses the coreMQTT library. If needed,
 * mqtt_operations.c can be modified to replace coreMQTT with another MQTT
 * library. This demo requires using the AWS IoT Core broker as Fleet
 * Provisioning is an AWS IoT Core feature.
 *
 * This demo provisions a device certificate using the provisioning by claim
 * workflow with a Certificate Signing Request (CSR). The demo connects to AWS
 * IoT Core using provided claim credentials (whose certificate needs to be
 * registered with IoT Core before running this demo), subscribes to the
 * CreateCertificateFromCsr topics, and obtains a certificate. It then
 * subscribes to the RegisterThing topics and activates the certificate and
 * obtains a Thing using the provisioning template. Finally, it reconnects to
 * AWS IoT Core using the new credentials.
 */

/* Standard includes. */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* POSIX includes. */
#include <unistd.h>
#include <errno.h>

/* aws iot config */
#include "aws_iot_config.h"

/* corePKCS11 includes. */
#include "core_pkcs11.h"
#include "core_pkcs11_config.h"

/* AWS IoT Fleet Provisioning Library. */
#include "fleet_provisioning.h"

/* Demo includes. */
#include "mqtt_operations.h"
#include "pkcs11_operations.h"
#include "fleet_provisioning_serializer.h"

#include "cJSON.h"

#include "nvs_flash.h"  // [NVS 추가] 플래시 메모리 헤더
#include "nvs.h"        // [NVS 추가] 플래시 메모리 헤더
#include "esp_mac.h"
#include "rx_mqtt.h"
#include "aws_iot_task.h"
#include "app_nvs.h"
#include "device_config.h"
/**
 * These configurations are required. Throw compilation error if it is not
 * defined.
 */
#ifndef PROVISIONING_TEMPLATE_NAME
    #error "Please define PROVISIONING_TEMPLATE_NAME to the template name registered with AWS IoT Core in demo_config.h."
#endif
#ifndef CLAIM_CERT_PATH
    #error "Please define path to claim certificate (CLAIM_CERT_PATH) in demo_config.h."
#endif
#ifndef CLAIM_PRIVATE_KEY_PATH
    #error "Please define path to claim private key (CLAIM_PRIVATE_KEY_PATH) in demo_config.h."
#endif
#ifndef DEVICE_SERIAL_NUMBER
    #error "Please define a serial number (DEVICE_SERIAL_NUMBER) in demo_config.h."
#endif

/**
 * @brief The length of #PROVISIONING_TEMPLATE_NAME.
 */
#define PROVISIONING_TEMPLATE_NAME_LENGTH    ( ( uint16_t ) ( sizeof( PROVISIONING_TEMPLATE_NAME ) - 1 ) )

/**
 * @brief The length of #DEVICE_SERIAL_NUMBER.
 */
#define DEVICE_SERIAL_NUMBER_LENGTH          ( ( uint16_t ) ( sizeof( DEVICE_SERIAL_NUMBER ) - 1 ) )

/**
 * @brief Size of AWS IoT Thing name buffer.
 *
 * See https://docs.aws.amazon.com/iot/latest/apireference/API_CreateThing.html#iot-CreateThing-request-thingName
 */
#define MAX_THING_NAME_LENGTH                128

/**
 * @brief The maximum number of times to run the loop in this demo.
 *
 * @note The demo loop is attempted to re-run only if it fails in an iteration.
 * Once the demo loop succeeds in an iteration, the demo exits successfully.
 */
#ifndef FLEET_PROV_MAX_DEMO_LOOP_COUNT
    #define FLEET_PROV_MAX_DEMO_LOOP_COUNT    ( 3 )
#endif

/**
 * @brief Time in seconds to wait between retries of the demo loop if
 * demo loop fails.
 */
#define DELAY_BETWEEN_DEMO_RETRY_ITERATIONS_SECONDS    ( 5 )

/**
 * @brief Size of buffer in which to hold the certificate signing request (CSR).
 */
#define CSR_BUFFER_LENGTH                              2048

/**
 * @brief Size of buffer in which to hold the certificate.
 */
#define CERT_BUFFER_LENGTH                             2048

/**
 * @brief Size of buffer in which to hold the certificate id.
 *
 * See https://docs.aws.amazon.com/iot/latest/apireference/API_Certificate.html#iot-Type-Certificate-certificateId
 */
#define CERT_ID_BUFFER_LENGTH                          64

/**
 * @brief Size of buffer in which to hold the certificate ownership token.
 */
#define OWNERSHIP_TOKEN_BUFFER_LENGTH                  512

/**
 * @brief Status values of the Fleet Provisioning response.
 */
typedef enum
{
    ResponseNotReceived,
    ResponseAccepted,
    ResponseRejected
} ResponseStatus_t;

/*-----------------------------------------------------------*/

/**
 * @brief Status reported from the MQTT publish callback.
 */
static ResponseStatus_t responseStatus;

/**
 * @brief Buffer to hold the provisioned AWS IoT Thing name.
 */
static char thingName[ MAX_THING_NAME_LENGTH ];

/**
 * @brief Length of the AWS IoT Thing name.
 */
static size_t thingNameLength;

/**
 * @brief Buffer to hold responses received from the AWS IoT Fleet Provisioning
 * APIs. When the MQTT publish callback receives an expected Fleet Provisioning
 * accepted payload, it copies it into this buffer.
 */

static uint8_t *payloadBuffer;
/**
 * @brief Length of the payload stored in #payloadBuffer. This is set by the
 * MQTT publish callback when it copies a received payload into #payloadBuffer.
 */
static size_t payloadLength;

/*-----------------------------------------------------------*/

/**
 * @brief Callback to receive the incoming publish messages from the MQTT
 * broker. Sets responseStatus if an expected CreateCertificateFromCsr or
 * RegisterThing response is received, and copies the response into
 * responseBuffer if the response is an accepted one.
 *
 * @param[in] pPublishInfo Pointer to publish info of the incoming publish.
 * @param[in] packetIdentifier Packet identifier of the incoming publish.
 */
static void provisioningPublishCallback( MQTTPublishInfo_t * pPublishInfo,
                                         uint16_t packetIdentifier );

/**
 * @brief Run the MQTT process loop to get a response.
 */
static bool waitForResponse( void );

/**
 * @brief Subscribe to the CreateCertificateFromCsr accepted and rejected topics.
 */
static bool subscribeToCsrResponseTopics( void );

/**
 * @brief Unsubscribe from the CreateCertificateFromCsr accepted and rejected topics.
 */
static bool unsubscribeFromCsrResponseTopics( void );

/**
 * @brief Subscribe to the RegisterThing accepted and rejected topics.
 */
static bool subscribeToRegisterThingResponseTopics( void );

/**
 * @brief Unsubscribe from the RegisterThing accepted and rejected topics.
 */
static bool unsubscribeFromRegisterThingResponseTopics( void );

/*-----------------------------------------------------------*/




#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>


bool parse_custom_thing_topic(const char *topic, uint16_t topic_len,
                              char *out_dev_type, size_t dev_type_len,
                              char *out_prefix, size_t prefix_len,
                              char out_mac_str[13],
                              uint8_t out_mac_bytes[6],
                              const char **out_event_type,
                              int *out_event_len)
{
    const char root_tag[] = "things/";
    size_t root_len = sizeof(root_tag) - 1;

    // 1. "things/" 접두사 검증
    if (topic == NULL || topic_len <= root_len || strncmp(topic, root_tag, root_len) != 0) {
        return false;
    }

    const char *p = topic + root_len;
    uint16_t remain_len = topic_len - root_len;


    const char *slash1 = memchr(p, '/', remain_len);
    if (!slash1) return false;

    size_t cur_type_len = slash1 - p;
    if (cur_type_len == 0 || cur_type_len >= dev_type_len) return false;

    if (out_dev_type != NULL) {
        memcpy(out_dev_type, p, cur_type_len);
        out_dev_type[cur_type_len] = '\0';
    }

    p = slash1 + 1;
    remain_len -= (cur_type_len + 1);


    const char *underscore = memchr(p, '_', remain_len);
    if (!underscore) return false;

    size_t cur_prefix_len = underscore - p;
    if (cur_prefix_len == 0 || cur_prefix_len >= prefix_len) return false;

    if (out_prefix != NULL) {
        memcpy(out_prefix, p, cur_prefix_len);
        out_prefix[cur_prefix_len] = '\0';
    }

    p = underscore + 1;
    remain_len -= (cur_prefix_len + 1);

    // 4. MAC 주소 문자열 12자리 확인 및 파싱 ("7CE8B1762BE8")
    if (remain_len < 12) return false;

    if (out_mac_str != NULL) {
        memcpy(out_mac_str, p, 12);
        out_mac_str[12] = '\0';
    }

    if (out_mac_bytes != NULL) {
        for (int i = 0; i < 6; i++) {
            unsigned int byte_val = 0;
            if (sscanf(p + (i * 2), "%2x", &byte_val) != 1) {
                return false;
            }
            out_mac_bytes[i] = (uint8_t)byte_val;
        }
    }

    p += 12;
    remain_len -= 12;

    // 5. event_type 추출 (MAC 뒤의 '/' 확인)
    if (remain_len > 0 && *p == '/') {
        if (out_event_type != NULL) {
            *out_event_type = p + 1; // "boot", "access" 등의 시작 포인터
        }
        if (out_event_len != NULL) {
            *out_event_len = remain_len - 1;
        }
    } else {
        if (out_event_type != NULL) *out_event_type = NULL;
        if (out_event_len != NULL) *out_event_len = 0;
        return false;
    }

    return true;
}


int parse_aws_topic(const char *topic, uint16_t topic_len,
                     char *out_prefix, size_t prefix_len,
                     uint8_t out_mac[6],
                     const char **out_subtopic)
{
    const char prefix_tag[] = "$aws/things/";
    size_t tag_len = sizeof(prefix_tag) - 1;

    // 1. $aws/things/ 접두사 확인
    if (topic_len <= tag_len || strncmp(topic, prefix_tag, tag_len) != 0) {
        return -1;
    }

    const char *p = topic + tag_len;
    uint16_t remain_len = topic_len - tag_len;


    const char *underscore = memchr(p, '_', remain_len);
    if (!underscore) return -2;

    size_t cur_prefix_len = underscore - p;
    if (cur_prefix_len >= prefix_len) return -3; // 버퍼 오버플로우 방지

    memcpy(out_prefix, p, cur_prefix_len);
    out_prefix[cur_prefix_len] = '\0'; 

    // 3. MAC 주소 문자열 (12자리 HEX: "7CE8B1762BE8") 파싱
    const char *mac_str = underscore + 1;
    remain_len -= (cur_prefix_len + 1);

    // 최소 12자리 MAC 문자열 확인
    if (remain_len < 12) return -4;

    for (int i = 0; i < 6; i++) {
        unsigned int byte_val = 0;
        // 2글자씩 16진수 파싱
        if (sscanf(mac_str + (i * 2), "%2x", &byte_val) != 1) {
            return -5;
        }
        out_mac[i] = (uint8_t)byte_val; // {0x7C, 0xE8, 0xB1, 0x76, 0x2B, 0xE8}
    }

    // 4. MAC 주소 뒤 '/' 탐색 후 서브토픽 시작 포인터 지정
    const char *after_mac = mac_str + 12;
    remain_len -= 12;

    if (remain_len > 0 && *after_mac == '/') {
        *out_subtopic = after_mac + 1; // "shadow/update/accepted..." 시작 위치
    } else {
        *out_subtopic = NULL;
    }

    return 0;
}
/*-----------------------------------------------------------*/
#include "rx_mqtt.h"
#include "shadow_mqtt.h"
#include "jobs_mqtt.h"
#define TAG "RX_MQTT"
static void provisioningPublishCallback( MQTTPublishInfo_t * pPublishInfo,
                                         uint16_t packetIdentifier )
{
    FleetProvisioningStatus_t status;
    FleetProvisioningTopic_t api;
    const char * cborDump;
    /* ========================================================== */
    /* 토픽 분리 및 파싱 적용                                    */
    /* ========================================================== */
    char dev_model[16] = {0};       
    uint8_t dev_mac[6] = {0};      // 6바이트 바이너리 MAC {0x7C, 0xE8, ...}
    const char *sub_topic = NULL;  // 서브토픽 시작 포인터
    /* Silence compiler warnings about unused variables. */

    status = FleetProvisioning_MatchTopic( pPublishInfo->pTopicName,
                                           pPublishInfo->topicNameLength, &api );

    if( status != FleetProvisioningSuccess )
    {
        ESP_LOGI(TAG, "[수신 테스트] 서버로부터 메시지가 도착했습니다!" );
        ESP_LOGI(TAG, "토픽: %.*s", ( int ) pPublishInfo->topicNameLength, pPublishInfo->pTopicName ) ;
        ESP_LOGI(TAG, "내용: %.*s", ( int ) pPublishInfo->payloadLength, ( const char * ) pPublishInfo->pPayload );
    
        int aws_shadow = parse_aws_topic(pPublishInfo->pTopicName, pPublishInfo->topicNameLength,
                                dev_model, sizeof(dev_model),
                                dev_mac,
                                &sub_topic);
        if(aws_shadow != 0)
        {
            ESP_LOGI(TAG, "aws_shadow error = %d", aws_shadow);
        }
        if (aws_shadow == 0)
        {
            // sub_topic의 유효 길이 계산 (null 종단 문자가 없을 수 있으므로)
            int sub_len = (int)(pPublishInfo->topicNameLength - (sub_topic - pPublishInfo->pTopicName));

            if (strcmp(dev_model, CONFIG_DEVICE_PREFIX) != 0)
            {
                ESP_LOGI(TAG,  "타겟 모델 불일치 무시 (수신: %s, 본 기기: %s)", dev_model, CONFIG_DEVICE_PREFIX );
                return;
            }

            /* ------------------------------------------------------ */
            /* 2. 기기 실제 MAC 일치 여부 확인 (ESP 내부 칩 MAC 조회)   */
            /* ------------------------------------------------------ */
            uint8_t my_chip_mac[6] = {0};
            // Wi-Fi Station 모드의 기본 eFuse MAC 주소 획득
            if (esp_read_mac(my_chip_mac, ESP_MAC_WIFI_STA) != ESP_OK)
            {
                ESP_LOGI(TAG,  "내부 MAC 주소 읽기 실패"  );
                return;
            }
            if (memcmp(dev_mac, my_chip_mac, sizeof(dev_mac)) != 0)
            {
                ESP_LOGI(TAG,   "타겟 MAC 불일치 무시 (수신: %02X:%02X:%02X:%02X:%02X:%02X, 내 MAC: %02X:%02X:%02X:%02X:%02X:%02X)",
                            dev_mac[0], dev_mac[1], dev_mac[2], dev_mac[3], dev_mac[4], dev_mac[5],
                            my_chip_mac[0], my_chip_mac[1], my_chip_mac[2], my_chip_mac[3], my_chip_mac[4], my_chip_mac[5]  );
                return;
            }
            ESP_LOGI(TAG,  " 모델: %s", dev_model  );
            ESP_LOGI(TAG,  " MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                    dev_mac[0], dev_mac[1], dev_mac[2], dev_mac[3], dev_mac[4], dev_mac[5] );
            ESP_LOGI(TAG,  " 서브토픽: %.*s", sub_len, sub_topic  );

            /* ------------------------------------------------------ */
            /* 서브토픽 분기: Jobs / Shadow                            */
            /* ------------------------------------------------------ */
            if (sub_topic != NULL && sub_len >= 5 && strncmp(sub_topic, "jobs/", 5) == 0)
            {
                const char *job_sub = sub_topic + 5; // "jobs/" 건너뜀
                int job_sub_len = sub_len - 5;

                // 1. 새 Job 통지 토픽인 경우 (실제 FOTA 작업 착수)
                if ((job_sub_len >= 11 && strncmp(job_sub, "notify-next", 11) == 0) ||
                    (job_sub_len >= 6  && strncmp(job_sub, "notify", 6) == 0) || 
                    (job_sub_len >= 18  && strncmp(job_sub, "$next/get/accepted", 18) == 0))
                {
                    ESP_LOGI(TAG,  " [분기] AWS Jobs 새 작업 수신 (notify)"  );
                    jobs_mqtt_rx_message(pPublishInfo);
                }
                // 2. 디바이스가 보낸 update에 대한 AWS의 승인 응답 (.../update/accepted)
                else if (strstr(job_sub, "/update/accepted") != NULL)
                {
                    ESP_LOGI(TAG,  " [분기] AWS Jobs Update 성공 확인 (accepted)"  );
                    // 여기서는 추가 다운로드를 진행하지 않고 로그만 찍거나 ACK 플래그만 세팅
                }
                // 3. 디바이스가 보낸 update에 대한 AWS의 거절 응답 (.../update/rejected)
                else if (strstr(job_sub, "/update/rejected") != NULL)
                {
                    ESP_LOGI(TAG,  " [분기] AWS Jobs Update 거절됨 (rejected)" );
                }
                else
                {
                    ESP_LOGI(TAG,  " [분기] 기타 Jobs 토픽: %.*s", job_sub_len, job_sub  );
                }
            }
            else if (sub_topic != NULL && sub_len >= 7 && strncmp(sub_topic, "shadow/", 7) == 0)
            {
                const char *shadow_sub = sub_topic + 7; // "shadow/" 건너뜀 (예: "update/...", "get/...")
                int shadow_sub_len = sub_len - 7;

                /* ====================================================== */
                /* 1. Shadow Update 계열 ("update/...")                   */
                /* ====================================================== */
                if (shadow_sub_len >= 7 && strncmp(shadow_sub, "update/", 7) == 0)
                {
                    const char *action = shadow_sub + 7; // "update/" 건너뜀
                    int action_len = shadow_sub_len - 7;

                    // 1-1. update/delta (클라우드에서 변경된 desired 수신)
                    if (action_len >= 5 && strncmp(action, "delta", 5) == 0)
                    {
                        ESP_LOGI(TAG,  " [분기] AWS Shadow Update -> Delta 수신 (desired 변경사항 반영)"  );
                        shadow_mqtt_update_delta_rx_messege(pPublishInfo);
                    }
                    // 1-2. update/accepted (디바이스의 reported 보고 성공 응답)
                    else if (action_len >= 8 && strncmp(action, "accepted", 8) == 0)
                    {
                        ESP_LOGI(TAG,  " [분기] AWS Shadow Update -> Accepted 수신 (reported 반영 완료)"  );
                        // TODO: ACK 플래그 처리 또는 응답 타임아웃 해제
                    }
                    // 1-3. update/rejected (reported 보고 실패/거절)
                    else if (action_len >= 8 && strncmp(action, "rejected", 8) == 0)
                    {
                        ESP_LOGI(TAG,  " [분기] AWS Shadow Update -> Rejected 수신 (보고 거절됨)" ) ;
                    }
                    else
                    {
                        ESP_LOGI(TAG,  " [분기] 기타 Shadow Update 토픽: %.*s", action_len, action  );
                    }
                }
                /* ====================================================== */
                /* 2. Shadow Get 계열 ("get/...")                         */
                /* ====================================================== */
                else if (shadow_sub_len >= 4 && strncmp(shadow_sub, "get/", 4) == 0)
                {
                    const char *action = shadow_sub + 4; // "get/" 건너뜀
                    int action_len = shadow_sub_len - 4;

                    // 2-1. get/accepted (부팅 직후 Shadow 전체 문서 수신)
                    if (action_len >= 8 && strncmp(action, "accepted", 8) == 0)
                    {
                        ESP_LOGI(TAG,  " [분기] AWS Shadow Get -> Accepted 수신 (전체 문서 동기화)"  );
                        shadow_mqtt_get_accepted_rx_messege(pPublishInfo);
                    }
                    // 2-2. get/rejected (문서가 없거나 권한 없음 등으로 거절)
                    else if (action_len >= 8 && strncmp(action, "rejected", 8) == 0)
                    {
                        ESP_LOGI(TAG, " [분기] AWS Shadow Get -> Rejected 수신 (조회 실패)"  );
                    }
                    else
                    {
                        ESP_LOGI(TAG,  " [분기] 기타 Shadow Get 토픽: %.*s", action_len, action  );
                    }
                }
                /* ====================================================== */
                /* 3. 기타 Shadow 토픽                                    */
                /* ====================================================== */
                else
                {
                    ESP_LOGI(TAG,  " [분기] 미처리 Shadow 토픽: %.*s", shadow_sub_len, shadow_sub  );
                }
            }          
            else
            {
                ESP_LOGI(TAG,  " [기타 Thing 토픽]: %.*s", sub_len, sub_topic  );
                mqtt_rx_messege(pPublishInfo);
            }
        }
        else
        {

            char custom_dev_type[8] = {0};   
            char custom_prefix[8] = {0};     
            char custom_mac_str[13] = {0};   // "7CE8B1762BE8"
            uint8_t custom_mac[6] = {0};     // {0x7C, 0xE8, ...}
            const char *event_type = NULL;   // "boot" 시작 포인터
            int event_type_len = 0;

            bool is_custom_thing = parse_custom_thing_topic(
                pPublishInfo->pTopicName, pPublishInfo->topicNameLength,
                custom_dev_type, sizeof(custom_dev_type),
                custom_prefix, sizeof(custom_prefix),
                custom_mac_str,
                custom_mac,
                &event_type,
                &event_type_len
            );

            if (is_custom_thing == true)
            {
                LogInfo( ( "--- [Custom Thing Topic 파싱 성공] ---" ) );
                LogInfo( ( " Device Type : %s", custom_dev_type ) );
                LogInfo( ( " Prefix      : %s", custom_prefix ) );
                LogInfo( ( " MAC String  : %s", custom_mac_str ) );
                LogInfo( ( " MAC Bytes   : %02X:%02X:%02X:%02X:%02X:%02X",
                            custom_mac[0], custom_mac[1], custom_mac[2],
                            custom_mac[3], custom_mac[4], custom_mac[5] ) );
                LogInfo( ( " Event Type  : %.*s", event_type_len, event_type ) );

                // 1. 모델/프리픽스 일치 확인
                if (strcmp(custom_prefix, CONFIG_DEVICE_PREFIX) != 0) {
                    LogWarn( ( "타겟 모델 불일치 무시 (수신: %s, 본 기기: %s)", custom_prefix, CONFIG_DEVICE_PREFIX ) );
                    return;
                }

                // 2. 본체 하드웨어 MAC 일치 확인
                uint8_t my_chip_mac[6] = {0};
                if (esp_read_mac(my_chip_mac, ESP_MAC_WIFI_STA) == ESP_OK) {
                    if (memcmp(custom_mac, my_chip_mac, sizeof(custom_mac)) != 0) {
                        LogWarn( ( "타겟 MAC 불일치 무시" ) );
                        return;
                    }
                }
                mqtt_rx_messege(pPublishInfo);
                // 3. event_type별 동작 분기
                if (event_type != NULL) {
                    if (event_type_len == 4 && strncmp(event_type, "boot", 4) == 0) {
                        LogInfo( ( "[이벤트 처리] boot 수신" ) );
                    }
                    else if (event_type_len == 12 && strncmp(event_type, "registration", 12) == 0) {
                        LogInfo( ( "[이벤트 처리] registration 수신" ) );
                    }
                    else if (event_type_len == 6 && strncmp(event_type, "access", 6) == 0) {
                        LogInfo( ( "[이벤트 처리] access 수신" ) );
                    }
                    else if (event_type_len == 5 && strncmp(event_type, "drink", 5) == 0) {
                        LogInfo( ( "[이벤트 처리] drink 수신" ) );
                    }
                    else {
                        LogInfo( ( "[기타 이벤트]: %.*s", event_type_len, event_type ) );
                    }
                }
            }
            else
            {
                // things/ 포맷도 아닌 기타 일반 토픽
                LogInfo( ( " is_custom_thing 불일치"));
                
            }
        }
    }
    else
    {
        if( api == FleetProvCborCreateCertFromCsrAccepted )
        {
            LogInfo( ( "Received accepted response from Fleet Provisioning CreateCertificateFromCsr API." ) );

            cborDump = getStringFromCbor( ( const uint8_t * ) pPublishInfo->pPayload, pPublishInfo->payloadLength );
            LogDebug( ( "Payload: %s", cborDump ) );
            free( ( void * ) cborDump );

            responseStatus = ResponseAccepted;

            /* Copy the payload from the MQTT library's buffer to #payloadBuffer. */
            ( void ) memcpy( ( void * ) payloadBuffer,
                             ( const void * ) pPublishInfo->pPayload,
                             ( size_t ) pPublishInfo->payloadLength );

            payloadLength = pPublishInfo->payloadLength;
        }
        else if( api == FleetProvCborCreateCertFromCsrRejected )
        {
            LogError( ( "Received rejected response from Fleet Provisioning CreateCertificateFromCsr API." ) );

            cborDump = getStringFromCbor( ( const uint8_t * ) pPublishInfo->pPayload, pPublishInfo->payloadLength );
            LogError( ( "Payload: %s", cborDump ) );
            free( ( void * ) cborDump );

            responseStatus = ResponseRejected;
        }
        else if( api == FleetProvCborRegisterThingAccepted )
        {
            LogInfo( ( "Received accepted response from Fleet Provisioning RegisterThing API." ) );

            cborDump = getStringFromCbor( ( const uint8_t * ) pPublishInfo->pPayload, pPublishInfo->payloadLength );
            LogDebug( ( "Payload: %s", cborDump ) );
            free( ( void * ) cborDump );

            responseStatus = ResponseAccepted;

            /* Copy the payload from the MQTT library's buffer to #payloadBuffer. */
            ( void ) memcpy( ( void * ) payloadBuffer,
                             ( const void * ) pPublishInfo->pPayload,
                             ( size_t ) pPublishInfo->payloadLength );

            payloadLength = pPublishInfo->payloadLength;
        }
        else if( api == FleetProvCborRegisterThingRejected )
        {
            LogError( ( "Received rejected response from Fleet Provisioning RegisterThing API." ) );

            cborDump = getStringFromCbor( ( const uint8_t * ) pPublishInfo->pPayload, pPublishInfo->payloadLength );
            LogError( ( "Payload: %s", cborDump ) );
            free( ( void * ) cborDump );

            responseStatus = ResponseRejected;
        }
        else
        {
            LogError( ( "Received message on unexpected Fleet Provisioning topic. Topic: %.*s.",
                        ( int ) pPublishInfo->topicNameLength,
                        ( const char * ) pPublishInfo->pTopicName ) );
        }
    }
}
/*-----------------------------------------------------------*/
#define MQTT_PROCESS_LOOP_TIMEOUT_MS             ( 1000U )
static bool waitForResponse( void )
{
    bool status = false;

    responseStatus = ResponseNotReceived;

    /* responseStatus is updated from the MQTT publish callback. */
    ( void ) ProcessLoopWithTimeout(MQTT_PROCESS_LOOP_TIMEOUT_MS);

    if( responseStatus == ResponseNotReceived )
    {
        LogError( ( "Timed out waiting for response." ) );
    }

    if( responseStatus == ResponseAccepted )
    {
        status = true;
    }

    return status;
}
/*-----------------------------------------------------------*/

static bool subscribeToCsrResponseTopics( void )
{
    bool status;

    status = SubscribeToTopic( FP_CBOR_CREATE_CERT_ACCEPTED_TOPIC,
                               FP_CBOR_CREATE_CERT_ACCEPTED_LENGTH );

    if( status == false )
    {
        LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                    FP_CBOR_CREATE_CERT_ACCEPTED_LENGTH,
                    FP_CBOR_CREATE_CERT_ACCEPTED_TOPIC ) );
    }

    if( status == true )
    {
        status = SubscribeToTopic( FP_CBOR_CREATE_CERT_REJECTED_TOPIC,
                                   FP_CBOR_CREATE_CERT_REJECTED_LENGTH );

        if( status == false )
        {
            LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                        FP_CBOR_CREATE_CERT_REJECTED_LENGTH,
                        FP_CBOR_CREATE_CERT_REJECTED_TOPIC ) );
        }
    }

    return status;
}
/*-----------------------------------------------------------*/

static bool unsubscribeFromCsrResponseTopics( void )
{
    bool status;

    status = UnsubscribeFromTopic( FP_CBOR_CREATE_CERT_ACCEPTED_TOPIC,
                                   FP_CBOR_CREATE_CERT_ACCEPTED_LENGTH );

    if( status == false )
    {
        LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                    FP_CBOR_CREATE_CERT_ACCEPTED_LENGTH,
                    FP_CBOR_CREATE_CERT_ACCEPTED_TOPIC ) );
    }

    if( status == true )
    {
        status = UnsubscribeFromTopic( FP_CBOR_CREATE_CERT_REJECTED_TOPIC,
                                       FP_CBOR_CREATE_CERT_REJECTED_LENGTH );

        if( status == false )
        {
            LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                        FP_CBOR_CREATE_CERT_REJECTED_LENGTH,
                        FP_CBOR_CREATE_CERT_REJECTED_TOPIC ) );
        }
    }

    return status;
}
/*-----------------------------------------------------------*/

static bool subscribeToRegisterThingResponseTopics( void )
{
    bool status;

    status = SubscribeToTopic( FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                               FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

    if( status == false )
    {
        LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                    FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                    FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
    }

    if( status == true )
    {
        status = SubscribeToTopic( FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                   FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

        if( status == false )
        {
            LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                        FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                        FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
        }
    }

    return status;
}
/*-----------------------------------------------------------*/

static bool unsubscribeFromRegisterThingResponseTopics( void )
{
    bool status;

    status = UnsubscribeFromTopic( FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                   FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

    if( status == false )
    {
        LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                    FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                    FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
    }

    if( status == true )
    {
        status = UnsubscribeFromTopic( FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                       FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

        if( status == false )
        {
            LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                        FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                        FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
        }
    }

    return status;
}


int aws_iot_provisioning_main(int argc, char **argv)
{
    bool status = false;
    bool is_already_registered = false;
    bool connectionEstablished = false;

    char *csr = NULL;
    char *certificate = NULL;
    char *ownershipToken = NULL;
    char *localPayloadBuffer = NULL;

    size_t csrLength = 0;
    size_t certificateLength = 0;
    size_t certificateIdLength = 0;
    size_t ownershipTokenLength = 0;

    char certificateId[CERT_ID_BUFFER_LENGTH] = {0};
    char thing_name_str[100] = {0};

    CK_SESSION_HANDLE p11Session = CK_INVALID_HANDLE;
    CK_FUNCTION_LIST_PTR pFunctionList = NULL;
    CK_RV pkcs11ret = CKR_OK;

    int demoRunCount = 0;

    uint8_t mac_byte[6];
    char dynamicMacStr[13];

    (void)argc;
    (void)argv;

    esp_read_mac(mac_byte, ESP_MAC_WIFI_STA);

    snprintf(dynamicMacStr,
             sizeof(dynamicMacStr),
             "%02X%02X%02X%02X%02X%02X",
             mac_byte[0], mac_byte[1], mac_byte[2],
             mac_byte[3], mac_byte[4], mac_byte[5]);

    localPayloadBuffer = calloc(1, CONFIG_MQTT_NETWORK_BUFFER_SIZE);

    if(localPayloadBuffer == NULL)
    {
        LogError(("Payload buffer allocation failed"));
        goto cleanup;
    }

    payloadBuffer = (uint8_t *)localPayloadBuffer;

    // 1. PKCS11 세션 초기화
    pkcs11ret = xInitializePkcs11Session(&p11Session);

    if(pkcs11ret != CKR_OK)
    {
        LogError(("Failed to initialize PKCS11"));
        goto cleanup;
    }

    // 2. Claim 인증서 로드
    // Claim 인증서는 최초 프로비저닝 시 사용
    is_already_registered = read_nvs_registration_flag();

    if(is_already_registered == false)
    {
        status = loadClaimCredentials(
            p11Session,
            CLAIM_CERT_PATH,
            pkcs11configLABEL_CLAIM_CERTIFICATE,
            CLAIM_PRIVATE_KEY_PATH,
            pkcs11configLABEL_CLAIM_PRIVATE_KEY
        );

        if(status != true)
        {
            LogError(("Claim certificate load failed"));
            goto cleanup;
        }

        // 3. 최초 프로비저닝일 때만 Device Key Pair 생성
        csr = calloc(1, CSR_BUFFER_LENGTH);

        if(csr == NULL)
        {
            LogError(("CSR buffer allocation failed"));
            goto cleanup;
        }

        status = generateKeyAndCsr(
            p11Session,
            pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS,
            pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS,
            csr,
            CSR_BUFFER_LENGTH,
            &csrLength
        );

        if(status != true)
        {
            LogError(("Device Key Pair generation failed"));
            goto cleanup;
        }
    }
    else
    {
        LogInfo(("[Registered device] Existing Device Key Pair will be reused"));

        // 중요:
        // 기등록 기기에서는 generateKeyAndCsr() 호출 금지
    }

    // 4. MQTT Payload 버퍼 준비
    // 최초 프로비저닝에만 필요한 임시 버퍼
    if(is_already_registered == false)
    {
        certificate = calloc(1, CERT_BUFFER_LENGTH);
        ownershipToken = calloc(1, OWNERSHIP_TOKEN_BUFFER_LENGTH);

        if(certificate == NULL || ownershipToken == NULL)
        {
            LogError(("Provisioning temporary buffer allocation failed"));
            goto cleanup;
        }

        certificateLength = CERT_BUFFER_LENGTH;
        certificateIdLength = CERT_ID_BUFFER_LENGTH;
        ownershipTokenLength = OWNERSHIP_TOKEN_BUFFER_LENGTH;

        // 5. Claim MQTT 연결
        status = EstablishMqttSession(
            provisioningPublishCallback,
            p11Session,
            pkcs11configLABEL_CLAIM_CERTIFICATE,
            pkcs11configLABEL_CLAIM_PRIVATE_KEY
        );

        if(status != true)
        {
            LogError(("Claim MQTT connection failed"));
            goto cleanup;
        }

        // 6. CSR 응답 구독
        status = subscribeToCsrResponseTopics();

        if(status != true)
        {
            LogError(("CSR subscribe failed"));
            goto cleanup;
        }

        // 7. CSR Publish
        status = generateCsrRequest(
            payloadBuffer,
            CONFIG_MQTT_NETWORK_BUFFER_SIZE,
            csr,
            csrLength,
            &payloadLength
        );

        if(status != true)
        {
            LogError(("CSR request generation failed"));
            goto cleanup;
        }

        status = PublishToTopic(
            FP_CBOR_CREATE_CERT_PUBLISH_TOPIC,
            FP_CBOR_CREATE_CERT_PUBLISH_LENGTH,
            (char *)payloadBuffer,
            payloadLength
        );

        if(status != true)
        {
            LogError(("CSR publish failed"));
            goto cleanup;
        }

        status = waitForResponse();

        if(status != true)
        {
            LogError(("CSR response timeout"));
            goto cleanup;
        }

        status = parseCsrResponse(
            payloadBuffer,
            payloadLength,
            certificate,
            &certificateLength,
            certificateId,
            &certificateIdLength,
            ownershipToken,
            &ownershipTokenLength
        );

        if(status != true)
        {
            LogError(("CSR response parsing failed"));
            goto cleanup;
        }

        unsubscribeFromCsrResponseTopics();

        LogInfo(("New device certificate received"));

        // 8. Thing 등록 요청
        snprintf(
            thing_name_str,
            sizeof(thing_name_str),
            "%s_%s",
            CONFIG_DEVICE_PREFIX,
            dynamicMacStr
        );

        status = generateRegisterThingRequest(
            payloadBuffer,
            CONFIG_MQTT_NETWORK_BUFFER_SIZE,
            ownershipToken,
            ownershipTokenLength,
            thing_name_str,
            strlen(thing_name_str),
            &payloadLength
        );

        if(status != true)
        {
            LogError(("Register Thing request generation failed"));
            goto cleanup;
        }

        status = subscribeToRegisterThingResponseTopics();

        if(status != true)
        {
            LogError(("Register Thing subscribe failed"));
            goto cleanup;
        }

        status = PublishToTopic(
            FP_CBOR_REGISTER_PUBLISH_TOPIC(PROVISIONING_TEMPLATE_NAME),
            FP_CBOR_REGISTER_PUBLISH_LENGTH(PROVISIONING_TEMPLATE_NAME_LENGTH),
            (char *)payloadBuffer,
            payloadLength
        );

        if(status != true)
        {
            LogError(("Register Thing publish failed"));
            goto cleanup;
        }

        status = waitForResponse();

        if(status != true)
        {
            LogError(("Register Thing response timeout"));
            goto cleanup;
        }

        thingNameLength = MAX_THING_NAME_LENGTH;

        status = parseRegisterThingResponse(
            payloadBuffer,
            payloadLength,
            thingName,
            &thingNameLength
        );

        if(status != true)
        {
            LogError(("Register Thing response parsing failed"));
            goto cleanup;
        }

        unsubscribeFromRegisterThingResponseTopics();

        // 9. Device 인증서 저장
        // loadCertificate의 실제 반환형 확인 필요
        status = loadCertificate(
            p11Session,
            certificate,
            pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
            certificateLength
        );

        if(status != true)
        {
            LogError(("Device certificate storage failed"));
            goto cleanup;
        }

        // 10. Claim MQTT 연결 종료
        DisconnectMqttSession();

        // 중요:
        // NVS 등록 플래그는 인증서 저장 성공 후에만 설정
        //
        // 여기에 실제 프로젝트의 NVS 등록 완료 저장 함수를 연결하세요.
        //
        // 예:
        // status = write_nvs_registration_flag(true);
        //
        // 함수 구현과 반환형 확인 전까지 임의로 호출하지 마세요.

        LogInfo(("Provisioning completed"));
    }
    else
    {
        LogInfo(("[Registered device] Provisioning skipped"));

        status = true;
    }

    // 11. 정식 Device MQTT 연결
    if(status == true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        status = EstablishMqttSession(
            provisioningPublishCallback,
            p11Session,
            pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
            pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS
        );

        if(status == true)
        {
            LogInfo(("Device MQTT connection successful"));

            connectionEstablished = true;

            if(is_already_registered == false)
            {
                mqtt_queue_send(MESSEGE_REGISTRATION, NULL, 0);
            }
            else
            {
                mqtt_queue_send(MESSEGE_BOOT, NULL, 0);
                mqtt_queue_send(AWS_MESSEGE_SHADOW_GET, NULL, 0);
            }

            mqtt_subscribe_init();

            pkcs11CloseSession(p11Session);
            p11Session = CK_INVALID_HANDLE;

            free(localPayloadBuffer);
            localPayloadBuffer = NULL;
            payloadBuffer = NULL;

            free(csr);
            free(certificate);
            free(ownershipToken);

            return EXIT_SUCCESS;
        }
        else
        {
            LogError(("Device MQTT connection failed"));
            goto cleanup;
        }
    }

cleanup:

    // MQTT 연결 정리
    if(connectionEstablished == false)
    {
        DisconnectMqttSession();
    }

    // 주의:
    // 기존 Device Key Pair는 실패했다고 무조건 삭제하면 안 됨.
    // 인증서와 Key의 관계를 보존해야 함.

    if(p11Session != CK_INVALID_HANDLE)
    {
        pkcs11CloseSession(p11Session);
        p11Session = CK_INVALID_HANDLE;
    }

    if(localPayloadBuffer != NULL)
    {
        free(localPayloadBuffer);
        localPayloadBuffer = NULL;
        payloadBuffer = NULL;
    }

    if(csr != NULL)
    {
        free(csr);
        csr = NULL;
    }

    if(certificate != NULL)
    {
        free(certificate);
        certificate = NULL;
    }

    if(ownershipToken != NULL)
    {
        free(ownershipToken);
        ownershipToken = NULL;
    }

    return EXIT_FAILURE;
}

/*-----------------------------------------------------------*/
