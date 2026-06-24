#ifndef __SPIFFS_UTIL_H__
#define __SPIFFS_UTIL_H__
void spiffs_facto(void);
void spiffs_delete(const char * path);
void spiffs_info(void);
void spiffs_format(void);
void spiffs_test(void);
void spiffs_init(void);

int spiffs_byte_read(char* path, char* data, int len);
int spiffs_string_read(char* path, char* data, int len);
int spiffs_byte_write(char* path, char* data, int len);
int spiffs_string_write(char* path, char* data, int len);
#define MAC_ADDRESS_LEN 6


#define BLE_MAC_ADDR_PATH               "/spiffs/ble_mac_file.bin"
#define BLE_INFO_PATH                   "/spiffs/ble_info_file.bin"

#define WIFI_INFO_PATH                  "/spiffs/wifi_info_file.bin"


#endif

