#include "esp_err.h"

/* Defines for error check */
#define TAG_OXI		"OXI:"
#define OXI_ERR		"ERR:"

#define BUF_SIZE	512 // UART Rx and Tx buffers size

void oxi_err_check(const char *dscr, esp_err_t er);
void uart_event_task(void *pvParameters);
esp_err_t wifi_init(void);
void register_ble_ev_hndl(void);
void start_prov(void* pvParameter);
void get_access_token(void);
void mqtt_start(void);