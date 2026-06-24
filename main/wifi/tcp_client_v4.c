/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "sdkconfig.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>            // struct addrinfo
#include <arpa/inet.h>
#include "esp_netif.h"
#include "esp_log.h"
#include "simple_ota_example.h"
#include "FreeRTOS_CLI.h"
#if defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#include "addr_from_stdin.h"
#endif

static const char *TAG = "example";

#define DOMAIN 1
static char buffer[100];
void tcp_client(char* addr, unsigned short port)
{
    char rx_buffer[128];

    int addr_family = 0;
    int ip_protocol = 0;
    static uint32_t index = 0;
    #if DOMAIN
    struct hostent *he;
    he = gethostbyname(addr);
    if (he == NULL) {
        APP_String_printf("DNS resolution failed for %s\n", addr);
        return;
    }
    #endif
    while (1) {
#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        #if DOMAIN
        memcpy(&dest_addr.sin_addr, he->h_addr, he->h_length);
        #else
        inet_pton(AF_INET, addr, &dest_addr.sin_addr);
        #endif
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = { 0 };
        ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_STREAM, &ip_protocol, &addr_family, &dest_addr));
#endif

        int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        //printf("Socket created, connecting to %s:%d", addr, port);
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", addr, port);
        while(1)
        {
            int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err != 0) {
                ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
                shutdown(sock, 0);
                close(sock);
                break;
            }
            struct timeval tv_timeo = { 1, 0 }; /* 3.5 second */
            err = setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO, &tv_timeo, sizeof( tv_timeo ) );
            if (err != 0) 
            {
                ESP_LOGE(TAG, "setsockopt: errno %d", errno);
                        break;
            }

            ESP_LOGI(TAG, "Successfully connected");

            while (1) {
                #if 1
                index++;
                sprintf((char*)buffer, "%ld\r\n",index);
                #else
               sprintf((char*)buffer,
                "GET /?value=%ld HTTP/1.1\r\n"
                "Host: 192.168.0.64:8888\r\n"
                "Connection: close\r\n"
                "\r\n",
                index);
                #endif
                int err = send(sock, buffer, strlen(buffer), 0);
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }

                int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                // Error occurred during receiving
                if (len < 0) {
                   // ESP_LOGE(TAG, "recv failed: errno %d", errno);
                }
                // Data received
                else {
                   
                    rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                    ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr);
                    ESP_LOGI(TAG, "%s", rx_buffer);
                }
            }
        }
        vTaskDelay(10);
    }
}
