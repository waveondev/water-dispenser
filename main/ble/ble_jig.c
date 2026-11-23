#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

/* NimBLE 헤더 */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "esp_central.h"
#include "ble_parse.h"
#include "FreeRTOS_CLI.h"
static const char *TAG = "BLE_CLIENT_PING";
#include "device_config.h"
// 대상 디바이스 이름

#define TARGET_DEVICE_NAME "Pallo"

#define PING_MAX_SEQ       100

// UUID 설정 (Little Endian)
#define BLE_SVC_UUID128_ARR    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,  0x93,0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
#define BLE_CHR_RX_UUID128_ARR 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,  0x93,0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E 
#define BLE_CHR_TX_UUID128_ARR 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,  0x93,0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E 

static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(BLE_SVC_UUID128_ARR);
static const ble_uuid128_t nus_rx_uuid  = BLE_UUID128_INIT(BLE_CHR_RX_UUID128_ARR);
static const ble_uuid128_t nus_tx_uuid  = BLE_UUID128_INIT(BLE_CHR_TX_UUID128_ARR);

static uint8_t own_addr_type;
static uint16_t peer_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t peer_write_val_handle = 0;
static bool is_connecting = false;

// 핑 테스트용 전역 변수
static SemaphoreHandle_t ping_start_sem = NULL;
static int64_t ping_send_time[PING_MAX_SEQ];
static bool    ping_received[PING_MAX_SEQ];
static int8_t  ping_rssi[PING_MAX_SEQ]; // ⭐️ 추가: 각 Seq 별 RSSI 저장 배열
static float   ping_rtt_ms[PING_MAX_SEQ]; // ⭐️ 추가: 각 Seq 별 RTT(ms) 저장 배열
static int     ping_rx_count = 0;
Motion_Packet_t health_Packet;
Motion_Packet_t menufact1_Packet;
Motion_Packet_t menufact2_Packet;
static int ble_spp_client_gap_event(struct ble_gap_event *event, void *arg);
static esp_timer_handle_t ping_timeout_timer = NULL;
static esp_timer_handle_t health_timeout_timer = NULL;


#define SEC_TO_US(sec) ((uint64_t)(sec) * 1000000ULL)
void ping_timer_Set(bool state,uint64_t timeout);
void health_timer_Set(bool state,uint64_t timeout);


static int target_ble = 0;
// 비콘 스캔 시작
static void ble_spp_client_scan(void)
{
    struct ble_gap_disc_params disc_params;
    memset(&disc_params, 0, sizeof(disc_params));
    disc_params.filter_duplicates = 0;
    disc_params.passive = 0; 
    disc_params.itvl = 160;  
    disc_params.window = 160; 

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, ble_spp_client_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Scan started looking for '%s'...", TARGET_DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "Scan start failed rc=%d", rc);
    }
}

// 60Hz 핑 송신 태스크
static void ble_ping_60hz_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Ping Task Created. Waiting for connection...");

    while (1) {
        // 연결 완료 및 구독 성공 시 세마포어 획득
        if (xSemaphoreTake(ping_start_sem, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "===============================================");
            ESP_LOGI(TAG, "Ping Test Started (60Hz, %d Packets)", PING_MAX_SEQ);
            ESP_LOGI(TAG, "===============================================");


            memset(ping_received, 0, sizeof(ping_received));
            memset(ping_rssi, 0, sizeof(ping_rssi)); // ⭐️ RSSI 버퍼 초기화
            memset(ping_rtt_ms, 0, sizeof(ping_rtt_ms)); // ⭐️ RTT 버퍼 초기화 (0.0f)
            ping_rx_count = 0;
            
            TickType_t xLastWakeTime = xTaskGetTickCount();
            const TickType_t xFrequency = pdMS_TO_TICKS(16); // 16ms 주기 방어

            for (uint8_t seq = 0; seq < PING_MAX_SEQ; seq++) {
                if (peer_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                    ESP_LOGW(TAG, "Disconnected during ping!");
                    break;
                }

                Motion_Packet_t payload = {0};
                payload.event_code = PING_REQUEST;
                payload.ping_res.seq = seq; // 시퀀스 번호 삽입

                // 송신 타임스탬프 기록
                ping_send_time[seq] = esp_timer_get_time();

                int rc = 0;
                int retry_count = 0;
                //do {
                    rc = ble_gattc_write_no_rsp_flat(peer_conn_handle, peer_write_val_handle, &payload, sizeof(payload));
                    if (rc == 6) { // BLE_HS_ENOMEM (큐 꽉 참)
                        vTaskDelay(pdMS_TO_TICKS(2)); 
                    }
               // } while (rc == 6);

                if (rc != 0) {
                    ESP_LOGE(TAG, "TX Error seq=%d, rc=%d (Queue Full Retry Failed)", seq, rc);
                }

                // 16.6ms(16ms) 엄격한 대기
                vTaskDelayUntil(&xLastWakeTime, xFrequency);
            }
        }
    }
}

// Characteristic Handle 추출 및 Notify 구독 요청
static void ble_spp_client_set_handle_and_subscribe(const struct peer *peer)
{
    const struct peer_chr *chr_rx, *chr_tx;
    const struct peer_dsc *dsc;

    chr_rx = peer_chr_find_uuid(peer, &nus_svc_uuid.u, &nus_rx_uuid.u);
    if (chr_rx != NULL) {
        peer_write_val_handle = chr_rx->chr.val_handle;
    }

    chr_tx = peer_chr_find_uuid(peer, &nus_svc_uuid.u, &nus_tx_uuid.u);
    if (chr_tx != NULL) {
        dsc = peer_dsc_find_uuid(peer, &nus_svc_uuid.u, &nus_tx_uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
        if (dsc != NULL) {
            uint8_t value[2] = {1, 0}; // Notification 활성화
            ble_gattc_write_flat(peer->conn_handle, dsc->dsc.handle, value, sizeof(value), NULL, NULL);
        }
    }
}

// 서비스 탐색 완료 콜백
static void ble_spp_client_on_disc_complete(const struct peer *peer, int status, void *arg)
{
    if (status != 0) {
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    
    ble_spp_client_set_handle_and_subscribe(peer);

    // 구독 후 잠시 대기했다가 핑 송신 세마포어 오픈
    vTaskDelay(pdMS_TO_TICKS(5000));
    //xSemaphoreGive(ping_start_sem); 
}

// GAP 이벤트 콜백
static int ble_spp_client_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
    {
        if (is_connecting) return 0;

        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) return 0;

        if (fields.name != NULL && fields.name_len > 0) {
            if (strncmp((const char*)fields.name, TARGET_DEVICE_NAME, strlen(TARGET_DEVICE_NAME)) == 0)
            {
                target_ble = 1;
            }
            else if(strncmp((const char*)fields.name, CONFIG_BLE_DEVICE_NAME, strlen(CONFIG_BLE_DEVICE_NAME)) == 0)
            {
                target_ble = 2;
            }
            else 
                return 0;

            if (target_ble){
                uint8_t ev_type = event->disc.event_type;

                // 1. 연결 가능 여부 판별
                if (!(ev_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || 
                    ev_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)) {
                    return 0;
                    
                }

                is_connecting = true;
                ble_gap_disc_cancel();
                ESP_LOGI(TAG, "Found target '%s', Connecting...", TARGET_DEVICE_NAME);

                // 💡 [수정됨] 연결 파라미터 완벽 세팅
                struct ble_gap_conn_params conn_params = {0};
                
                // 1. 연결을 맺기 위한 스캔 파라미터 (이게 0이면 에러 발생)
                conn_params.scan_itvl = 16;     // 16 * 0.625ms = 10ms
                conn_params.scan_window = 16;   // 16 * 0.625ms = 10ms
                
                // 2. 60Hz 데이터 전송을 위한 15ms 통신 간격
                conn_params.itvl_min = 12;      // 12 * 1.25ms = 15ms
                conn_params.itvl_max = 12;      // 12 * 1.25ms = 15ms
                conn_params.latency = 0;
                conn_params.supervision_timeout = 400; // 400 * 10ms = 4초

                rc = ble_gap_connect(own_addr_type, &event->disc.addr, 10000, &conn_params, ble_spp_client_gap_event, NULL);
                if (rc != 0) {
                    // ⭐️ 실패 시 로그 출력 추가 (rc=4면 파라미터 에러, rc=2면 Busy 상태)
                    ESP_LOGE(TAG, "Connection request failed (rc=%d). Resuming scan...", rc);
                    is_connecting = false;
                    target_ble = 0;
                    ble_spp_client_scan();
                }
            }
        }
        return 0;
    }

        case BLE_GAP_EVENT_CONNECT:
            health_timer_Set(true, 5);
            ping_timer_Set(true,10);
        if (event->connect.status == 0) {
            peer_conn_handle = event->connect.conn_handle;
            // 버퍼 초기화
            memset(&health_Packet,0,sizeof(health_Packet));
            memset(&menufact1_Packet,0,sizeof(menufact1_Packet));
            memset(&menufact2_Packet,0,sizeof(menufact2_Packet));
            APP_String_printf("Connected\r\n");
            peer_add(peer_conn_handle);
            peer_disc_all(peer_conn_handle, ble_spp_client_on_disc_complete, NULL);
        } else {
            // ⭐️ 연결 거부 또는 타임아웃 발생 시 로그 출력
            ESP_LOGE(TAG, "Connection failed with status=%d", event->connect.status);
            is_connecting = false;
            target_ble = 0;
            ble_spp_client_scan();
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
    {
        uint16_t data_len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (data_len > 0) {
            Motion_Packet_t rx_buf = {0};
            const uint8_t *mac = NULL;
            if (ble_gap_conn_find(event->notify_rx.conn_handle, &desc) == 0) {
                // desc.peer_ota_addr.val 에 6바이트 MAC 주소가 담겨있습니다. (Little Endian)
                mac = desc.peer_ota_addr.val;
                os_mbuf_copydata(event->notify_rx.om, 0, data_len, &rx_buf);
                ESP_LOGI("BLE_JIG", "[MAC: %02X:%02X:%02X:%02X:%02X:%02X] Notify 수신 (Handle: %d, Len: %d)",
                        mac[5], mac[4], mac[3], mac[2], mac[1], mac[0],
                        event->notify_rx.conn_handle, data_len);
                ESP_LOGI(TAG,"[BLE_Receive_data] %d 바이트 데이터 처리 중: ", data_len);
                ESP_LOG_BUFFER_HEXDUMP(TAG, &rx_buf, data_len, ESP_LOG_INFO);
                ping_timer_Set(true,2);
                    Motion_Packet_t payload = {0};                
                switch(rx_buf.event_code)
                {
                    case TIME_REQUEST:
      
                      //  payload.event_code = HEALTH_DATA_REQUEST;

                        // 응답 없는 Write (가장 빠름)
                       //  ble_gattc_write_no_rsp_flat(peer_conn_handle, peer_write_val_handle, &payload, sizeof(payload));

                    break;
                    case HEALTH_DATA_RESPONSE:
                        payload.event_code = MANUFACT1_REQUEST;
                        memcpy(&health_Packet,&rx_buf,sizeof(Motion_Packet_t));
                        // 응답 없는 Write (가장 빠름)
                        ble_gattc_write_no_rsp_flat(peer_conn_handle, peer_write_val_handle, &payload, sizeof(payload));
                    break;
                    case MANUFACT1_RESPONSE:
                        memcpy(&menufact1_Packet,&rx_buf,sizeof(Motion_Packet_t));                    
                        xSemaphoreGive(ping_start_sem);               
                    break;
                    case MANUFACT2_RESPONSE:
                        memcpy(&menufact2_Packet,&rx_buf,sizeof(Motion_Packet_t));      
                        payload.event_code = MANUFACT1_REQUEST;
                        // 응답 없는 Write (가장 빠름)
                        ble_gattc_write_no_rsp_flat(peer_conn_handle, peer_write_val_handle, &payload, sizeof(payload));                          
                    break;

                    case PING_RESPONSE:
                        uint8_t rx_seq = rx_buf.ping_res.seq;

                            if (rx_seq < PING_MAX_SEQ && !ping_received[rx_seq]) {
                                ping_received[rx_seq] = true;
                                ping_rx_count++;

                                int8_t current_rssi = 0;
                                ble_gap_conn_rssi(event->notify_rx.conn_handle, &current_rssi);
                                ping_rssi[rx_seq] = current_rssi;

                                // RTT 계산 및 ms 단위 저장
                                int64_t rtt_us = esp_timer_get_time() - ping_send_time[rx_seq];
                                ping_rtt_ms[rx_seq] = (float)rtt_us / 1000.0f; // ⭐️ 배열에 저장

                                ESP_LOGI(TAG, "[RX ECHO] Seq: %3d | RTT: %5lld us (%.2f ms) | RSSI: %4d dBm",
                                        rx_seq, rtt_us, ping_rtt_ms[rx_seq], current_rssi);
                            }  
                    break;
                }
            } else {
                ESP_LOGI("BLE_RX", "[Conn Handle: %d] Notify 수신 (Len: %d)", event->notify_rx.conn_handle, data_len);
            }

        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGE(TAG, "Disconnected! reason=%d", event->disconnect.reason);
        peer_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        is_connecting = false;
        target_ble = 0;
        peer_delete(event->disconnect.conn.conn_handle);
        ble_spp_client_scan();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "Conn Param Updated! Interval: %.2f ms", desc.conn_itvl * 1.25);
        }
        return 0;

    default:
        return 0;
    }
}

static void ble_spp_client_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    ESP_LOGI(TAG, "BLE Sync Complete. Starting scan...");
    ble_spp_client_scan();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ping_timer_Set(bool state,uint64_t timeout)
{
 
    if (esp_timer_is_active(ping_timeout_timer)) {
        esp_timer_stop(ping_timeout_timer);
    } 
    if(state)
    {
        esp_timer_start_once(ping_timeout_timer, (SEC_TO_US(timeout)));
        ESP_LOGI(TAG, "ping_timer set"); 
    }
}
void health_timer_Set(bool state,uint64_t timeout)
{
 
    if (esp_timer_is_active(health_timeout_timer)) {
        esp_timer_stop(health_timeout_timer);
    } 
    if(state)
    {
        esp_timer_start_once(health_timeout_timer, (SEC_TO_US(timeout)));
        ESP_LOGI(TAG, "health_timeout_timer set"); 
    }
}
#if 0
FREERTOS_HZ 100/1000
LOG_DEFAULT_LEVEL 
#endif



static void health_timer_callback(void* arg)
{
    Motion_Packet_t payload = {0};                
    if(target_ble == 2)
    {
        payload.event_code = MANUFACT2_REQUEST;

        ble_gattc_write_no_rsp_flat(peer_conn_handle, peer_write_val_handle, &payload, sizeof(payload));
    }
    else{
        payload.event_code = HEALTH_DATA_REQUEST;

        ble_gattc_write_no_rsp_flat(peer_conn_handle, peer_write_val_handle, &payload, sizeof(payload));
    }


}

static void ping_timer_callback(void* arg)
{
    if(is_connecting == false)
    {
        APP_String_printf("NO CONNECT\r\n");
        return;  
    }
    if(ping_rx_count == 0)
    {
        APP_String_printf("TIMEOUT\r\n");
        return;
    }
    if(target_ble == 2)
    {
        APP_String_printf("FOUNTO\r\n");
        APP_String_printf("Manufacture Date:%08x\r\n",  __builtin_bswap32(menufact1_Packet.manufact_res.Manufacture_date));
        APP_String_printf("MAC Address:%02X%02X%02X%02X%02X%02X\r\n",
                        menufact1_Packet.manufact_res.mac[0], menufact1_Packet.manufact_res.mac[1],
                        menufact1_Packet.manufact_res.mac[2], menufact1_Packet.manufact_res.mac[3],
                        menufact1_Packet.manufact_res.mac[4], menufact1_Packet.manufact_res.mac[5]);
        APP_String_printf("On Time:%d\r\n",( menufact1_Packet.manufact_res.active_time));
        APP_String_printf("Motor On Time:%u\r\n", menufact1_Packet.manufact_res.battery_time);
                            
        APP_String_printf("Active Time:%08x\r\n",__builtin_bswap32( menufact2_Packet.manufact_res.active_time));
        APP_String_printf("Used:%u\r\n", menufact2_Packet.manufact_res.battery_time);
    }
    else if(target_ble == 1){
        APP_String_printf("PALLO\r\n");
        APP_String_printf("Manufacture Date:%08x\r\n",  __builtin_bswap32(menufact1_Packet.manufact_res.Manufacture_date));
        APP_String_printf("MAC Address:%02X%02X%02X%02X%02X%02X\r\n",
                        menufact1_Packet.manufact_res.mac[0], menufact1_Packet.manufact_res.mac[1],
                        menufact1_Packet.manufact_res.mac[2], menufact1_Packet.manufact_res.mac[3],
                        menufact1_Packet.manufact_res.mac[4], menufact1_Packet.manufact_res.mac[5]);
        APP_String_printf("Active Time:%08x\r\n",__builtin_bswap32( menufact1_Packet.manufact_res.active_time));
        APP_String_printf("Battery Time:%u\r\n", menufact1_Packet.manufact_res.battery_time);
                            
        APP_String_printf("Bat Level:%u %%\r\n", health_Packet.health_data_res.Bat_Level);
        APP_String_printf("Bat Voltage:%u\r\n",  health_Packet.health_data_res.Bat_Voltage);
                
        APP_String_printf("FW Version:v%u.%u.%u\r\n", health_Packet.health_data_res.major, health_Packet.health_data_res.minor, health_Packet.health_data_res.patch);
        APP_String_printf("Target RSSI:%d\r\n", health_Packet.health_data_res.target_rssi);
        
        // 비트필드 fault_flag 상세 출력
        APP_String_printf("Fault Flag:0x%02X\r\n", health_Packet.health_data_res.fault_flag.byte);
        //APP_String_printf("  |- Bat Status  : %u\r\n", health_Packet.health_data_res.fault_flag.bit.Bat_Status);
        //APP_String_printf("  |- IMU Error   : %u\r\n", health_Packet.health_data_res.fault_flag.bit.IMU_Err);
        //APP_String_printf("  |- BLE Error   : %u\r\n", health_Packet.health_data_res.fault_flag.bit.BLE_Err);
        //APP_String_printf("  |- Storage Err : %u\r\n", health_Packet.health_data_res.fault_flag.bit.storage);
        //APP_String_printf("  |- Reset Reason: %u\r\n", health_Packet.health_data_res.fault_flag.bit.reset_reason);
    }    


    float loss_rate = (1.0f - ((float)ping_rx_count / PING_MAX_SEQ)) * 100.0f;

    APP_String_printf("Ping Test TX:%d\r\n", PING_MAX_SEQ);
    APP_String_printf("Ping Test RX:%d\r\n",ping_rx_count);
    APP_String_printf("Ping Test Loss:%.1f\r\n",loss_rate);
    float rtt_min = 999999.0f;
    float rtt_max = 0.0f;
    float rtt_sum = 0.0f;

    int8_t rssi_min = 127;
    int8_t rssi_max = -128;
    int32_t rssi_sum = 0;

    for (int i = 0; i < PING_MAX_SEQ; i++) {
        if (ping_received[i]) {
            // RTT Min / Max / Sum
            if (ping_rtt_ms[i] < rtt_min) rtt_min = ping_rtt_ms[i];
            if (ping_rtt_ms[i] > rtt_max) rtt_max = ping_rtt_ms[i];
            rtt_sum += ping_rtt_ms[i];

            // RSSI Min / Max / Sum
            if (ping_rssi[i] < rssi_min) rssi_min = ping_rssi[i];
            if (ping_rssi[i] > rssi_max) rssi_max = ping_rssi[i];
            rssi_sum += ping_rssi[i];
        }
    }

    float rtt_avg = rtt_sum / ping_rx_count;
    float rssi_avg = (float)rssi_sum / ping_rx_count;

    APP_String_printf("RTT Min:%.2f\r\n", rtt_min);
    APP_String_printf("RTT Max:%.2f\r\n", rtt_max);
    APP_String_printf("RTT Avg:%.2f\r\n", rtt_avg);    
    APP_String_printf("RSSI Min:%d\r\n", rssi_min);
    APP_String_printf("RSSI Max:%d\r\n", rssi_max);
    APP_String_printf("RSSI Avg:%.1f\r\n", rssi_avg);                        
}
void ble_jig_main(void)
{
    nimble_port_init();

    // Ping 송신을 제어할 이진 세마포어
    ping_start_sem = xSemaphoreCreateBinary();
    const esp_timer_create_args_t pingtest_args = {
        .callback = &ping_timer_callback,
        .name = "pingtimer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&pingtest_args, &ping_timeout_timer));
    ping_timer_Set(true,10);

    const esp_timer_create_args_t health_args = {
        .callback = &health_timer_callback,
        .name = "health_args"
    };
    ESP_ERROR_CHECK(esp_timer_create(&health_args, &health_timeout_timer));



    // Ping 60Hz 송신 전용 태스크 (우선순위 5)
    xTaskCreate(ble_ping_60hz_task, "ping_tx_task", 4096, NULL, 5, NULL);

    ble_hs_cfg.sync_cb = ble_spp_client_on_sync;
    peer_init(1, 64, 64, 64);
    
    nimble_port_freertos_init(ble_host_task);
}
