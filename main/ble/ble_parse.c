#include "ble_parse.h"
#include "esp_mac.h" // MAC 주소 관련 API 헤더
#include "wifi_task.h"
#include "ble_task.h"
#include "app_config_flash.h"
#include "wifi_task.h"
void BLE_Receive_data(uint8_t* data, uint16_t len)
{
    printf("[새 태스크] %d 바이트 데이터 처리 중: ", len);

    for(int i = 0; i < len; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");


    char buf[128];

    if(len >= sizeof(buf))
        len = sizeof(buf) - 1;

    memcpy(buf, data, len);
    buf[len] = '\0';


    // scan 명령
    if(strcmp(buf, "scan") == 0)
    {
        wifi_scan_start();
        return;
    }


    char *cmd;
    char *index;
    char *ssid;
    char *pass;


    cmd = strtok(buf, " ");
    index = strtok(NULL, " ");
    ssid = strtok(NULL, " ");
    pass = strtok(NULL, " \r\n");


    printf("cmd   : %s\n", cmd ? cmd : "NULL");
    printf("index : %s\n", index ? index : "NULL");
    printf("ssid  : %s\n", ssid ? ssid : "NULL");
    printf("pass  : %s\n", pass ? pass : "NULL");


    if(cmd && strcmp(cmd, "CONNECT_AP") == 0)
    {
        if(ssid == NULL || pass == NULL)
        {
            printf("CONNECT_AP 파라미터 부족\n");
            return;
        }


        printf("SSID=%s PASS=%s\n", ssid, pass);


        wifi_config_t* wifi_config = get_wifi_config();


        memset(wifi_config->conn_ssid, 0,
               sizeof(wifi_config->conn_ssid));

        memset(wifi_config->conn_password, 0,
               sizeof(wifi_config->conn_password));


        strncpy((char*)wifi_config->conn_ssid,
                ssid,
                sizeof(wifi_config->conn_ssid)-1);

        strncpy((char*)wifi_config->conn_password,
                pass,
                sizeof(wifi_config->conn_password)-1);

        save_wifi_configuration();
        Wifi_Connect((uint8_t*)ssid,(uint8_t*)pass);
        printf("저장 완료\n");
    }
}
  //esp_read_mac(MyMac,ESP_MAC_WIFI_STA);
