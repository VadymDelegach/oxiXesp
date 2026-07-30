#include "esp_http_client.h"
#include "esp_partition.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "libswd.h"

/* Defines for error check */
#define TAG_OXI		"OXI:"
#define OXI_ERR		"ERR:"
#define OTA_TAG		"OTA:"
#define SWD_TAG		"SWD:"

#define BUF_SIZE	512 // UART Rx and Tx buffers size
#define TITLE_LEN 22
#define VERSION_LEN 10
#define STM_PAGE_SIZE 2048
#define STM_END_PG 128
#define BASE_STM_FLASH 0x8000000

typedef struct {
	bool spi_bus_init;
	spi_device_handle_t deviceSWD;
	libswd_ctx_t *libswdctx;
	int adr_swd;
	int dat_swd[4];
} swd_info_t;

typedef struct {
	char *type;
	char *getreq;
	char *data;
	int data_len;
	esp_http_client_handle_t client;
	char title[TITLE_LEN];
	char vers[VERSION_LEN];
	int size;
	uint32_t chunk;
	swd_info_t swdinf;
	const esp_partition_t *stm_part;
} upgrade_info;

void oxi_err_check(const char *dscr, esp_err_t er);
void uart_event_task(void *pvParameters);
esp_err_t wifi_init(void);
void register_ble_ev_hndl(void);
void start_prov(void* pvParameter);
void get_access_token(void);
void mqtt_start(void);
void ota_task(void *pvParameter);
void ota_err_msg(uint8_t er);
bool swd_init(swd_info_t *swdinf);
int get_size_stm_prog_old(swd_info_t *swdinf);
int save_stm_range(upgrade_info *upinf, int size);
int write_stm(upgrade_info *upinf, int stm_adr, int offset, int size);
void restartSTM(upgrade_info *upinf);