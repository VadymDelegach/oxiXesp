#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "driver/uart.h"
#include "oxiXesp.h"

extern QueueHandle_t uart0_queue;

static void rx_parce(char* dat)
{

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
		}
	}
	free(rx);
	vTaskDelete(NULL);
}