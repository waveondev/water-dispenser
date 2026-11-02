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


bool parse_aws_topic(const char *topic, uint16_t topic_len,
                     char *out_prefix, size_t prefix_len,
                     uint8_t out_mac[6],
                     const char **out_subtopic)
{
    const char prefix_tag[] = "$aws/things/";
    size_t tag_len = sizeof(prefix_tag) - 1;

    // 1. $aws/things/ 접두사 확인
    if (topic_len <= tag_len || strncmp(topic, prefix_tag, tag_len) != 0) {
        return false;
    }

    const char *p = topic + tag_len;
    uint16_t remain_len = topic_len - tag_len;


    const char *underscore = memchr(p, '_', remain_len);
    if (!underscore) return false;

    size_t cur_prefix_len = underscore - p;
    if (cur_prefix_len >= prefix_len) return false; // 버퍼 오버플로우 방지

    memcpy(out_prefix, p, cur_prefix_len);
    out_prefix[cur_prefix_len] = '\0'; 

    // 3. MAC 주소 문자열 (12자리 HEX: "7CE8B1762BE8") 파싱
    const char *mac_str = underscore + 1;
    remain_len -= (cur_prefix_len + 1);

    // 최소 12자리 MAC 문자열 확인
    if (remain_len < 12) return false;

    for (int i = 0; i < 6; i++) {
        unsigned int byte_val = 0;
        // 2글자씩 16진수 파싱
        if (sscanf(mac_str + (i * 2), "%2x", &byte_val) != 1) {
            return false;
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

    return true;
}
/*-----------------------------------------------------------*/
#include "rx_mqtt.h"
#include "shadow_mqtt.h"
#include "jobs_mqtt.h"
static void provisioningPublishCallback( MQTTPublishInfo_t * pPublishInfo,
                                         uint16_t packetIdentifier )
{
    FleetProvisioningStatus_t status;
    FleetProvisioningTopic_t api;
    const char * cborDump;
    /* ========================================================== */
    /* 토픽 분리 및 파싱 적용                                    */
    /* ========================================================== */
    char dev_model[5] = {0};       
    uint8_t dev_mac[6] = {0};      // 6바이트 바이너리 MAC {0x7C, 0xE8, ...}
    const char *sub_topic = NULL;  // 서브토픽 시작 포인터
    /* Silence compiler warnings about unused variables. */

    status = FleetProvisioning_MatchTopic( pPublishInfo->pTopicName,
                                           pPublishInfo->topicNameLength, &api );

    if( status != FleetProvisioningSuccess )
    {
        LogInfo( ( "[수신 테스트] 서버로부터 메시지가 도착했습니다!" ) );
        LogInfo( ( "▶ 토픽: %.*s", ( int ) pPublishInfo->topicNameLength, pPublishInfo->pTopicName ) );
        LogInfo( ( "▶ 내용: %.*s", ( int ) pPublishInfo->payloadLength, ( const char * ) pPublishInfo->pPayload ) );
    
        bool aws_shadow = parse_aws_topic(pPublishInfo->pTopicName, pPublishInfo->topicNameLength,
                                dev_model, sizeof(dev_model),
                                dev_mac,
                                &sub_topic);

        if (aws_shadow == true)
        {
            // sub_topic의 유효 길이 계산 (null 종단 문자가 없을 수 있으므로)
            int sub_len = (int)(pPublishInfo->topicNameLength - (sub_topic - pPublishInfo->pTopicName));

            if (strcmp(dev_model, CONFIG_DEVICE_PREFIX) != 0)
            {
                LogInfo( ( "타겟 모델 불일치 무시 (수신: %s, 본 기기: %s)", dev_model, CONFIG_DEVICE_PREFIX ) );
                return;
            }

            /* ------------------------------------------------------ */
            /* 2. 기기 실제 MAC 일치 여부 확인 (ESP 내부 칩 MAC 조회)   */
            /* ------------------------------------------------------ */
            uint8_t my_chip_mac[6] = {0};
            // Wi-Fi Station 모드의 기본 eFuse MAC 주소 획득
            if (esp_read_mac(my_chip_mac, ESP_MAC_WIFI_STA) != ESP_OK)
            {
                LogInfo( ( "내부 MAC 주소 읽기 실패" ) );
                return;
            }
            if (memcmp(dev_mac, my_chip_mac, sizeof(dev_mac)) != 0)
            {
                LogInfo( ( "타겟 MAC 불일치 무시 (수신: %02X:%02X:%02X:%02X:%02X:%02X, 내 MAC: %02X:%02X:%02X:%02X:%02X:%02X)",
                            dev_mac[0], dev_mac[1], dev_mac[2], dev_mac[3], dev_mac[4], dev_mac[5],
                            my_chip_mac[0], my_chip_mac[1], my_chip_mac[2], my_chip_mac[3], my_chip_mac[4], my_chip_mac[5] ) );
                return;
            }
            LogInfo( ( " 모델: %s", dev_model ) );
            LogInfo( ( " MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                    dev_mac[0], dev_mac[1], dev_mac[2], dev_mac[3], dev_mac[4], dev_mac[5] ) );
            LogInfo( ( " 서브토픽: %.*s", sub_len, sub_topic ) );

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
                    LogInfo( ( " [분기] AWS Jobs 새 작업 수신 (notify)" ) );
                    jobs_mqtt_rx_message(pPublishInfo);
                }
                // 2. 디바이스가 보낸 update에 대한 AWS의 승인 응답 (.../update/accepted)
                else if (strstr(job_sub, "/update/accepted") != NULL)
                {
                    LogInfo( ( " [분기] AWS Jobs Update 성공 확인 (accepted)" ) );
                    // 여기서는 추가 다운로드를 진행하지 않고 로그만 찍거나 ACK 플래그만 세팅
                }
                // 3. 디바이스가 보낸 update에 대한 AWS의 거절 응답 (.../update/rejected)
                else if (strstr(job_sub, "/update/rejected") != NULL)
                {
                    LogError( ( " [분기] AWS Jobs Update 거절됨 (rejected)" ) );
                }
                else
                {
                    LogInfo( ( " [분기] 기타 Jobs 토픽: %.*s", job_sub_len, job_sub ) );
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
                        LogInfo( ( " [분기] AWS Shadow Update -> Delta 수신 (desired 변경사항 반영)" ) );
                        shadow_mqtt_update_delta_rx_messege(pPublishInfo);
                    }
                    // 1-2. update/accepted (디바이스의 reported 보고 성공 응답)
                    else if (action_len >= 8 && strncmp(action, "accepted", 8) == 0)
                    {
                        LogInfo( ( " [분기] AWS Shadow Update -> Accepted 수신 (reported 반영 완료)" ) );
                        // TODO: ACK 플래그 처리 또는 응답 타임아웃 해제
                    }
                    // 1-3. update/rejected (reported 보고 실패/거절)
                    else if (action_len >= 8 && strncmp(action, "rejected", 8) == 0)
                    {
                        LogError( ( " [분기] AWS Shadow Update -> Rejected 수신 (보고 거절됨)" ) );
                    }
                    else
                    {
                        LogInfo( ( " [분기] 기타 Shadow Update 토픽: %.*s", action_len, action ) );
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
                        LogInfo( ( " [분기] AWS Shadow Get -> Accepted 수신 (전체 문서 동기화)" ) );
                        shadow_mqtt_get_accepted_rx_messege(pPublishInfo);
                    }
                    // 2-2. get/rejected (문서가 없거나 권한 없음 등으로 거절)
                    else if (action_len >= 8 && strncmp(action, "rejected", 8) == 0)
                    {
                        LogError( ( " [분기] AWS Shadow Get -> Rejected 수신 (조회 실패)" ) );
                    }
                    else
                    {
                        LogInfo( ( " [분기] 기타 Shadow Get 토픽: %.*s", action_len, action ) );
                    }
                }
                /* ====================================================== */
                /* 3. 기타 Shadow 토픽                                    */
                /* ====================================================== */
                else
                {
                    LogInfo( ( " [분기] 미처리 Shadow 토픽: %.*s", shadow_sub_len, shadow_sub ) );
                }
            }          
            else
            {
                LogInfo( ( " [기타 Thing 토픽]: %.*s", sub_len, sub_topic ) );
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
/*-----------------------------------------------------------*/

/* This example uses a single application task, which shows that how to use
 * the Fleet Provisioning library to generate and validate AWS IoT Fleet
 * Provisioning MQTT topics, and use the coreMQTT library to communicate with
 * the AWS IoT Fleet Provisioning APIs. */
#include "topic_list.h"

// [도우미 함수] 특정 클래스와 라벨을 가진 PKCS#11 객체를 모두 찾아서 전부 파기
static void prvDestroyAllMatchingObjects( CK_SESSION_HANDLE session,
                                          CK_FUNCTION_LIST_PTR pFuncList,
                                          CK_OBJECT_CLASS objClass,
                                          const char * pLabel )
{
    if( ( session == CK_INVALID_HANDLE ) || ( pFuncList == NULL ) || ( pLabel == NULL ) )
    {
        return;
    }

    CK_ATTRIBUTE xTemplate[] = {
        { CKA_CLASS, &objClass, sizeof( objClass ) },
        { CKA_LABEL, ( void * ) pLabel, strlen( pLabel ) }
    };

    if( pFuncList->C_FindObjectsInit( session, xTemplate, 2 ) == CKR_OK )
    {
        CK_OBJECT_HANDLE xObj = CK_INVALID_HANDLE;
        CK_ULONG ulCount = 0;

        // 남아있는 객체가 여러 개일 수 있으므로 while 루프로 싹 지움
        while( ( pFuncList->C_FindObjects( session, &xObj, 1, &ulCount ) == CKR_OK ) && ( ulCount > 0 ) )
        {
            pFuncList->C_DestroyObject( session, xObj );
        }
        pFuncList->C_FindObjectsFinal( session );
    }
}

int aws_iot_provisioning_main( int argc, char ** argv )
{
    bool status = false;
    char* csr = NULL;
    char* certificate = NULL;
    char* ownershipToken = NULL;
    char* localPayloadBuffer = NULL;

    size_t csrLength = 0;
    size_t certificateLength;
    char certificateId[ CERT_ID_BUFFER_LENGTH ];
    size_t certificateIdLength;
    size_t ownershipTokenLength;
    bool connectionEstablished = false;
    CK_SESSION_HANDLE p11Session = CK_INVALID_HANDLE;
    int demoRunCount = 0;
    CK_RV pkcs11ret = CKR_OK;

    ( void ) argc;
    ( void ) argv;
    uint8_t mac_byte[6];
    char dynamicMacStr[13];

    esp_read_mac(mac_byte, ESP_MAC_WIFI_STA);
    snprintf(dynamicMacStr, sizeof(dynamicMacStr), "%02X%02X%02X%02X%02X%02X",
            mac_byte[0], mac_byte[1], mac_byte[2], mac_byte[3], mac_byte[4], mac_byte[5]);
    uint8_t index = 0;
    ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

    localPayloadBuffer = (char*)calloc(1, CONFIG_MQTT_NETWORK_BUFFER_SIZE);
    if( localPayloadBuffer == NULL )
    {
        LogError( ( "payloadBuffer 메모리 할당 실패" ) );
        return EXIT_FAILURE;
    }
    payloadBuffer = (uint8_t*)localPayloadBuffer;

    // 1. PKCS#11 세션 초기화
    pkcs11ret = xInitializePkcs11Session( &p11Session );
    if( pkcs11ret != CKR_OK )
    {
        LogError( ( "Failed to initialize PKCS #11." ) );
        free(localPayloadBuffer);
        return EXIT_FAILURE;
    }

    // =========================================================================
    // [핵심] 재진입 시 이전 실패 시도에서 남겨진 미완성 객체 강제 전수 파기 (누수 원천 차단)
    // =========================================================================
    CK_FUNCTION_LIST_PTR pFunctionList = NULL;
    if( C_GetFunctionList( &pFunctionList ) == CKR_OK )
    {
        // 1) 이전 Claim Cert / Key 파기
        prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_CERTIFICATE, pkcs11configLABEL_CLAIM_CERTIFICATE );
        prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_PRIVATE_KEY, pkcs11configLABEL_CLAIM_PRIVATE_KEY );

        // 2) 이전 Device Key Pair 파기 (Private Key뿐만 아니라 Public Key도 함께 파기해야 누수가 안 남음!)
        prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_PRIVATE_KEY, pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS );
        prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_PUBLIC_KEY, pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS );

        // 3) 혹시 이전에 쓰다 만 Device Cert가 남아있다면 파기
        prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_CERTIFICATE, pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS );
    }
    // =========================================================================

    // 2. Claim 인증서 로드 (루프 밖 1회)
    status = loadClaimCredentials( p11Session,
                                   CLAIM_CERT_PATH,
                                   pkcs11configLABEL_CLAIM_CERTIFICATE,
                                   CLAIM_PRIVATE_KEY_PATH,
                                   pkcs11configLABEL_CLAIM_PRIVATE_KEY );
    ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

    if( status != true )
    {
        LogError( ( "Claim 인증서 로드 실패" ) );
        pkcs11CloseSession( p11Session );
        free(localPayloadBuffer);
        return EXIT_FAILURE;
    }

    // 3. 디바이스 키 쌍 및 CSR 생성 (루프 밖 1회)
    csr = (char*)calloc(1, CSR_BUFFER_LENGTH);
    if( csr == NULL )
    {
        LogError( ( "CSR 버퍼 할당 실패" ) );
        pkcs11CloseSession( p11Session );
        free(localPayloadBuffer);
        return EXIT_FAILURE;
    }

    status = generateKeyAndCsr( p11Session,
                                pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS,
                                pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS,
                                csr, CSR_BUFFER_LENGTH, &csrLength );
    ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

    do
    {
        bool is_already_registered = read_nvs_registration_flag();

        if ( is_already_registered == false )
        {
            LogInfo( ( "[최초 실행] Fleet Provisioning 시작" ) );

            // 루프마다 새로 받아오는 임시 버퍼만 해제 후 재할당 (csr은 재사용)
            if(certificate) { free(certificate); certificate = NULL; }
            if(ownershipToken) { free(ownershipToken); ownershipToken = NULL; }

            certificate = (char*)calloc(1, CERT_BUFFER_LENGTH);
            ownershipToken = (char*)calloc(1, OWNERSHIP_TOKEN_BUFFER_LENGTH);
            certificateLength = CERT_BUFFER_LENGTH;
            certificateIdLength = CERT_ID_BUFFER_LENGTH;
            ownershipTokenLength = OWNERSHIP_TOKEN_BUFFER_LENGTH;
            
            ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());
            
            if( !certificate || !ownershipToken )
            {
                LogError( ( "프로비저닝 임시 버퍼 할당 실패" ) );
                status = false;
            }
            else
            {
                // Claim MQTT 연결
                status = EstablishMqttSession( provisioningPublishCallback,
                                               p11Session,
                                               pkcs11configLABEL_CLAIM_CERTIFICATE,
                                               pkcs11configLABEL_CLAIM_PRIVATE_KEY );
                
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) status = subscribeToCsrResponseTopics();
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                // 이미 루프 밖에서 만든 csr 재사용
                if( status == true ) status = generateCsrRequest( payloadBuffer, CONFIG_MQTT_NETWORK_BUFFER_SIZE, csr, csrLength, &payloadLength );
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) status = PublishToTopic( FP_CBOR_CREATE_CERT_PUBLISH_TOPIC, FP_CBOR_CREATE_CERT_PUBLISH_LENGTH, ( char * ) payloadBuffer, payloadLength );
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) status = waitForResponse();
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) {
                    status = parseCsrResponse( payloadBuffer, payloadLength, certificate, &certificateLength, certificateId, &certificateIdLength, ownershipToken, &ownershipTokenLength );
                    if( status == true ) LogInfo( ( "새 인증서 발급 성공" ) );
                }
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) unsubscribeFromCsrResponseTopics();
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                // Thing 등록 요청
                char thing_name_str[100];
                snprintf(thing_name_str, sizeof(thing_name_str), "%s_%s", CONFIG_DEVICE_PREFIX, dynamicMacStr);

                if( status == true ) status = generateRegisterThingRequest( payloadBuffer, CONFIG_MQTT_NETWORK_BUFFER_SIZE, ownershipToken, ownershipTokenLength, thing_name_str, strlen(thing_name_str), &payloadLength );
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) status = subscribeToRegisterThingResponseTopics();
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) status = PublishToTopic( FP_CBOR_REGISTER_PUBLISH_TOPIC( PROVISIONING_TEMPLATE_NAME ), FP_CBOR_REGISTER_PUBLISH_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ), ( char * ) payloadBuffer, payloadLength );
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) status = waitForResponse();
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) {
                    thingNameLength = MAX_THING_NAME_LENGTH;
                    status = parseRegisterThingResponse( payloadBuffer, payloadLength, thingName, &thingNameLength );
                }
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                if( status == true ) unsubscribeFromRegisterThingResponseTopics();
                ESP_LOGE("HEAP_CHECK", "%d: %d", index++, esp_get_free_heap_size());

                // Thing 등록까지 완전히 성공했을 때만 Device Cert 영구 저장
                if( status == true ) {
                    loadCertificate( p11Session, certificate, pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS, certificateLength );
                }

                // Claim 세션 종료
                DisconnectMqttSession();
            }

            if(certificate) { free(certificate); certificate = NULL; }
            if(ownershipToken) { free(ownershipToken); ownershipToken = NULL; }
        }
        else
        {
            LogInfo( ( "[기등록 기기] 발급 요청 건너뜀" ) );
            status = true;
        }

        // 정식 MQTT 연결 (성공 시)
        if( status == true )
        {
            vTaskDelay(pdMS_TO_TICKS(1000));

            status = EstablishMqttSession( provisioningPublishCallback,
                                           p11Session,
                                           pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
                                           pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS );

            if( status == true )
            {
                LogInfo( ( "정식 MQTT 연결 성공!" ) );
                connectionEstablished = true;

                if (is_already_registered == false) {
                    mqtt_queue_send(MESSEGE_REGISTRATION, NULL, 0);
                } else {
                    mqtt_queue_send(MESSEGE_BOOT, NULL, 0);
                    mqtt_queue_send(AWS_MESSEGE_SHADOW_GET, NULL, 0);
                }

                mqtt_subscribe_init();
                
                pkcs11CloseSession( p11Session );
                free(localPayloadBuffer);
                if(csr) free(csr);
                payloadBuffer = NULL;
                return EXIT_SUCCESS;
            }
        }

        demoRunCount++;
        if( demoRunCount < 1 )
        {
            vTaskDelay(pdMS_TO_TICKS(DELAY_BETWEEN_DEMO_RETRY_ITERATIONS_SECONDS * 1000));
        }
        else
        {
            break;
        }

    } while( status != true );

    // =========================================================================
    // 최종 실패 종료 시: 이번 시도에서 만든 미완성 Key Pair 파기 및 세션 해제
    // =========================================================================
    if( p11Session != CK_INVALID_HANDLE )
    {
        if( pFunctionList != NULL )
        {
            // 등록에 실패했으므로 생성했던 미완성 Private Key와 Public Key 모두 파기
            prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_PRIVATE_KEY, pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS );
            prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_PUBLIC_KEY, pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS );
// 2. ★ 추가된 부분: 이번 세션에서 메모리에 로드했던 Claim Cert와 Key도 명시적으로 파기 ★
            prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_CERTIFICATE, pkcs11configLABEL_CLAIM_CERTIFICATE );
            prvDestroyAllMatchingObjects( p11Session, pFunctionList, CKO_PRIVATE_KEY, pkcs11configLABEL_CLAIM_PRIVATE_KEY );
        }

        pkcs11CloseSession( p11Session );
        p11Session = CK_INVALID_HANDLE;
    }

    if(localPayloadBuffer) { free(localPayloadBuffer); payloadBuffer = NULL; }
    if(csr) free(csr);
    if(certificate) free(certificate);
    if(ownershipToken) free(ownershipToken);

    return ( status == true ) ? EXIT_SUCCESS : EXIT_FAILURE;
}
/*-----------------------------------------------------------*/
