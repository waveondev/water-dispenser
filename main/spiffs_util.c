#include "spiffs_util.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "dirent.h"
#include "FreeRTOS_CLI.h"
static const char *TAG = "spiffs_util";

esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 16,
    .format_if_mount_failed = true
};

esp_vfs_spiffs_conf_t* mount = NULL;

#if 0
void spiffs_test(void)
{

    ESP_LOGI(TAG, "Initializing SPIFFS");



    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(mount);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

#ifdef CONFIG_EXAMPLE_SPIFFS_CHECK_ON_START
    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(mount->partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    } else {
        ESP_LOGI(TAG, "SPIFFS_check() successful");
    }
#endif

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(mount->partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(mount->partition_label);
        return;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Check consistency of reported partiton size info.
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
        ret = esp_spiffs_check(mount->partition_label);
        // Could be also used to mend broken files, to clean unreferenced pages, etc.
        // More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
            return;
        } else {
            ESP_LOGI(TAG, "SPIFFS_check() successful");
        }
    }

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/spiffs/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/spiffs/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/spiffs/hello.txt", "/spiffs/foo.txt") != 0) {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/spiffs/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(mount->partition_label);
    ESP_LOGI(TAG, "SPIFFS unmounted");

}
#endif

int spiffs_string_write(char* path, char* data, int len)
{
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for spiffs_string_write");
        return -1;
    }
    fprintf(f,data);
    fclose(f);
    return 0;
}

int spiffs_byte_write(char* path, char* data, int len)
{
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for spiffs_byte_write");
        return -1;
    }
    fwrite(data, len, 1, f);
    fclose(f);
    return 0;
}

int spiffs_string_read(char* path, char* data, int len)
{
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for spiffs_string_read");
        return -1;
    }
    fgets(data, len, f);
    fclose(f);
    return 0;
}

int spiffs_byte_read(char* path, char* data, int len)
{
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for spiffs_byte_read %s",path);
        return -1;
    }
    fread(data, len, 1, f);
    fclose(f);
    return 0;
}


static int spiffs_enable(void)
{
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return -1;
    }

    mount = &conf;
    return 0;
}

static int spiffs_disable(void)
{
    esp_err_t ret = 0;
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    // All done, unmount partition and disable SPIFFS
    ret = esp_vfs_spiffs_unregister(mount->partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)%x", esp_err_to_name(ret),ret);
        return -1;
    }

    ESP_LOGI(TAG, "SPIFFS unmounted");
    mount = NULL;
    return 0;
}

void spiffs_format(void)
{
    if(mount == NULL)
    {
        ESP_LOGE(TAG, "mount faile");
        return;
    }
    esp_spiffs_format(mount->partition_label);
}
void spiffs_info(void)
{
    if(mount == NULL)
    {
        ESP_LOGE(TAG, "mount faile");
        return;
    }
    esp_err_t ret = 0;
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(mount->partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        spiffs_format();
        return;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Check consistency of reported partiton size info.
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
        ret = esp_spiffs_check(mount->partition_label);
        // Could be also used to mend broken files, to clean unreferenced pages, etc.
        // More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
            return;
        } else {
            ESP_LOGI(TAG, "SPIFFS_check() successful");
        }
    }

        DIR* dir = opendir("/spiffs");
        if (dir == NULL) {
            return;
        }

        while (true) {
            struct dirent* de = readdir(dir);
            if (!de) {
                break;
            }
            
            APP_String_printf("Found file: %s\n", de->d_name);
        }
        closedir(dir);

}
void spiffs_facto(void)
{
    spiffs_delete(BLE_INFO_PATH);
    spiffs_delete(WIFI_INFO_PATH);
}
void spiffs_delete(const char * path)
{
  ESP_LOGI(TAG,"Deleting file: %s\r\n", path);
  if(remove(path) == 0){
    ESP_LOGI(TAG,"file deleted");
  } else {
    ESP_LOGE(TAG,"delete failed");
  }
}

void spiffs_init(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_err_t ret = 0;
    if(spiffs_enable() != 0)
        return;

    
    #if 0
    char mac[6];
    char read_mac[6];
    mac[0] = 0x3c;
    mac[1] = 0xea;
    mac[2] = 0xf9;
    mac[3] = 0x10;
    mac[4] = 0x01;
    mac[5] = 0x03;
    #if 0
    esp_iface_mac_addr_set(mac, ESP_MAC_BASE);
    #endif
    spiffs_mac_write(mac);
    spiffs_mac_read(read_mac);
    ESP_LOGI(TAG, "mac = %02x %02x %02x %02x %02x %02x",read_mac[0],read_mac[1],read_mac[2],read_mac[3],read_mac[4],read_mac[5]);
    #endif
    //if(spiffs_disable() != 0)
        //return;
}