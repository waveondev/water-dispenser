#include <string.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
//#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "lwip/err.h"
#include "lwip/tcp.h"

#include "FreeRTOS_CLI.h"
#define SERVER_PORT 1234  // 원하는 포트

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  tcp_server_error(void *arg, err_t err);

// 클라이언트로부터 데이터 수신 콜백
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    // 받은 데이터 출력
    APP_String_printf("Received: %.*s\n", p->len, (char *)p->payload);

    // echo back
    tcp_write(tpcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);

    pbuf_free(p);
    return ERR_OK;
}

// 클라이언트 연결 시 콜백
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);     // 수신 콜백 설정
    tcp_err(newpcb, tcp_server_error);     // 에러 콜백
    return ERR_OK;
}

// 연결 에러 시 콜백
static void tcp_server_error(void *arg, err_t err) {
    APP_String_printf("TCP connection aborted with error: %d\n", err);
}

// TCP 서버 초기화 함수
void tcp_server(void) {
    struct tcp_pcb *pcb;

    pcb = tcp_new();
    if (pcb == NULL) {
        APP_String_printf("Error creating PCB\n");
        return;
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, SERVER_PORT) != ERR_OK) {
        APP_String_printf("TCP bind failed\n");
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_accept(pcb, tcp_server_accept);

   // printf("TCP server listening on port %d\n", SERVER_PORT);
   // while(1)
    {
    //   vTaskDelay(1); 
    }
}
