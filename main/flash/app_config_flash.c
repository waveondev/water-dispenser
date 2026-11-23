#include "app_config_flash.h"
#include "esp_log.h"
#include "opmode_task.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_button.h"
#include "esp_timer.h"  // 👈 이 줄을 추가해 주세요!
#include "ble_parse.h"
#include "ble_task.h"
#include "esp_mac.h"
static const char *TAG = __FILE__;
static esp_timer_handle_t minute10_timer;

#define TIMER_10_MIN_IN_US   (10ULL * 60ULL * 1000000ULL)

static void save_facto_configuration(void);
app_config_t app_config = 
{
    .op_mode = OP_MODE_NORMAL,
    .pump_clean_duration = 180,
    .filter_life_days = 30,
    .moter_life_days = 60,    
    .min_weight_threshold = 800,
    .splash_delta_g = 100,
    .gate_way_rssi_th = -85,
    .hx1_scale = 1000.0f,
    .hx1_offset = 0,
    .case_raw_data = 0,
    .tof_sense_threshold_l = 50,
    .tof_sense_threshold_r = 50,
    .motion_data_time = 60,
    .EFFECTIVE_DWELL_TIME = 5,
    .env_mode = "prod"
};

app_wifi_config_t wifi_config = 
{
   .conn_ssid = "",
   .conn_password = ""
};

app_facto_config_t facto_config = 
{
    .hx1_scale = 1000.0f,
    .case_raw_data = 0,
    .tof_sense_cal_l = 0,
    .tof_sense_cal_r = 0,
    .Device_Serial = ""
};

static bool app_save_flag = false;
static bool wifi_save_flag = false;
static bool facto_save_flag = false;
static bool motor_save_flag = false;
static bool filter_save_flag = false;
static bool ontime_save_flag = false;
static bool motor_ontime_save_flag = false;
static bool rtc_init_save_flag = false;
static uint32_t motor_save_time = 0;
static uint32_t filter_save_time = 0;
static uint32_t motor_save_time_buf = 0;
static uint32_t filter_save_time_buf = 0;
static uint32_t ontime_save = 0;
static uint32_t motor_on_time_save = 0;
static uint32_t active_time = 0;
static uint32_t factory_days = 0;
static uint32_t date_buf = 0;
void app_nvs_save_set(void)
{
    app_save_flag = true;
}
void wifi_nvs_save_set(void)
{
    wifi_save_flag = true;
}
void facto_nvs_save_set(void)
{
    facto_save_flag = true;
}
void motor_nvs_save_set(void)
{
    motor_save_flag = true;
}
void filter_nvs_save_set(void)
{
    filter_save_flag = true;
}
void ontime_nvs_save_set(void)
{
    ontime_save_flag = true;
}
void motor_ontime_nvs_save_set(void)
{
    motor_ontime_save_flag = true;
}

void rtc_init_nvs_save_set(uint32_t date)
{
    date_buf = date;
    rtc_init_save_flag = true;
}



void ManuFact1_reqeust(Motion_Packet_t *Motion_Packet)
{
    memset(Motion_Packet,0,sizeof(Motion_Packet_t));
    Motion_Packet->event_code = MANUFACT1_RESPONSE;
    Motion_Packet->manufact_res.Manufacture_date = __builtin_bswap32(factory_days);


    uint8_t mac[6] = {0};

    // BLE 광고 MAC 주소와 일치시키려면 ESP_MAC_BT 사용 (Wi-Fi MAC과 마지막 바이트가 +2 다름)
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE("MAC", "Failed to read MAC address: %s", esp_err_to_name(ret));
        return;
    }

    // ESP32는 이미 Big-Endian(MSB first)이므로 [5 - i]가 아니라 [i] 그대로 대입
    for (int i = 0; i < 6; i++)
    {
        Motion_Packet->manufact_res.mac[i] = mac[i];
    }
    Motion_Packet->manufact_res.major = WATER_MAJOR;
    Motion_Packet->manufact_res.minor = WATER_MINOR;
    Motion_Packet->manufact_res.patch = WATER_PATCH;


    Motion_Packet->manufact_res.battery_time = (uint16_t)motor_on_time_save;
    Motion_Packet->manufact_res.active_time = (ontime_save);

    ble_send_data_to_queue(NULL,NULL,(uint8_t*)Motion_Packet,sizeof(Motion_Packet_t));  
}


void ManuFact2_reqeust(Motion_Packet_t *Motion_Packet)
{
    memset(Motion_Packet,0,sizeof(Motion_Packet_t));
    Motion_Packet->event_code = MANUFACT2_RESPONSE;
    Motion_Packet->manufact_res.Manufacture_date = __builtin_bswap32(factory_days);


    uint8_t mac[6] = {0};

    // BLE 광고 MAC 주소와 일치시키려면 ESP_MAC_BT 사용 (Wi-Fi MAC과 마지막 바이트가 +2 다름)
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE("MAC", "Failed to read MAC address: %s", esp_err_to_name(ret));
        return;
    }

    // ESP32는 이미 Big-Endian(MSB first)이므로 [5 - i]가 아니라 [i] 그대로 대입
    for (int i = 0; i < 6; i++)
    {
        Motion_Packet->manufact_res.mac[i] = mac[i];
    }
    Motion_Packet->manufact_res.major = WATER_MAJOR;
    Motion_Packet->manufact_res.minor = WATER_MINOR;
    Motion_Packet->manufact_res.patch = WATER_PATCH;


    Motion_Packet->manufact_res.battery_time = read_nvs_motor_en_flag() ? 1:0;
    Motion_Packet->manufact_res.active_time = __builtin_bswap32(active_time);

    ble_send_data_to_queue(NULL,NULL,(uint8_t*)Motion_Packet,sizeof(Motion_Packet_t));  
}


void factory_erase_enable(Motion_Packet_t *Motion_Packet)
{
  uint8_t ret = 0;
  if(Motion_Packet->factory_req.Manufacture_date != __builtin_bswap32(factory_days))  
  {
    ret |= 1<<0;
  }
  if(Motion_Packet->factory_req.major != WATER_MAJOR)
  {
    ret |= 1<<1;
  }
  if(Motion_Packet->factory_req.minor != WATER_MINOR)
  {
    ret |= 1<<2;
  }
  if(Motion_Packet->factory_req.patch != WATER_PATCH)
  {
    ret |= 1<<3;
  }
  if(ret == 0)
  {
    set_factory_flag(FACTORY_MENUFACT);
  }
  else
  {
    memset(Motion_Packet,0,sizeof(Motion_Packet_t));
    Motion_Packet->event_code = FACTORY_RESPONSE;
    Motion_Packet->factory_res.ret = ret;
    ble_send_data_to_queue(NULL,NULL,(uint8_t*)Motion_Packet,sizeof(Motion_Packet_t));  

  }
}



void reset_all_nvs_data(void)
{
    ESP_LOGW("NVS", APP_NAMESPACE " NVS 데이터 영역을 초기화합니다...");
    
    nvs_handle_t handle;
    
    // 1. WAVEON 네임스페이스를 쓰기 모드로 엽니다.
    esp_err_t err = nvs_open(APP_NAMESPACE, NVS_READWRITE, &handle);
    
    if (err == ESP_OK) {
        // 2. WAVEON 네임스페이스 내부의 모든 Key를 지웁니다.
        err = nvs_erase_all(handle);
        
        if (err == ESP_OK) {
            // 3. 변경 사항을 플래시에 반영(커밋)합니다.
            err = nvs_commit(handle);
        }
        
        // 4. 핸들을 닫아 메모리를 해제합니다.
        nvs_close(handle);
    }

    // 결과 처리 및 재부팅
    if (err == ESP_OK) {
        ESP_LOGI("NVS", APP_NAMESPACE"초기화 완료! 변경사항을 적용하기 위해 재부팅합니다.");

    } else {
        ESP_LOGE("NVS", APP_NAMESPACE"NVS 초기화 실패: %s", esp_err_to_name(err));
    }
}
void reset_all_nvs_menu(void)
{
    ESP_LOGW("NVS", MENUFACT_NAMESPACE" NVS 데이터 영역을 초기화합니다...");
    
    nvs_handle_t handle;
    
    // 1. WAVEON 네임스페이스를 쓰기 모드로 엽니다.
    esp_err_t err = nvs_open(MENUFACT_NAMESPACE, NVS_READWRITE, &handle);
    
    if (err == ESP_OK) {
        // 2. WAVEON 네임스페이스 내부의 모든 Key를 지웁니다.
        err = nvs_erase_all(handle);
        
        if (err == ESP_OK) {
            // 3. 변경 사항을 플래시에 반영(커밋)합니다.
            err = nvs_commit(handle);
        }
        
        // 4. 핸들을 닫아 메모리를 해제합니다.
        nvs_close(handle);
    }

    // 결과 처리 및 재부팅
    if (err == ESP_OK) {
        ESP_LOGI("NVS", MENUFACT_NAMESPACE "초기화 완료! 변경사항을 적용하기 위해 재부팅합니다.");

    } else {
        ESP_LOGE("NVS", MENUFACT_NAMESPACE " NVS 초기화 실패: %s", esp_err_to_name(err));
    }
}
void reset_all_nvs_facto(void)
{

    ESP_LOGI(TAG, "Starting NVS Reset Sequence...");

    // 1. NVS 전체 삭제 (Erase)
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS Partition Erased.");
    } else {
        ESP_LOGE(TAG, "NVS Erase Failed: %s", esp_err_to_name(err));
    }

    // 2. NVS 재초기화 (Init)
    err = nvs_flash_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS Partition Re-initialized.");
    } else {
        ESP_LOGE(TAG, "NVS Init Failed: %s", esp_err_to_name(err));
    }

    // 3. 재부팅 전 로그 출력 및 지연 (로그 유실 방지용 1초 대기)
    ESP_LOGI(TAG, "Restarting ESP32 in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4. 소프트웨어 재부팅 (ESP Restart)
    esp_restart();    
}
#include "esp_flash.h"
#include "esp_log.h"
#include <stdint.h>

#define PRODUCTION_FLASH_ADDR    0x20FFF0


uint32_t readProductionDate(void)
{
    esp_err_t ret = esp_flash_read(
        NULL,
        &factory_days,
        PRODUCTION_FLASH_ADDR,
        sizeof(factory_days)
    );

    if(ret != ESP_OK)
    {
        ESP_LOGE("MENUFACT", "Flash read failed: %s",
                 esp_err_to_name(ret));
        return 0;
    }

    ESP_LOGI("MENUFACT", "Production Date: %08lX",
             (unsigned long)factory_days);
        
    return factory_days;
}
void dump_all_configurations(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "         [SYSTEM CONFIGURATION DUMP]              ");
    ESP_LOGI(TAG, "==================================================");

    // 1. APP 설정 출력
    ESP_LOGI(TAG, "[APP CONFIG]");
    ESP_LOGI(TAG, "  - Operation Mode       : %ld", app_config.op_mode);
    ESP_LOGI(TAG, "  - Pump Clean Duration  : %ld sec", app_config.pump_clean_duration);
    ESP_LOGI(TAG, "  - Filter Life Days     : %ld days", app_config.filter_life_days);
    ESP_LOGI(TAG, "  - Moter Life Days     : %ld days", app_config.moter_life_days);

    ESP_LOGI(TAG, "  - Min Weight Threshold : %ld g", app_config.min_weight_threshold); // 구조체 멤버에 있으면 출력
    ESP_LOGI(TAG, "  - Splash Delta         : %ld g", app_config.splash_delta_g);
    ESP_LOGI(TAG, "  - Gateway RSSI Thr     : %ld dBm", app_config.gate_way_rssi_th);
    ESP_LOGI(TAG, "  - HX1 Scale Factor     : %.2f", app_config.hx1_scale);
    ESP_LOGI(TAG, "  - HX1 Tare Offset      : %ld", app_config.hx1_offset);
    ESP_LOGI(TAG, "  - case_raw_data        : %d", app_config.case_raw_data);
    
    ESP_LOGI(TAG, "  - tof_sense_threshold_l: %ld", app_config.tof_sense_threshold_l);
    ESP_LOGI(TAG, "  - tof_sense_threshold_r: %ld", app_config.tof_sense_threshold_r);

    ESP_LOGI(TAG, "  - motion_data_time     : %ld", app_config.motion_data_time);
    ESP_LOGI(TAG, "  - EFFECTIVE_DWELL_TIME : %ld", app_config.EFFECTIVE_DWELL_TIME);
    ESP_LOGI(TAG, "  - env_mode             : %s", app_config.env_mode);

    
    ESP_LOGI(TAG, "--------------------------------------------------");

    // 2. Wi-Fi 설정 출력
    ESP_LOGI(TAG, "[WIFI CONFIG]");
    // SSID나 PASSWORD가 비어있으면 [EMPTY]로 센스있게 표기
    ESP_LOGI(TAG, "  - Wi-Fi SSID           : %s", (wifi_config.conn_ssid[0] == '\0') ? "[EMPTY]" : (char*)wifi_config.conn_ssid);
    ESP_LOGI(TAG, "  - Wi-Fi Password       : %s", (wifi_config.conn_password[0] == '\0') ? "[EMPTY]" : "********"); // 보안상 별표 표기 (원하시면 %s로 생자로 까셔도 됩니다)
    ESP_LOGI(TAG, "--------------------------------------------------");

    ESP_LOGI(TAG, "==================================================");
// 3. BLE 설정 출력
    ESP_LOGI(TAG, "[MOTOR]");
    ESP_LOGI(TAG, "  - MOTOR      : %d", motor_save_time);
    
    ESP_LOGI(TAG, "==================================================");
    // 3. BLE 설정 출력
    ESP_LOGI(TAG, "[FILTER]");
    ESP_LOGI(TAG, "  - FILTER      : %d", filter_save_time);
    ESP_LOGI(TAG, "==================================================");    

    ESP_LOGI(TAG, "[MENUFACT]");
    // SSID나 PASSWORD가 비어있으면 [EMPTY]로 센스있게 표기

    ESP_LOGI(TAG, "  - ontime_save  : %d", ontime_save);
    ESP_LOGI(TAG, "  - motor_on_time_save : %d", motor_on_time_save);
    ESP_LOGI(TAG, "  - active_time : %08x", active_time);

    ESP_LOGI(TAG, "--------------------------------------------------");

    ESP_LOGI(TAG, "[motor_en] = %s",read_nvs_motor_en_flag()?"true":"false" );
    
    
    
    ESP_LOGI(TAG, "[FACTO CONFIG]");
    // SSID나 PASSWORD가 비어있으면 [EMPTY]로 센스있게 표기

    ESP_LOGI(TAG, "  - facto scale          : %.2f", facto_config.hx1_scale);
    ESP_LOGI(TAG, "  - case_raw_data        : %d", facto_config.case_raw_data);
    ESP_LOGI(TAG, "  - tof_sense_cal_l      : %d", facto_config.tof_sense_cal_l);
    ESP_LOGI(TAG, "  - tof_sense_cal_r      : %d", facto_config.tof_sense_cal_r);
    ESP_LOGI(TAG, "  - facto serial         : %s", (facto_config.Device_Serial[0] == '\0') ? "[EMPTY]" : (char*)facto_config.Device_Serial);
    ESP_LOGI(TAG, "--------------------------------------------------");

    ESP_LOGI(TAG, "[Registation] = %s",read_nvs_registration_flag()?"true":"false" );
    
    readProductionDate();
}

app_config_t* get_app_config(void)
{
    return &app_config;
}

app_wifi_config_t* get_wifi_config(void)
{
    return &wifi_config;
}

app_facto_config_t* get_facto_config(void)
{
    return &facto_config;
}
uint32_t* get_motor_time(void)
{
    return &motor_save_time;
}
uint32_t* get_filter_time(void)
{
    return &filter_save_time;
}

void erase_app_configuration(void)
{
    // 1. NVS에서 시스템 구조체 통째로 읽어오기 시도
    memset(&app_config,0,sizeof(app_config));
    write_nvs_blob(APP_NAMESPACE, APP_KEY_CONFIGURATION, &app_config, sizeof(app_config));
}

void load_app_configuration(void)
{
    // 1. NVS에서 시스템 구조체 통째로 읽어오기 시도
    esp_err_t err = read_nvs_blob(APP_NAMESPACE, APP_KEY_CONFIGURATION, &app_config, sizeof(app_config_t));

    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[CONFIG] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
               
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_blob(APP_NAMESPACE, APP_KEY_CONFIGURATION, &app_config, sizeof(app_config_t));
    } else {
        ESP_LOGI(TAG,"[CONFIG] NVS에서 시스템 설정 로드 성공! (opmode = %d 저울 Offset: %d)\r\n", 
                          app_config.op_mode, app_config.hx1_offset);
    }
    app_config.case_raw_data = facto_config.case_raw_data;
    app_config.hx1_scale = facto_config.hx1_scale;
}

// 값이 바뀔 때마다 호출해 줄 저장 함수
static void save_app_configuration(void)
{
    write_nvs_blob(APP_NAMESPACE, APP_KEY_CONFIGURATION, &app_config, sizeof(app_config_t));
// 2. 검증을 위해 NVS에서 데이터를 다시 읽어올 임시 그릇 생성
    app_config_t temp_cfg;
    memset(&temp_cfg, 0, sizeof(app_config_t)); // 0으로 깨끗하게 청소

    // 3. NVS에서 방금 저장한 값을 다시 로드(Load)
    esp_err_t err = read_nvs_blob(APP_NAMESPACE, APP_KEY_CONFIGURATION, &temp_cfg, sizeof(app_config_t));

    if (err == ESP_OK) {
        // 4. memcmp로 원본(app_config)과 NVS에서 읽어온 값(temp_cfg)을 비교
        // 두 메모리 블록이 100% 일치하면 0을 리턴합니다.
        if (memcmp(&app_config, &temp_cfg, sizeof(app_config_t)) == 0) {
            ESP_LOGI(TAG, "[CONFIG] NVS 데이터 검증 성공! 저장된 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[CONFIG] 로드된 모드: %ld, 저울 Offset: %ld", 
                      temp_cfg.op_mode, temp_cfg.hx1_offset);
        } else {
            // 플래시 메모리 물리적 손상이나 섹터 오류 시 감지됨
            ESP_LOGE(TAG, "[CONFIG]  NVS 데이터 검증 실패! 저장된 값이 원본과 일치하지 않습니다!");
        }
    } else {
        ESP_LOGE(TAG, "[CONFIG] 검증을 위해 데이터를 읽어오는 중 에러 발생 (%s)", esp_err_to_name(err));
    }
}

void erase_wifi_configuration(void)
{
    // 1. NVS에서 시스템 구조체 통째로 읽어오기 시도
    memset(&wifi_config,0,sizeof(wifi_config));

    write_nvs_blob(APP_NAMESPACE, APP_KEY_WIFI_CONFIG, &wifi_config, sizeof(wifi_config));
}
void load_wifi_configuration(void)
{
    // 1. NVS에서 시스템 구조체 통째로 읽어오기 시도
    esp_err_t err = read_nvs_blob(APP_NAMESPACE, APP_KEY_WIFI_CONFIG, &wifi_config, sizeof(app_wifi_config_t));
    
    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[WIFI] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_blob(APP_NAMESPACE, APP_KEY_WIFI_CONFIG, &wifi_config, sizeof(app_wifi_config_t));
    } else {
        ESP_LOGI(TAG,"[WIFI] NVS에서 시스템 설정 로드 성공! (ssid = %s, pass = %s)\r\n", 
                          wifi_config.conn_ssid, wifi_config.conn_password);
    }
}

// 값이 바뀔 때마다 호출해 줄 저장 함수
static void save_wifi_configuration(void)
{
    write_nvs_blob(APP_NAMESPACE, APP_KEY_WIFI_CONFIG, &wifi_config, sizeof(app_wifi_config_t));
// 2. 검증을 위해 NVS에서 데이터를 다시 읽어올 임시 그릇 생성
    app_wifi_config_t temp_cfg;
    memset(&temp_cfg, 0, sizeof(app_wifi_config_t)); // 0으로 깨끗하게 청소

    // 3. NVS에서 방금 저장한 값을 다시 로드(Load)
    esp_err_t err = read_nvs_blob(APP_NAMESPACE, APP_KEY_WIFI_CONFIG, &temp_cfg, sizeof(app_wifi_config_t));

    if (err == ESP_OK) {
        // 4. memcmp로 원본(wifi_config)과 NVS에서 읽어온 값(temp_cfg)을 비교
        // 두 메모리 블록이 100% 일치하면 0을 리턴합니다.
        if (memcmp(&wifi_config, &temp_cfg, sizeof(app_wifi_config_t)) == 0) {
            ESP_LOGI(TAG, "[WIFI] NVS 데이터 검증 성공! 저장된 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[WIFI] 로드된 SSID: %s", temp_cfg.conn_ssid);
        } else {
            // 플래시 메모리 섹터 불량이나 마스킹 오류 시 감지됨
            ESP_LOGE(TAG, "[WIFI] NVS 데이터 검증 실패! 저장된 값이 원본과 일치하지 않습니다!");
        }
    } else {
        ESP_LOGE(TAG, "[WIFI] 검증을 위해 데이터를 읽어오는 중 에러 발생 (%s)", esp_err_to_name(err));
    }
}


void erase_facto_configuration(void)
{
    // 1. NVS에서 시스템 구조체 통째로 읽어오기 시도
    memset(&facto_config,0,sizeof(facto_config));
    facto_config.hx1_scale = 1000.0f;
    write_nvs_blob(FACTORY_NAMESPACE, FACTORY_KEY, &facto_config, sizeof(facto_config));
}
void load_facto_configuration(void)
{
    // 1. NVS에서 시스템 구조체 통째로 읽어오기 시도
    esp_err_t err = read_nvs_blob(FACTORY_NAMESPACE, FACTORY_KEY, &facto_config, sizeof(app_facto_config_t));
    
    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[facto] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_blob(FACTORY_NAMESPACE, FACTORY_KEY, &facto_config, sizeof(app_facto_config_t));
    } else {
        ESP_LOGI(TAG,"[facto] NVS에서 시스템 설정 로드 성공!");
    }
}

// 값이 바뀔 때마다 호출해 줄 저장 함수
static void save_facto_configuration(void)
{
    write_nvs_blob(FACTORY_NAMESPACE, FACTORY_KEY, &facto_config, sizeof(app_facto_config_t));
// 2. 검증을 위해 NVS에서 데이터를 다시 읽어올 임시 그릇 생성
    app_facto_config_t temp_cfg;
    memset(&temp_cfg, 0, sizeof(app_facto_config_t)); // 0으로 깨끗하게 청소

    // 3. NVS에서 방금 저장한 값을 다시 로드(Load)
    esp_err_t err = read_nvs_blob(FACTORY_NAMESPACE, FACTORY_KEY, &temp_cfg, sizeof(app_facto_config_t));

    if (err == ESP_OK) {
        // 두 메모리 블록이 100% 일치하면 0을 리턴합니다.
        if (memcmp(&facto_config, &temp_cfg, sizeof(app_facto_config_t)) == 0) {
            ESP_LOGI(TAG, "[facto] NVS 데이터 검증 성공! 저장된 값이 원본과 100%% 일치합니다.");
        } else {
            // 플래시 메모리 섹터 불량이나 마스킹 오류 시 감지됨
            ESP_LOGE(TAG, "[facto]  NVS 데이터 검증 실패! 저장된 값이 원본과 일치하지 않습니다!");
        }
    } else {
        ESP_LOGE(TAG, "[facto] 검증을 위해 데이터를 읽어오는 중 에러 발생 (%s)", esp_err_to_name(err));
    }
}

void load_rtc_init_time(void)
{
    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ACTIVE, &active_time);

    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[MOTOR] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ACTIVE, active_time);   
    } else {
        ESP_LOGI(TAG,"[active_time] NVS에서 시스템 설정 로드 성공! (active_time = %d)\r\n", 
                          active_time);
    }
}

static void save_rtc_init_time(void)
{
    if(active_time != 0)
        return;

    active_time = date_buf;
    write_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ACTIVE, active_time);
// 2. 검증을 위해 NVS에서 방금 저장한 값을 다시 읽어올 임시 그릇 생성
    uint32_t temp_cfg = 0xffffffff;

    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ACTIVE, &temp_cfg);

    if (err == ESP_OK) {
        // 4. 🔥 memcmp로 원본(ble_config)과 읽어온 것(temp_cfg)을 크기만큼 비교
        // memcmp는 두 메모리가 완전히 일치하면 '0'을 반환합니다.
        if (temp_cfg == active_time) {
            ESP_LOGI(TAG, "[BLE] NVS 데이터 검증 성공! 읽어온 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[BLE] 로드된 이름: %d", active_time);
            
        } else {
            // 메모리가 일치하지 않는 경우 (대개 이런 일은 거의 없지만, 플래시 불량 등의 이슈 체크용)
            ESP_LOGE(TAG, "[BLE]  NVS 데이터 검증 실패! 저장된 값이 원본과 다릅니다!");
        }
    } else {
        ESP_LOGE(TAG, "[BLE] 검증을 위해 다시 읽어오는 과정에서 에러 발생 (%s)", esp_err_to_name(err));
    }
}


void load_motor_ontime_time(void)
{
    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_MOTOR_ON, &motor_on_time_save);

    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[MOTOR] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_MOTOR_ON, motor_on_time_save);   
    } else {
        ESP_LOGI(TAG,"[motor_on_time_save] NVS에서 시스템 설정 로드 성공! (motor_on_time_save = %d)\r\n", 
                          motor_on_time_save);
    }
}

static void save_motor_on_time(void)
{
    if(motor_on_time_save >= 35000)
        return;
    motor_on_time_save++;
    write_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_MOTOR_ON, motor_on_time_save);
// 2. 검증을 위해 NVS에서 방금 저장한 값을 다시 읽어올 임시 그릇 생성
    uint32_t temp_cfg = 0xffffffff;

    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_MOTOR_ON, &temp_cfg);

    if (err == ESP_OK) {
        // 4. 🔥 memcmp로 원본(ble_config)과 읽어온 것(temp_cfg)을 크기만큼 비교
        // memcmp는 두 메모리가 완전히 일치하면 '0'을 반환합니다.
        if (temp_cfg == motor_on_time_save) {
            ESP_LOGI(TAG, "[BLE] NVS 데이터 검증 성공! 읽어온 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[BLE] 로드된 이름: %d", motor_on_time_save);
            
        } else {
            // 메모리가 일치하지 않는 경우 (대개 이런 일은 거의 없지만, 플래시 불량 등의 이슈 체크용)
            ESP_LOGE(TAG, "[BLE]  NVS 데이터 검증 실패! 저장된 값이 원본과 다릅니다!");
        }
    } else {
        ESP_LOGE(TAG, "[BLE] 검증을 위해 다시 읽어오는 과정에서 에러 발생 (%s)", esp_err_to_name(err));
    }
}

void load_ontime_time(void)
{
    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ONTIME, &ontime_save);

    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[MOTOR] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ONTIME, ontime_save);   
    } else {
        ESP_LOGI(TAG,"[ontime] NVS에서 시스템 설정 로드 성공! (ontime_save = %d)\r\n", 
                          ontime_save);
    }
}
static void save_on_time(void)
{
    ontime_save++;
    write_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ONTIME, ontime_save);
// 2. 검증을 위해 NVS에서 방금 저장한 값을 다시 읽어올 임시 그릇 생성
    uint32_t temp_cfg = 0xffffffff;

    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(MENUFACT_NAMESPACE, MENUFACT_ONTIME, &temp_cfg);

    if (err == ESP_OK) {
        // 4. 🔥 memcmp로 원본(ble_config)과 읽어온 것(temp_cfg)을 크기만큼 비교
        // memcmp는 두 메모리가 완전히 일치하면 '0'을 반환합니다.
        if (temp_cfg == ontime_save) {
            ESP_LOGI(TAG, "[BLE] NVS 데이터 검증 성공! 읽어온 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[BLE] 로드된 이름: %d", ontime_save);
            
        } else {
            // 메모리가 일치하지 않는 경우 (대개 이런 일은 거의 없지만, 플래시 불량 등의 이슈 체크용)
            ESP_LOGE(TAG, "[BLE]  NVS 데이터 검증 실패! 저장된 값이 원본과 다릅니다!");
        }
    } else {
        ESP_LOGE(TAG, "[BLE] 검증을 위해 다시 읽어오는 과정에서 에러 발생 (%s)", esp_err_to_name(err));
    }
}

void load_motor_time(void)
{
    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(APP_NAMESPACE, APP_KEY_MOTOR_TIME, &motor_save_time);

    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[MOTOR] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_uint(APP_NAMESPACE, APP_KEY_MOTOR_TIME, motor_save_time);   
    } else {
        ESP_LOGI(TAG,"[MOTOR] NVS에서 시스템 설정 로드 성공! (motor_save_time = %d)\r\n", 
                          motor_save_time);
    }
    motor_save_time_buf = motor_save_time;
}
static void save_motor_time(void)
{
    write_nvs_uint(APP_NAMESPACE, APP_KEY_MOTOR_TIME, motor_save_time);
// 2. 검증을 위해 NVS에서 방금 저장한 값을 다시 읽어올 임시 그릇 생성
    uint32_t temp_cfg = 0xffffffff;

    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(APP_NAMESPACE, APP_KEY_MOTOR_TIME, &temp_cfg);

    if (err == ESP_OK) {
        // 4. 🔥 memcmp로 원본(ble_config)과 읽어온 것(temp_cfg)을 크기만큼 비교
        // memcmp는 두 메모리가 완전히 일치하면 '0'을 반환합니다.
        if (temp_cfg == motor_save_time) {
            ESP_LOGI(TAG, "[BLE] NVS 데이터 검증 성공! 읽어온 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[BLE] 로드된 이름: %d", motor_save_time);
            
        } else {
            // 메모리가 일치하지 않는 경우 (대개 이런 일은 거의 없지만, 플래시 불량 등의 이슈 체크용)
            ESP_LOGE(TAG, "[BLE]  NVS 데이터 검증 실패! 저장된 값이 원본과 다릅니다!");
        }
    } else {
        ESP_LOGE(TAG, "[BLE] 검증을 위해 다시 읽어오는 과정에서 에러 발생 (%s)", esp_err_to_name(err));
    }
}

void load_filter_time(void)
{
    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(APP_NAMESPACE, APP_KEY_FILTER_TIME, &filter_save_time);

    if (err != ESP_OK) {
        // 2. 만약 최초 부팅이라 데이터가 없다면 기본값(Default) 세팅
        ESP_LOGI(TAG,"[FILTER] 저장된 설정이 없어 기본값을 생성합니다.\r\n");
                
        // 기본값 세팅 후 NVS에 최초로 구워두기
        write_nvs_uint(APP_NAMESPACE, APP_KEY_FILTER_TIME, filter_save_time);
    } else {
        ESP_LOGI(TAG,"[FILTER] NVS에서 시스템 설정 로드 성공! (filter_save_time = %d)\r\n", 
                          filter_save_time);
    }
    filter_save_time_buf = filter_save_time;
}
static void save_filter_time(void)
{
    write_nvs_uint(APP_NAMESPACE, APP_KEY_FILTER_TIME, filter_save_time);
// 2. 검증을 위해 NVS에서 방금 저장한 값을 다시 읽어올 임시 그릇 생성
    uint32_t temp_cfg = 0xffffffff;

    // 3. NVS에서 데이터를 다시 역으로 로드(Load)
    esp_err_t err = read_nvs_uint(APP_NAMESPACE, APP_KEY_FILTER_TIME, &temp_cfg);

    if (err == ESP_OK) {
        // 4. 🔥 memcmp로 원본(ble_config)과 읽어온 것(temp_cfg)을 크기만큼 비교
        // memcmp는 두 메모리가 완전히 일치하면 '0'을 반환합니다.
        if (temp_cfg == filter_save_time) {
            ESP_LOGI(TAG, "[FILTER] NVS 데이터 검증 성공! 읽어온 값이 원본과 100%% 일치합니다.");
            ESP_LOGI(TAG, "[FILTER] 로드된 이름: %d", filter_save_time);
        } else {
            // 메모리가 일치하지 않는 경우 (대개 이런 일은 거의 없지만, 플래시 불량 등의 이슈 체크용)
            ESP_LOGE(TAG, "[FILTER] ⚠️ NVS 데이터 검증 실패! 저장된 값이 원본과 다릅니다!");
        }
    } else {
        ESP_LOGE(TAG, "[FILTER] 검증을 위해 다시 읽어오는 과정에서 에러 발생 (%s)", esp_err_to_name(err));
    }
}
#define FLASH_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)

static void flash_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting flash_task ");

    while (1) {

        if(app_save_flag)
        {
            app_save_flag = false;
            save_app_configuration();
        }
        if(wifi_save_flag)
        {
            wifi_save_flag = false;
            save_wifi_configuration();
        }
        if(facto_save_flag)
        {
            facto_save_flag = false;
            save_facto_configuration();
        }
        if(motor_save_flag)
        {
            motor_save_flag = false;
            save_motor_time();
        }
        if(filter_save_flag)
        {
            filter_save_flag = false;
            save_filter_time();
        }
        if(ontime_save_flag)
        {
            ontime_save_flag = false;
            save_on_time();
        }        
        if(motor_ontime_save_flag)
        {
            motor_ontime_save_flag = false;
            save_motor_on_time();
        }        
        if(rtc_init_save_flag)
        {
            rtc_init_save_flag = false;
            save_rtc_init_time();
        }    

        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void NVS_Flash_init(void)
{

    static uint8_t ucParameterToPass;
    if (xTaskCreate(
            flash_task,                  // 태스크 함수
            "flash_task",                // 태스크 이름
            FLASH_TASK_STACK_SIZE,       // 스택 크기
            &ucParameterToPass,        // 파라미터
            tskIDLE_PRIORITY + 1,      // 우선순위
            NULL
        ) != pdPASS) {                 // pdTRUE 대신 pdPASS를 쓰는 것이 FreeRTOS 관례입니다.
        
        ESP_LOGE(TAG, "Error creating Button_task on Core 1");
    }
    load_facto_configuration();
    load_app_configuration();

    load_wifi_configuration();

    load_motor_time();
    load_filter_time();    
    load_ontime_time();
    load_motor_ontime_time();
    load_rtc_init_time();
    dump_all_configurations();
    //app_config.gate_way_rssi_th = -100;
}




