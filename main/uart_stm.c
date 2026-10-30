#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "driver/uart.h"
#include "oxiXesp.h"

#define MIN_RSSI -90
#define MAX_RSSI -30

extern QueueHandle_t uart0_queue;
extern bool rtc_req;
extern char espvers[];
extern bool stm_on_flag;

static void rx_parce(char* dat)
{
	if (strstr(dat, "+STA")) {
		xTaskCreate(start_prov, "Start BLE", 4096, NULL, tskIDLE_PRIORITY + 1,
																		NULL);
	} else if (strstr(dat, "+SWD")) {
		xTaskCreate(ota_task, "ota_task", 8192, (void *)"sw", 4, NULL);
	} else if (strstr(dat, "+OTA")) {
		xTaskCreate(ota_task, "ota_task", 8192, (void *)"fw", 4, NULL);
	} else if (strstr(dat, "+AcTkDel")) {
		delete_access_token();
	} else if (strstr(dat, "+RTC")) {
		//printf("OXI:RTC:2026;8;25;15;1\n");
		rtc_req = true;
	} else if (strstr(dat, "+VERS") && strstr(espvers, "oxiXesp")) {
		printf("%sVERS:%s\n", TAG_OXI, espvers + strlen("oxiXesp"));
	} else if (strstr(dat, "+RSSI")) {
		int rssi = 0;
		esp_wifi_sta_get_rssi(&rssi);
		if (rssi < MIN_RSSI) {
			rssi = 0;
		} else if (rssi > MAX_RSSI) {
			rssi = 100;
		} else {
			rssi = (int)(100.0 *
					(float)(rssi - MIN_RSSI) / (float)(MAX_RSSI - MIN_RSSI));
		}
		printf("%sRSSI:%d\n", TAG_OXI, rssi);
		stm_on_flag = true;
	}
}

void uart_event_task(void *pvParameters)
{
	char* rx = NULL;
	uart_event_t event;
	esp_err_t er;
	size_t rx_len;

	if ((rx = malloc(BUF_SIZE)) == NULL) {
		oxi_err_check("Memory for UART receive", ESP_ERR_NO_MEM);
		esp_restart();
	}
	// Task cycle
	while (1) {
		if (!xQueueReceive(uart0_queue, &event, portMAX_DELAY))
			continue;
		bzero(rx, BUF_SIZE);
		/* handle receive data event */
		if (event.type == UART_DATA) {
			er = uart_get_buffered_data_len(UART_NUM_0, &rx_len);
			if (er != ESP_OK) {
				oxi_err_check("Determinate received data length", er);
				continue;
			}
			if (uart_read_bytes(UART_NUM_0, rx, rx_len, portMAX_DELAY) < 0) {
				oxi_err_check("Read received data", ESP_ERR_INVALID_SIZE);
				continue;
			}
			/* parcing received data */
			rx_parce(rx);
		} else {
			xQueueReset(uart0_queue);
		}
	}
	free(rx);
	vTaskDelete(NULL);
}