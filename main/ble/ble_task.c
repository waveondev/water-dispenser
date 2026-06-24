#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* NimBLE 필수 헤더 */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_ONLY";

#define DEVICE_NAME "Wave_test"
#define MY_UUID128_BASE(XX, YY) \
    BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
                        0xF3, 0x93, 0xB5, 0xA3, YY, XX, 0x40, 0x6E)

#define BLE_SVC_UUID128   MY_UUID128_BASE(0x00, 0x01) // 서비스 UUID (0x0001)
#define BLE_CHR_1_UUID128 MY_UUID128_BASE(0x00, 0x03) // CH_APPLY (0x0003)
#define BLE_CHR_2_UUID128 MY_UUID128_BASE(0x00, 0x02) // CH_RESULT (0x0002)

static uint8_t own_addr_type;
static uint16_t ble_spp_svc_gatt_notify_val_handle;
static bool conn_handle_subs[CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1];

extern void ble_store_config_init(void);
static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg);

// 큐로 주고받을 데이터 구조체
typedef struct {
    uint16_t len;
    uint8_t data[128]; 
} ble_data_msg_t;
static uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool is_phone_connected = false;
static QueueHandle_t ble_rx_queue = NULL;
static QueueHandle_t ble_tx_queue = NULL; // 🔴 이름을 수신용(rx)에서 송신용(tx) 개념으로

static void ble_spp_server_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=", desc->conn_handle, desc->our_ota_addr.type);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=", desc->peer_id_addr.type);
    MODLOG_DFLT(INFO, " encrypted=%d authenticated=%d bonded=%d\n", 
                desc->sec_state.encrypted, desc->sec_state.authenticated, desc->sec_state.bonded);
}

/**
 * [추가] 주변 비콘 스캔 시작 함수
 */
static void ble_spp_client_scan(void)
{
    struct ble_gap_disc_params disc_params;
    int rc;

    memset(&disc_params, 0, sizeof(disc_params));
    disc_params.filter_duplicates = 0; 
    disc_params.passive = 0;           // Active 스캔 필수 (이름 요청용)

    // 🔴 [수정] 500ms 비콘 스캔을 위한 타이밍 최적화
    // interval과 window를 같게 설정하면 ESP32가 쉬지 않고 100% 확률로 계속 스캔 대기를 합니다.
    disc_params.itvl = 400;            // 스캔 주기 (단위: 0.625ms, 즉 250ms)
    disc_params.window = 400;          // 스캔 윈도우 (단위: 0.625ms, 즉 250ms) -> 100% 듀티 사이클

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, ble_spp_server_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error discovering peers; rc=%d\n", rc);
    }
}

/**
 * BLE Advertising(신호 브로드캐스팅) 시작 함수
 */
static void ble_spp_server_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields; 
    int rc;

    // 1. 기본 어드버타이징 패킷 설정 (Flags + UUID)
    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    ble_uuid128_t adv_uuid = *(ble_uuid128_t *)BLE_SVC_UUID128;
    fields.uuids128 = &adv_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    // 2. 스캔 응답 패킷 설정 (기기 이름 격리)
    memset(&rsp_fields, 0, sizeof rsp_fields);
    const char *name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields); 
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting scan response data; rc=%d\n", rc);
        return;
    }

    // 3. 광고 시작
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_spp_server_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
    }
}

/**
 * NimBLE GAP 이벤트 콜백 핸들러 (연결 상태 감시 + [추가] 비콘 스캔 처리)
 */
static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        MODLOG_DFLT(INFO, "connection %s; status=%d \n",
                    event->connect.status == 0 ? "established" : "failed", event->connect.status);
        if (event->connect.status == 0) {
            current_conn_handle = event->connect.conn_handle;
            is_phone_connected = true;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                ble_spp_server_print_conn_desc(&desc);
            }
        }
        if (event->connect.status != 0 || CONFIG_BT_NIMBLE_MAX_CONNECTIONS > 1) {
            ble_spp_server_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d \n", event->disconnect.reason);
        current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        is_phone_connected = false;
        if (event->disconnect.conn.conn_handle <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            conn_handle_subs[event->disconnect.conn.conn_handle] = false;
        }
        ble_spp_server_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        MODLOG_DFLT(INFO, "subscribe event; conn_handle=%d attr_handle=%d \n",
                    event->subscribe.conn_handle, event->subscribe.attr_handle);
        if (event->subscribe.conn_handle <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            conn_handle_subs[event->subscribe.conn_handle] = true;
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_spp_server_advertise();
        return 0;

    // 🔴 [추가] 주변에서 비콘 패킷을 가로챘을 때 일어나는 이벤트
    case BLE_GAP_EVENT_DISC:
    {
        // 주변 기기 정보를 통합 저장할 저장소 (최대 20개)
        typedef struct {
            uint8_t addr[6];
            char name[32];
            int8_t rssi;
        } dev_info_t;
        
        static dev_info_t dev_list[20];
        static int dev_count = 0;

        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        // 1. MAC 주소 기준으로 기존 리스트 검색 및 등록 (이름 캐싱용)
        int idx = -1;
        for (int i = 0; i < dev_count; i++) {
            if (memcmp(dev_list[i].addr, event->disc.addr.val, 6) == 0) {
                idx = i;
                break;
            }
        }

        if (idx == -1) {
            if (dev_count < 20) {
                idx = dev_count++;
            } else {
                static int rolling_idx = 0;
                idx = rolling_idx;
                rolling_idx = (rolling_idx + 1) % 20;
            }
            memset(&dev_list[idx], 0, sizeof(dev_info_t));
            memcpy(dev_list[idx].addr, event->disc.addr.val, 6);
            strcpy(dev_list[idx].name, "Unknown (No Name)");
        }

        // 이름 패킷이 들어왔을 때만 캐시 업데이트
        if (fields.name != NULL && fields.name_len > 0) {
            int len = (fields.name_len > 31) ? 31 : fields.name_len;
            memcpy(dev_list[idx].name, fields.name, len);
            dev_list[idx].name[len] = '\0';
        }

        // 2. ⭐️ [핵심 필터] 캐시된 이름이 "Wave_Tracker"인 기기만 처리
        if (strcmp(dev_list[idx].name, "Wave_Tracker") == 0) {
            
            // 💡 16-bit Service Data가 들어왔는지 확인
            if (fields.svc_data_uuid16 != NULL && fields.svc_data_uuid16_len > 0) {
                
                // 패킷 구조상 앞의 2바이트는 서비스 UUID(0x1234) 자체를 나타냅니다.
                // 리틀 엔디안 형태이므로 [0]=0x34, [1]=0x12 구조를 띱니다.
                uint16_t svc_uuid = (fields.svc_data_uuid16[1] << 8) | fields.svc_data_uuid16[0];
                
                // 우리가 찾는 0x1234 서비스 데이터가 맞는지 검증합니다.
                if (svc_uuid == 0x1234) {
                    
                    // 실제 데이터는 서비스 UUID(2바이트)를 제외한 그 뒤 영역입니다.
                    const uint8_t *actual_data = fields.svc_data_uuid16 + 2;
                    int actual_data_len = fields.svc_data_uuid16_len - 2;

                    printf("\n🎯 [Wave_Tracker] Service Data (0x1234) 포착 🎯\n");
                    printf("MAC  : %02X:%02X:%02X:%02X:%02X:%02X (%d dBm)\n",
                           event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                           event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0],
                           event->disc.rssi);

                    // 16진수 바이트 출력
                    if (actual_data_len > 0) {
                        printf("HEX  : ");
                        for (int i = 0; i < actual_data_len; i++) {
                            printf("%02X ", actual_data[i]);
                        }
                        printf("\n");

                        // 아스키 문자열 출력
                        printf("STR  : ");
                        for (int i = 0; i < actual_data_len; i++) {
                            char c = actual_data[i];
                            printf("%c", (c >= 32 && c <= 126) ? c : '.');
                        }
                        printf("\n");
                    } else {
                        printf("HEX  : 서비스 데이터 페이로드가 비어있음\n");
                    }
                    printf("-------------------------------------\n");
                }
            }
        }

        return 0;
    }

    default:
        return 0;
    }
}

static void ble_spp_server_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void ble_spp_server_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    // 내 신호 전송 시작
    ble_spp_server_advertise();

    // 🔴 [추가] 동기화 완료 후 비콘 스캔 엔진 가동
    ble_spp_client_scan();
}

void ble_spp_server_host_task(void *param)
{
    MODLOG_DFLT(INFO, "BLE Host Task Started\n");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * GATT 캐릭터리스틱 처리 (중괄호 블록을 넣어 컴파일 에러 완전 수정)
 */
static int ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR: 
    { // 🟢 중괄호 추가로 switch-unreachable 변수선언 문제 해결
        uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
        
        if (data_len > 0 && ble_rx_queue != NULL) {
            ble_data_msg_t msg;
            msg.len = (data_len > 128) ? 128 : data_len; 
            
            ble_hs_mbuf_to_flat(ctxt->om, msg.data, msg.len, NULL);
            xQueueSend(ble_rx_queue, &msg, 0);
        }
        break;
    } // 🟢 블록 마감

    default:
        break;
    }
    return 0;
}

static const struct ble_gatt_svc_def new_ble_svc_gatt_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_SVC_UUID128,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_CHR_1_UUID128,
                .access_cb = ble_svc_gatt_handler,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_CHR_2_UUID128,
                .access_cb = ble_svc_gatt_handler,
                .val_handle = &ble_spp_svc_gatt_notify_val_handle, 
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 } 
        },
    },
    { 0 }, 
};

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with def_handle=%d val_handle=%d\n", ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    default:
        break;
    }
}

int gatt_svr_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(new_ble_svc_gatt_defs);
    if (rc != 0) return rc;

    rc = ble_gatts_add_svcs(new_ble_svc_gatt_defs);
    if (rc != 0) return rc;

    return 0;
}

void ble_server_send_notify(uint16_t conn_handle, uint8_t *data, uint16_t len)
{
    struct os_mbuf *om;
    om = ble_hs_mbuf_from_flat(data, len);
    if (om != NULL) {
        ble_gatts_notify_custom(conn_handle, ble_spp_svc_gatt_notify_val_handle, om);
    }
}
/**
 * @brief 스마트폰으로 보낼 데이터를 BLE 송신 큐에 집어넣는 함수
 * * @param data 보낼 데이터의 바이트 배열 포인터
 * @param len  보낼 데이터의 길이 (바이트 단위)
 * @return true 큐에 성공적으로 진입함, false 큐가 가득 찼거나 비활성화 상태
 */
bool ble_send_data_to_queue(const uint8_t *data, uint16_t len)
{
    // 1. 안전 장치: 데이터가 없거나 길이가 0인 경우 차단
    if (data == NULL || len == 0) {
        return false;
    }

    // 2. 안전 장치: 큐가 생성되지 않은 상태라면 차단
    if (ble_tx_queue == NULL) {
        printf("[BLE TX FUNC] 에러: ble_tx_queue가 초기화되지 않았습니다.\n");
        return false;
    }

    ble_data_msg_t my_packet;

    // 3. 방어 코드: 구조체 배열 크기(128바이트)를 넘지 않도록 길이 제한
    if (len > 128) {
        my_packet.len = 128;
        printf("[BLE TX FUNC] 경고: 데이터 크기가 128바이트를 초과하여 잘라냅니다.\n");
    } else {
        my_packet.len = len;
    }

    // 4. 데이터 실제 복사
    memcpy(my_packet.data, data, my_packet.len);

    // 5. 큐로 전송 (포트 대기 시간은 0으로 설정하여 현재 태스크가 멈추지 않게 함)
    if (xQueueSend(ble_tx_queue, &my_packet, 0) == pdTRUE) {
        return true;
    } else {
        printf("[BLE TX FUNC] 에러: 송신 큐가 가득 차서 데이터를 넣지 못했습니다.\n");
        return false;
    }
}
static void ble_rx_processing_task(void *pvParameters)
{
    ble_data_msg_t msg;
    MODLOG_DFLT(INFO, "사용자 데이터 처리 태스크 시작\n");

    while (1) {
        if (xQueueReceive(ble_rx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            printf("[새 태스크] %d 바이트 데이터 처리 중: ", msg.len);
            for (int i = 0; i < msg.len; i++) {
                printf("%02X ", msg.data[i]);
            }
            printf("\n");
        }
    }
}
static void ble_tx_processing_task(void *pvParameters)
{
    ble_data_msg_t msg;
    MODLOG_DFLT(INFO, "BLE 송신 전송 전담 태스크 시작\n");

    while (1) {
        // 내가 폰으로 보낼 데이터가 큐에 들어올 때까지 대기
        if (xQueueReceive(ble_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (is_phone_connected && current_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                ble_server_send_notify(current_conn_handle, msg.data, msg.len);
                printf("[TX 태스크] 스마트폰으로 %d 바이트 Notify 전송 완료\n", msg.len);
            } else {
                printf("[TX 태스크] 경고: 스마트폰이 연결되어 있지 않아 전송 취소\n");
            }
        }
    }
}
void ble_task_init(void)
{
    esp_err_t ret;
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        MODLOG_DFLT(ERROR, "Failed to init nimble %d \n", ret);
        return;
    }

    for (int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
        conn_handle_subs[i] = false;
    }

    ble_hs_cfg.reset_cb = ble_spp_server_on_reset;
    ble_hs_cfg.sync_cb = ble_spp_server_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    #ifndef CONFIG_EXAMPLE_IO_TYPE
    #define CONFIG_EXAMPLE_IO_TYPE 3 
    #endif
    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;

    assert(gatt_svr_init() == 0);
    assert(ble_svc_gap_device_name_set(DEVICE_NAME) == 0);

    ble_store_config_init();

    ble_rx_queue = xQueueCreate(10, sizeof(ble_data_msg_t));
    ble_tx_queue = xQueueCreate(10, sizeof(ble_data_msg_t));

    // xTaskCreate 대신 xTaskCreatePinnedToCore를 사용합니다.
    if (xTaskCreatePinnedToCore(
            ble_rx_processing_task,                  // 태스크 함수
            "ble_rx_task",                // 태스크 이름
            4096,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 3,      // 우선순위
            NULL,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
                ESP_LOGE(TAG, "Error creating ble_rx_task on Core 1");
    }

    if (xTaskCreatePinnedToCore(
            ble_tx_processing_task,                  // 태스크 함수
            "ble_tx_task",                // 태스크 이름
            4096,       // 스택 크기
            NULL,        // 파라미터
            tskIDLE_PRIORITY + 3,      // 우선순위
            NULL,                  // 태스크 핸들
            1                          // ⭐ 코어 ID (1번 코어 = APP_CPU)
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
              ESP_LOGE(TAG, "Error creating ble_tx_task on Core 1");
    }

    nimble_port_freertos_init(ble_spp_server_host_task);
}